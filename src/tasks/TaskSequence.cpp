#include <hksr/framework/tasks/TaskSequence.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace hksr::framework {
namespace {
std::vector<TaskNodeIndex> Children(const TaskNodeOperation& op) {
    return std::visit([](const auto& node)->std::vector<TaskNodeIndex> {
        using T=std::decay_t<decltype(node)>;
        if constexpr(std::is_same_v<T,TaskSerial> || std::is_same_v<T,TaskParallel>)return node.children;
        else if constexpr(std::is_same_v<T,TaskRepeat>)return {node.child};
        else if constexpr(std::is_same_v<T,TaskBranch>)return {node.yes,node.no};
        else return {};
    },op);
}
}
void ValidateTaskSequence(const TaskSequence& sequence) {
    if(sequence.entry>=sequence.nodes.size() || (sequence.interruption && *sequence.interruption>=sequence.nodes.size()))
        throw std::invalid_argument("task sequence entry is absent");
    // Explicit repeat nodes own iteration. Raw structural cycles have no
    // separate cursor/lifetime and would recurse without a scheduler boundary.
    std::set<std::string> requests;
    for(const auto& node:sequence.nodes)if(const auto* submit=std::get_if<TaskSubmit>(&node.operation))requests.insert(submit->request);
    std::vector<int> colors(sequence.nodes.size());
    std::function<void(TaskNodeIndex)> visit=[&](auto index) {
        if(index>=sequence.nodes.size())throw std::invalid_argument("task child is absent");
        if(colors[index]==1)throw std::invalid_argument("structural task cycle at "+sequence.nodes[index].source);
        if(colors[index]==2)return;
        colors[index]=1;
        const auto& op=sequence.nodes[index].operation;
        if(const auto* repeat=std::get_if<TaskRepeat>(&op))
            if(!repeat->count && !repeat->until_finish)throw std::invalid_argument("task loop has no exit at "+sequence.nodes[index].source);
        if(const auto* wait=std::get_if<TaskWaitSeconds>(&op))
            if(!std::isfinite(wait->seconds) || wait->seconds<0)throw std::invalid_argument("invalid task wait duration");
        if(const auto* wait=std::get_if<TaskWaitRequest>(&op))
            if(wait->request.empty() || !requests.count(wait->request) || !std::isfinite(wait->progress) || wait->progress<0)
                throw std::invalid_argument("invalid or undeclared request wait at "+sequence.nodes[index].source);
        if(const auto* submit=std::get_if<TaskSubmit>(&op))
        {
            if(submit->request.empty())throw std::invalid_argument("task request name is empty");
            if(const auto* sound=std::get_if<TaskSoundCommand>(&submit->operation))
                if(sound->participant.name.empty() || sound->event.empty() || sound->event.find('\0')!=std::string::npos)
                    throw std::invalid_argument("invalid task sound command");
        }
        for(auto child:Children(op))visit(child);
        colors[index]=2;
    };
    for(TaskNodeIndex i=0;i<sequence.nodes.size();++i)visit(i);
}
struct TaskSequenceExecution::State {
    struct Cursor {
        TaskNodeIndex node;
        bool entered=false,done=false,progressed=false;
        double elapsed=0,credit=0,slack=0;
        std::uint64_t frame=0,iterations=0;
        std::size_t child=0;
        std::vector<Cursor> children;
        std::optional<std::size_t> request;
        explicit Cursor(TaskNodeIndex value):node(value){}
    };
    struct Request {TaskSubmission value;bool finished=false,cancelled=false;std::string name,source;};
    struct Step {bool done=false;double remainder=0;};
    std::shared_ptr<const TaskSequence> sequence;
    TaskSequenceServices services;
    TaskExecutionStatus status;
    Cursor cursor;
    std::map<std::size_t,Request> requests;
    std::size_t next_request=0;
    std::map<std::string,std::size_t> named;
    std::optional<std::uint64_t> last_frame;
    bool advancing=false;
    std::optional<TaskExitReason> pending_stop;
    State(TaskExecutionToken token,std::shared_ptr<const TaskSequence> plan,TaskSequenceServices ports)
        :sequence(std::move(plan)),services(std::move(ports)),cursor(sequence->entry) {status.token=token;}
    void Release(Request& request,TaskExitReason reason) {
        if(request.cancelled || (reason==TaskExitReason::Completed && request.finished))return;
        if(reason==TaskExitReason::Completed)request.finished=true;else request.cancelled=true;
        const auto receipt=request.value.receipt;
        try{services.release(receipt,reason);}catch(const std::exception& e){status.issues.push_back(e.what());}
    }
    void ReleaseAll(TaskExitReason reason) {
        // Claim first: a callback may synchronously deliver an observation or
        // ask for a stop. Finish and cancellation are each claimed once.
        for(auto& [id,request]:requests){(void)id;Release(request,reason);}
    }
    void Collect() {
        if(status.phase==TaskExecutionPhase::Draining && std::all_of(requests.begin(),requests.end(),[](const auto& item){return item.second.value.state.drained;}))
            status.phase=TaskExecutionPhase::Finished;
    }
    void CollectRequests() {
        std::set<std::size_t> live;
        for(const auto& [name,id]:named){(void)name;live.insert(id);}
        std::function<void(const Cursor&)> visit=[&](const auto& node) {
            if(node.done)return;
            if(node.request)live.insert(*node.request);
            for(const auto& child:node.children)visit(child);
        };
        visit(cursor);
        for(auto it=requests.begin();it!=requests.end();) {
            if(!live.count(it->first) && (it->second.finished || it->second.cancelled) && it->second.value.state.drained)it=requests.erase(it);
            else ++it;
        }
        status.retained_requests=requests.size();
    }
    void Close(TaskExitReason reason,bool finished_branch=false) {
        status.reason=reason;status.phase=TaskExecutionPhase::Draining;
        // An interruption branch can end by submitting a final cleanup command.
        // Its own completed work finishes normally even though the original
        // task was replaced. Prior requests were cancelled when it began.
        ReleaseAll(finished_branch?TaskExitReason::Completed:reason);Collect();
    }
    void Stop(TaskExitReason reason) {
        if(status.phase==TaskExecutionPhase::Finished)return;
        if(status.phase==TaskExecutionPhase::Draining){status.reason=reason;ReleaseAll(reason);Collect();return;}
        const bool authored=(reason==TaskExitReason::Interrupted || reason==TaskExitReason::Replaced) && sequence->interruption &&
            status.phase==TaskExecutionPhase::Running;
        if(!authored){Close(reason);return;}
        status.reason=reason;status.phase=TaskExecutionPhase::Interrupting;ReleaseAll(reason);
        cursor=Cursor(*sequence->interruption);named.clear();
    }
    void FlushStop() {
        while(pending_stop) {const auto reason=*pending_stop;pending_stop.reset();Stop(reason);}
    }
    void Fail(const Cursor& c,const std::string& detail) {
        status.issues.push_back(sequence->nodes[c.node].source+": "+detail);pending_stop=TaskExitReason::Failed;
    }
    Step Drive(Cursor& c,double seconds,std::uint64_t frame) {
        // Finished parallel siblings retain their earlier unused time until
        // the join completes. Budget yields retain time at the suspended node.
        if(c.done){c.slack+=seconds;return {true,c.slack};}
        if(pending_stop)return {};
        if(services.yield && services.yield()){c.credit+=seconds;return {};}
        seconds+=c.credit;c.credit=0;
        const bool first=!c.entered;c.entered=true;
        const auto finish=[&](double remaining){c.done=true;c.slack=remaining;return Step{true,remaining};};
        auto result=std::visit([&](const auto& node)->Step {
            using T=std::decay_t<decltype(node)>;
            if constexpr(std::is_same_v<T,TaskWaitSeconds>) {
                const auto consumed=std::min(seconds,std::max(0.0,node.seconds-c.elapsed));c.elapsed+=consumed;
                if(consumed>0)c.progressed=true;
                if(c.elapsed>=node.seconds)return finish(seconds-consumed);
            } else if constexpr(std::is_same_v<T,TaskWaitFrame>) {
                if(first)c.frame=frame;
                if(frame!=c.frame){c.progressed=true;return finish(0);} // no sub-frame timestamp
            } else if constexpr(std::is_same_v<T,TaskSubmit>) {
                if(!first)return finish(seconds);
                if(const auto old=named.find(node.request);old!=named.end()) {
                    auto& request=requests.at(old->second);
                    if(!request.value.state.performance_complete && !request.value.state.superseded && !request.cancelled) {
                        Fail(c,"request name is still active: "+node.request);return {};
                    }
                    Release(request,TaskExitReason::Completed);
                    if(pending_stop)return {};
                }
                const auto id=++next_request;
                auto value=services.submit(status.token,id,node);
                if(value.receipt.execution.value!=status.token.value || value.receipt.request!=id ||
                    (!value.receipt.entity_generation && value.state.submitted) || value.receipt.participant.name.empty()) {
                    Fail(c,"submission returned invalid request identity");return {};
                }
                named[node.request]=id;
                const auto& submitted=requests.emplace(id,Request{std::move(value),false,false,
                    node.request,sequence->nodes[c.node].source}).first->second.value.state;
                status.retained_requests=requests.size();
                if(!submitted.error.empty()){Fail(c,submitted.error);return {};}
                return finish(seconds);
            } else if constexpr(std::is_same_v<T,TaskWaitRequest>) {
                if(!c.request) {
                    const auto found=named.find(node.request);
                    if(found==named.end()) {
                        status.waiting.push_back(sequence->nodes[c.node].source+": awaiting submission "+node.request);return {};
                    }
                    c.request=found->second;
                }
                const auto& value=requests.at(*c.request).value.state;
                if(!value.error.empty()){Fail(c,value.error);return {};}
                const bool done=node.signal==TaskWaitSignal::Submitted?value.submitted:
                    node.signal==TaskWaitSignal::Started?value.started:
                    node.signal==TaskWaitSignal::RequestComplete?value.request_complete:
                    node.signal==TaskWaitSignal::PerformanceComplete?value.performance_complete:
                    node.signal==TaskWaitSignal::Drained?value.drained:value.progress && *value.progress>=node.progress;
                if(done){if(!first)c.progressed=true;return finish(first?seconds:0);} // unknown asynchronous completion time
                if(node.signal==TaskWaitSignal::Started && value.performance_complete) {
                    Fail(c,"request ended without an observed start: "+node.request);return {};
                }
                if(value.superseded) {
                    const auto required=node.signal==TaskWaitSignal::Progress?"progress "+std::to_string(node.progress):
                        node.signal==TaskWaitSignal::Submitted?std::string("submission"):
                        node.signal==TaskWaitSignal::Started?std::string("playback start"):
                        node.signal==TaskWaitSignal::RequestComplete?std::string("acceptance"):
                        node.signal==TaskWaitSignal::PerformanceComplete?std::string("performance completion"):std::string("drain");
                    Fail(c,"request superseded before "+required+": "+node.request+
                        "; observed="+(value.progress?std::to_string(*value.progress):std::string("unavailable")));
                    return {};
                }
                if(node.signal==TaskWaitSignal::Progress && value.performance_complete) {
                    Fail(c,"request completed before progress "+std::to_string(node.progress)+": "+node.request+
                        "; observed="+(value.progress?std::to_string(*value.progress):std::string("unavailable")));
                    return {};
                }
            } else if constexpr(std::is_same_v<T,TaskSerial>) {
                if(first)for(auto child:node.children)c.children.emplace_back(child);
                double remaining=seconds;
                while(c.child<c.children.size()) {
                    const auto step=Drive(c.children[c.child],remaining,frame);if(!step.done)return {};
                    remaining=step.remainder;++c.child;
                }
                return finish(remaining);
            } else if constexpr(std::is_same_v<T,TaskParallel>) {
                if(first)for(auto child:node.children)c.children.emplace_back(child);
                bool done=true;double remaining=c.children.empty()?seconds:std::numeric_limits<double>::infinity();
                for(auto& child:c.children) {
                    const auto step=Drive(child,seconds,frame);done=done && step.done;
                    remaining=std::min(remaining,step.remainder);
                }
                if(done)return finish(remaining);
            } else if constexpr(std::is_same_v<T,TaskRepeat>) {
                double remaining=seconds;
                for(;;) {
                    if(c.children.empty()) {
                        if((node.count && c.iterations>=*node.count) || (node.until_finish && status.finish_requested))return finish(remaining);
                        c.children.emplace_back(node.child);
                    }
                    const auto step=Drive(c.children[0],remaining,frame);
                    if(!step.done)return {};
                    const bool progressed=c.children[0].progressed;
                    if(!node.count && !progressed){Fail(c,"loop iteration has no time or frame progress");return {};}
                    c.progressed=c.progressed || progressed;
                    remaining=step.remainder;++c.iterations;c.children.clear();
                    CollectRequests();
                    if(services.yield && services.yield()){c.credit+=remaining;return {};}
                }
            } else if constexpr(std::is_same_v<T,TaskBranch>) {
                if(first) {
                    if(!services.condition){Fail(c,"condition service is absent");return {};}
                    const auto state=services.condition(node.condition);
                    if(state==TaskAvailability::Unknown){Fail(c,"branch condition is unresolved");return {};}
                    c.children.emplace_back(state==TaskAvailability::Available?node.yes:node.no);
                }
                const auto step=Drive(c.children[0],seconds,frame);if(step.done)return finish(step.remainder);
            }
            status.waiting.push_back(sequence->nodes[c.node].source);return {};
        },sequence->nodes[c.node].operation);
        for(const auto& child:c.children)c.progressed=c.progressed || child.progressed;
        return result;
    }
};
TaskSequenceExecution::TaskSequenceExecution(TaskExecutionToken token,std::shared_ptr<const TaskSequence> plan,TaskSequenceServices services) {
    if(!token.value || !plan || !services.submit || !services.release)throw std::invalid_argument("task execution services missing");
    ValidateTaskSequence(*plan);state_=std::make_unique<State>(token,std::move(plan),std::move(services));
}
TaskSequenceExecution::~TaskSequenceExecution()=default;
void TaskSequenceExecution::Advance(double seconds,std::uint64_t frame) {
    auto& s=*state_;if(!std::isfinite(seconds) || seconds<0)throw std::invalid_argument("invalid task delta");
    if(s.advancing || s.status.phase==TaskExecutionPhase::Finished)return;
    s.Collect();if(s.status.phase==TaskExecutionPhase::Draining || s.status.phase==TaskExecutionPhase::Finished)return;
    if(s.last_frame && frame<=*s.last_frame)return;
    s.last_frame=frame;s.status.waiting.clear();s.advancing=true;
    struct Exit {bool& flag;~Exit(){flag=false;}} exit{s.advancing};
    try {
        const auto result=s.Drive(s.cursor,seconds,frame);
        if(s.pending_stop)s.FlushStop();
        else if(result.done)s.Close(s.status.reason.value_or(TaskExitReason::Completed),true);
    }catch(const std::exception& e){s.status.issues.push_back(e.what());s.pending_stop.reset();s.Close(TaskExitReason::Failed);}
    s.FlushStop();
    s.CollectRequests();
}
bool TaskSequenceExecution::Observe(const TaskRequestReceipt& receipt,const TaskRequestState& update) {
    auto& s=*state_;
    const auto found=s.requests.find(static_cast<std::size_t>(receipt.request));
    if(found==s.requests.end())return false;
    auto expected=found->second.value.receipt;
    if(update.submitted && !receipt.entity_generation)return false;
    if(!expected.entity_generation)expected.entity_generation=receipt.entity_generation;
    if(!expected.model)expected.model=receipt.model; // first actual model binding
    if(!(expected==receipt))return false;
    if(!found->second.value.receipt.entity_generation)found->second.value.receipt.entity_generation=receipt.entity_generation;
    if(!found->second.value.receipt.model)found->second.value.receipt.model=receipt.model;
    auto& value=found->second.value.state;
    value.submitted=value.submitted || update.submitted;value.request_complete=value.request_complete || update.request_complete;
    value.performance_complete=value.performance_complete || update.performance_complete;value.drained=value.drained || update.drained;
    value.superseded=value.superseded || update.superseded;
    value.started=value.started || update.started;
    if(update.progress && std::isfinite(*update.progress) && *update.progress>=0 && (!value.progress || *update.progress>*value.progress))value.progress=update.progress;
    if(!update.error.empty()) {
        if(value.error!=update.error)s.status.issues.push_back("request="+std::to_string(receipt.request)+": "+update.error);
        value.error=update.error;Stop(TaskExitReason::Failed);
    }
    s.Collect();return true;
}
void TaskSequenceExecution::RequestFinish(){state_->status.finish_requested=true;}
void TaskSequenceExecution::Fail(std::string error) {
    if(state_->status.issues.empty() || state_->status.issues.back()!=error)state_->status.issues.push_back(std::move(error));
    Stop(TaskExitReason::Failed);
}
void TaskSequenceExecution::Stop(TaskExitReason reason) {
    auto& s=*state_;
    if(reason==TaskExitReason::Completed)throw std::invalid_argument("completion comes from task execution");
    const auto severity=[](TaskExitReason value){
        return value==TaskExitReason::Deleted || value==TaskExitReason::WorldLeft || value==TaskExitReason::ActorLost?3:
            value==TaskExitReason::Failed?2:1;
    };
    if(!s.pending_stop || severity(reason)>severity(*s.pending_stop))s.pending_stop=reason;
    if(!s.advancing) {
        s.advancing=true;struct Exit{bool& flag;~Exit(){flag=false;}} exit{s.advancing};s.FlushStop();
    }
}
const TaskExecutionStatus& TaskSequenceExecution::Status() const{return state_->status;}
std::vector<TaskRequestSnapshot> TaskSequenceExecution::Requests() const {
    std::vector<TaskRequestSnapshot> out;out.reserve(state_->requests.size());
    for(const auto& [id,request]:state_->requests) {
        (void)id;out.push_back({request.name,request.source,request.value.receipt,request.value.state});
    }
    return out;
}
bool TaskSequenceExecution::Drained() const{return state_->status.phase==TaskExecutionPhase::Finished;}
}
