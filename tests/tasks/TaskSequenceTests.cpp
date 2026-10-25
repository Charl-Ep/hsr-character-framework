#include <hksr/framework/tasks/TaskSequence.hpp>
#include <stdexcept>

namespace {
using namespace hksr::framework;
void Check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
TaskNode Submit(const char* key,const char* actor="body") {return {key,TaskSubmit{key,TaskAnimationCommand{{actor},key,0,0}}};}
struct Host {
    std::vector<TaskSubmission> requests;
    std::vector<std::pair<TaskRequestReceipt,TaskExitReason>> releases;
    bool immediate=false,drain=true;
    std::function<void()> on_submit,on_release;
    TaskSequenceServices Services() {
        TaskSequenceServices result;
        result.submit=[&](TaskExecutionToken execution,std::uint64_t serial,const TaskSubmit& node) {
            const auto& animation=std::get<TaskAnimationCommand>(node.operation);
            TaskSubmission value;value.receipt={execution,serial,42,animation.participant,100};
            value.state.submitted=true;value.state.request_complete=true;value.state.performance_complete=immediate;
            value.state.drained=drain;requests.push_back(value);if(on_submit)on_submit();return value;
        };
        result.release=[&](const auto& receipt,auto reason){releases.push_back({receipt,reason});if(on_release)on_release();};
        return result;
    }
};
std::shared_ptr<TaskSequence> Plan(std::initializer_list<TaskNode> nodes,TaskNodeIndex entry) {
    auto result=std::make_shared<TaskSequence>();result->nodes=nodes;result->entry=entry;return result;
}
void Clocks() {
    Host host;host.immediate=true;
    auto plan=Plan({{"first",TaskWaitSeconds{0.2}},{"second",TaskWaitSeconds{0.3}},Submit("done"),{"serial",TaskSerial{{0,1,2}}}},3);
    TaskSequenceExecution run({1},plan,host.Services());run.Advance(0.5,1);
    Check(run.Drained() && host.requests.size()==1 && host.releases.size()==1,"serial waits did not pass remaining time");
    auto parallel=Plan({{"long",TaskWaitSeconds{0.5}},{"short",TaskWaitSeconds{0.2}},{"both",TaskParallel{{0,1}}},
        {"after",TaskWaitSeconds{0.25}},Submit("end"),{"serial",TaskSerial{{2,3,4}}}},5);
    TaskSequenceExecution branches({2},parallel,host.Services());branches.Advance(0.6,1);
    Check(host.requests.size()==1,"parallel remainder used shortest branch instead of longest");
    branches.Advance(0.2,2);Check(branches.Drained() && host.requests.size()==2,"parallel branches consumed each other's time");
    auto frame=Plan({{"frame",TaskWaitFrame{}},{"second",TaskWaitSeconds{0.1}},Submit("after-frame"),{"serial",TaskSerial{{0,1,2}}}},3);
    TaskSequenceExecution boundary({3},frame,host.Services());boundary.Advance(1,1);boundary.Advance(1,1);boundary.Advance(1,2);
    Check(host.requests.size()==2,"frame wait invented a sub-frame completion time");
    boundary.Advance(0.1,3);Check(boundary.Drained() && host.requests.size()==3,"frame successor failed");
}
void Receipts() {
    Host host;host.drain=false;
    auto plan=Plan({Submit("body"),Submit("prop","prop"),{"body-wait",TaskWaitRequest{"body"}},
        {"prop-wait",TaskWaitRequest{"prop"}},{"both",TaskParallel{{2,3}}},{"after",TaskWaitSeconds{0.1}},
        Submit("end"),{"serial",TaskSerial{{0,1,4,5,6}}}},7);
    TaskSequenceExecution run({7},plan,host.Services());run.Advance(0.5,1);
    Check(host.requests.size()==2 && !run.Drained(),"independent requests were not submitted");
    TaskRequestState done;done.performance_complete=true;
    auto wrong=host.requests[0].receipt;wrong.model=43;
    Check(!run.Observe(wrong,done),"old model receipt was accepted");wrong=host.requests[0].receipt;wrong.execution={8};
    Check(!run.Observe(wrong,done),"other execution receipt was accepted");
    wrong=host.requests[0].receipt;wrong.entity_generation=101;
    Check(!run.Observe(wrong,done),"other entity receipt was accepted");
    Check(run.Observe(host.requests[0].receipt,done),"body observation rejected");run.Advance(0.5,2);
    Check(host.requests.size()==2,"body progress completed prop wait");
    run.Observe(host.requests[1].receipt,done);run.Advance(0.5,3);
    Check(host.requests.size()==2,"asynchronous wait invented remaining frame time");
    run.Advance(0.1,4);Check(host.requests.size()==3 && run.Status().phase==TaskExecutionPhase::Draining,"completion ignored resource drain");
    Check(host.releases.size()==3 && host.releases[0].second==TaskExitReason::Completed,"natural finish did not release producers");
    run.Stop(TaskExitReason::Deleted);run.Stop(TaskExitReason::Deleted);
    Check(host.releases.size()==6,"tail cancellation missing or duplicated");
    done.drained=true;for(const auto& request:host.requests)run.Observe(request.receipt,done);
    Check(run.Drained(),"late drain receipt was lost");
    // Queueing before a model is ready is not a failed native call. Bind the
    // actual model on its first observation; it cannot change afterwards.
    auto services=host.Services();auto submit=services.submit;
    services.submit=[&](auto token,auto serial,const auto& node){auto value=submit(token,serial,node);value.receipt.model=0;value.state={};return value;};
    auto queued_plan=Plan({Submit("queued"),{"submitted",TaskWaitRequest{"queued",TaskWaitSignal::Submitted}},
        {"entry",TaskSerial{{0,1}}}},2);
    TaskSequenceExecution queued({8},queued_plan,services);queued.Advance(0,1);
    Check(queued.Status().issues.empty() && queued.Status().phase==TaskExecutionPhase::Running,"queued request incorrectly rejected");
    auto receipt=host.requests.back().receipt;TaskRequestState accepted;accepted.submitted=true;
    Check(queued.Observe(receipt,accepted),"first actual model binding rejected");
    auto replaced=receipt;replaced.model=99;Check(!queued.Observe(replaced,accepted),"request rebound to a replacement model");
    queued.Advance(0,2);Check(queued.Status().phase==TaskExecutionPhase::Draining,"queued submit did not complete");
    accepted.drained=true;queued.Observe(receipt,accepted);Check(queued.Drained(),"queued request did not drain");
    auto producer_later=Plan({{"wait",TaskWaitRequest{"parallel-submit",TaskWaitSignal::Submitted}},Submit("parallel-submit"),
        {"parallel",TaskParallel{{0,1}}}},2);
    TaskSequenceExecution forward({9},producer_later,host.Services());forward.Advance(0,1);
    Check(forward.Status().issues.empty(),"parallel request wait depended on vector traversal order");
    forward.Advance(0,2);Check(forward.Status().phase==TaskExecutionPhase::Draining,"parallel request was not observed after submission");
    done.drained=true;forward.Observe(host.requests.back().receipt,done);Check(forward.Drained(),"forward request did not drain");
}
void Loops() {
    Host host;host.immediate=true;
    auto plan=Plan({{"interval",TaskWaitSeconds{0.1}},Submit("repeat"),{"body",TaskSerial{{0,1}}},
        {"loop",TaskRepeat{2,3,false}}},3);
    TaskSequenceExecution finite({10},plan,host.Services());finite.Advance(0.35,1);
    Check(finite.Drained() && host.requests.size()==3,"finite loop discarded frame remainder");
    Check(finite.Status().retained_requests==1,"drained earlier loop requests accumulated");
    auto ongoing=Plan({{"interval",TaskWaitSeconds{0.1}},Submit("iteration"),{"body",TaskSerial{{0,1}}},
        {"loop",TaskRepeat{2,{},true}},Submit("outro"),{"entry",TaskSerial{{3,4}}}},5);
    TaskSequenceExecution continuous({11},ongoing,host.Services());continuous.Advance(0.05,1);continuous.RequestFinish();continuous.Advance(0.1,2);
    Check(continuous.Drained() && host.requests.size()==5,"finish did not complete iteration then outro");
    auto invalid=Plan({{"empty",TaskSerial{}},{"loop",TaskRepeat{0,{},true}}},1);
    TaskSequenceExecution no_progress({12},invalid,host.Services());no_progress.Advance(0,1);
    Check(no_progress.Status().reason==TaskExitReason::Failed && no_progress.Status().issues.size()==1,"immediate unbounded loop was not diagnosed");
    // Cooperative budget never truncates a finite sequence.
    bool yield=false;auto services=host.Services();services.yield=[&]{return yield;};
    services.submit=[&](auto token,auto serial,const auto& node){auto value=host.Services().submit(token,serial,node);yield=true;return value;};
    auto budgeted=Plan({Submit("one"),Submit("two"),{"serial",TaskSerial{{0,1}}}},2);
    TaskSequenceExecution work({13},budgeted,services);work.Advance(0,1);Check(!work.Drained(),"budget did not yield");
    yield=false;work.Advance(0,2);Check(work.Drained() && host.requests.size()==7,"yield discarded task cursor");
    // Different parallel branches may exhaust the work budget at different
    // points; retain their independent time credits until the join.
    unsigned checks=0;bool budget_enabled=true;auto credit_services=host.Services();
    credit_services.yield=[&]{return budget_enabled && ++checks>=4;};
    auto credits=Plan({{"short",TaskWaitSeconds{0.1}},{"long",TaskWaitSeconds{0.2}},
        {"parallel",TaskParallel{{0,1}}},{"tail",TaskWaitSeconds{0.2}},Submit("credited"),{"serial",TaskSerial{{2,3,4}}}},5);
    TaskSequenceExecution credited({14},credits,credit_services);credited.Advance(0.5,1);
    Check(!credited.Drained(),"credit test did not yield");
    budget_enabled=false;credited.Advance(0,2);
    Check(credited.Drained() && host.requests.size()==8,"budget yield discarded parallel remaining time");
}
void TerminalProgress() {
    Host host;
    auto plan=Plan({Submit("body"),{"final-cue",TaskWaitRequest{"body",TaskWaitSignal::Progress,1}},
        Submit("cue"),{"entry",TaskSerial{{0,1,2}}}},3);
    TaskSequenceExecution run({40},plan,host.Services());run.Advance(0,1);
    TaskRequestState ended;ended.performance_complete=true;ended.progress=0.99;
    run.Observe(host.requests[0].receipt,ended);run.Advance(0.1,2);
    Check(run.Status().reason==TaskExitReason::Failed && host.requests.size()==1,
        "terminal progress wait either hung or executed an unobserved cue");
    Check(run.Status().issues.at(0).find("observed=0.990000")!=std::string::npos,"terminal progress diagnostic omitted its observation");
    const auto receipts=run.Requests();
    Check(receipts.size()==1 && receipts[0].name=="body" && receipts[0].receipt.execution.value==40 &&
        receipts[0].state.performance_complete && receipts[0].state.progress==0.99,"request snapshot lost value/ownership");
    // Performance completion is a different, explicitly authored dependency.
    auto completion=Plan({Submit("body"),{"end",TaskWaitRequest{"body"}},Submit("after"),{"entry",TaskSerial{{0,1,2}}}},3);
    TaskSequenceExecution finished({41},completion,host.Services());finished.Advance(0,1);
    finished.Observe(host.requests.back().receipt,ended);finished.Advance(0.1,2);
    Check(host.requests.size()==3 && finished.Status().reason==TaskExitReason::Completed,"state exit did not satisfy completion wait");
}
void Stops() {
    Host host;host.immediate=true;
    auto plan=Plan({Submit("start"),{"wait",TaskWaitSeconds{1}},{"entry",TaskSerial{{0,1}}},Submit("abort")},2);plan->interruption=3;
    TaskSequenceExecution run({20},plan,host.Services());run.Advance(0.1,1);run.Stop(TaskExitReason::Interrupted);
    Check(run.Status().phase==TaskExecutionPhase::Interrupting && host.requests.size()==1,"interrupt branch timing changed");
    run.Advance(0.1,2);Check(run.Drained() && host.requests.size()==2 && host.releases.size()==2,"interrupt not independently executed");
    TaskSequenceExecution removed({21},plan,host.Services());removed.Advance(0.1,1);removed.Stop(TaskExitReason::WorldLeft);
    removed.Advance(0.1,2);Check(removed.Drained() && host.requests.size()==3,"world exit ran live-model abort presentation");
    TaskSequenceExecution reentrant({22},plan,host.Services());host.on_submit=[&]{reentrant.Stop(TaskExitReason::Deleted);};
    reentrant.Advance(0.1,1);host.on_submit={};
    Check(reentrant.Drained() && host.releases.size()==4,"stop during submission lost the newly returned receipt");
    TaskSequenceExecution cleanup({23},plan,host.Services());cleanup.Advance(0.1,1);
    host.on_release=[&]{cleanup.Stop(TaskExitReason::Deleted);};cleanup.Stop(TaskExitReason::Replaced);host.on_release={};
    cleanup.Advance(0.1,2);Check(cleanup.Drained() && host.requests.size()==5,"delete during cleanup was overwritten by interruption");
    auto branch=Plan({Submit("yes"),Submit("no"),{"branch",TaskBranch{TaskPhaseEquals{"x"},0,1}}},2);
    auto services=host.Services();services.condition=[](const auto&){return TaskAvailability::Unknown;};
    TaskSequenceExecution unknown({24},branch,services);unknown.Advance(0,1);
    Check(unknown.Status().reason==TaskExitReason::Failed && host.requests.size()==5,"unknown branch executed a guessed side");
    // Successful branch decisions do not merge the two instruction streams.
    services.condition=[](const auto&){return TaskAvailability::Unavailable;};
    TaskSequenceExecution alternate({25},branch,services);alternate.Advance(0,1);
    Check(alternate.Drained() && host.requests.size()==6,"false branch was not executed");
    // Partial submission remains owned and is cancelled exactly once.
    services=host.Services();auto submit=services.submit;
    services.submit=[&](auto token,auto serial,const auto& node){auto value=submit(token,serial,node);value.state.error="partial failure";return value;};
    TaskSequenceExecution partial({26},Plan({Submit("partial")},0),services);partial.Advance(0,1);
    const auto released=host.releases.size();partial.Stop(TaskExitReason::Deleted);
    Check(partial.Status().reason==TaskExitReason::Failed && host.releases.size()==released && released==7,"partial submission cleanup repeated or disappeared");
    // A raw graph cycle is invalid, rather than being treated as an implicit loop.
    auto cycle=Plan({{"cycle",TaskSerial{{0}}}},0);bool rejected=false;
    try{ValidateTaskSequence(*cycle);}catch(const std::invalid_argument&){rejected=true;}
    Check(rejected,"structural cycle accepted");
}
void SupersededWaits() {
    for(auto signal:{TaskWaitSignal::Progress,TaskWaitSignal::PerformanceComplete,TaskWaitSignal::RequestComplete}) {
        Host host;
        auto ports=host.Services();
        ports.submit=[&](auto token,auto serial,const auto& node){
            auto value=host.Services().submit(token,serial,node);
            value.state.request_complete=false;return value;
        };
        auto plan=Plan({Submit("body"),{"wait",TaskWaitRequest{"body",signal,1}},Submit("must-not-run"),
            {"entry",TaskSerial{{0,1,2}}}},3);
        TaskSequenceExecution run({42},plan,ports);run.Advance(0,1);
        TaskRequestState state;state.superseded=true;state.drained=true;state.progress=0.95;
        run.Observe(host.requests[0].receipt,state);run.Advance(0.1,2);
        Check(run.Status().reason==TaskExitReason::Failed && host.requests.size()==1,"superseded request satisfied an unmet dependency or hung");
    }
    Host host;
    auto plan=Plan({Submit("body"),{"cue",TaskWaitRequest{"body",TaskWaitSignal::Progress,0.9}},
        Submit("after"),{"entry",TaskSerial{{0,1,2}}}},3);
    TaskSequenceExecution run({43},plan,host.Services());run.Advance(0,1);
    TaskRequestState state;state.superseded=true;state.drained=true;state.progress=0.95;
    run.Observe(host.requests[0].receipt,state);run.Advance(0.1,2);
    Check(run.Status().reason==TaskExitReason::Completed && host.requests.size()==2,"handoff erased an already observed cue");
}
void AbilityInvocations() {
    Host host;host.immediate=true;
    auto plan=Plan({Submit("body"),{"body-wait",TaskWaitSeconds{1}},
        {"body-sequence",TaskSerial{{0,1}}},{"call",TaskAbilityCall{"phase",2,true}},
        {"join",TaskWaitAbilities{}},Submit("after"),{"entry",TaskSerial{{3,4,5}}}},6);
    TaskSequenceExecution joined({50},plan,host.Services());joined.Advance(0.4,1);
    Check(host.requests.size()==1 && !joined.ProducersComplete(),"ability invocation joined or completed prematurely");
    Check(joined.Status().abilities.size()==1 && joined.Status().abilities[0].executing && joined.Status().abilities[0].waiting,
        "ability registration is not observable");
    joined.Advance(0.7,2);
    Check(joined.Drained() && host.requests.size()==2 && !joined.Status().abilities[0].executing,
        "registered ability completion did not release the join");

    // IsSkillPerform=false does not suppress execution or cancel its body when
    // the caller proceeds. Producer lifetime and user control are separate.
    auto detached=std::make_shared<TaskSequence>(*plan);
    std::get<TaskAbilityCall>(detached->nodes[3].operation).wait_for_performance=false;
    TaskSequenceExecution tail({51},detached,host.Services());tail.Advance(0.4,3);
    Check(host.requests.size()==4 && tail.Status().phase==TaskExecutionPhase::Draining && !tail.ProducersComplete(),
        "unregistered ability blocked control or was discarded");
    const auto releases=host.releases.size();tail.Advance(0.3,4);
    Check(!tail.Drained() && host.releases.size()==releases,"unregistered producer was released before completing");
    tail.Advance(0.4,5);Check(tail.Drained() && tail.ProducersComplete(),"detached producer never drained");

    // Calling an executing identity is not another copy, and completion of an
    // earlier invocation is not a permanent completed flag for the next one.
    auto repeated=Plan({Submit("hit"),{"wait",TaskWaitSeconds{0.2}},
        {"child",TaskSerial{{0,1}}},{"first",TaskAbilityCall{"same",2,true}},
        {"duplicate",TaskAbilityCall{"same",2,true}},{"join",TaskWaitAbilities{}},
        {"again",TaskAbilityCall{"same",2,false}},{"join-again",TaskWaitAbilities{}},
        {"entry",TaskSerial{{3,4,5,6,7}}}},8);
    const auto before=host.requests.size();TaskSequenceExecution reentry({52},repeated,host.Services());reentry.Advance(0.1,6);
    Check(host.requests.size()==before+1,"active ability was invoked twice");
    reentry.Advance(0.2,7);
    Check(host.requests.size()==before+2 && !reentry.Drained() && reentry.Status().abilities[0].invocation==2 &&
        reentry.Status().abilities[0].waiting,"ability reentry lost registration or reused its earlier completion");
    reentry.Advance(0.2,8);Check(reentry.Drained(),"second invocation remained blocked");

    // A child can register another ability before its own body becomes ready.
    auto nested=Plan({{"child-wait",TaskWaitSeconds{1}}, {"child-call",TaskAbilityCall{"child",0,true}},
        {"parent-call",TaskAbilityCall{"parent",1,false}}, {"join",TaskWaitAbilities{}},
        Submit("after-nested"),{"entry",TaskSerial{{2,3,4}}}},5);
    TaskSequenceExecution children({53},nested,host.Services());children.Advance(0.4,9);
    Check(children.Status().abilities.size()==2 && !children.ProducersComplete(),"nested waiting registration lost");
    children.Advance(0.7,10);Check(children.Drained(),"nested child advanced twice or not at all");

    // Cancellation stops pending children, including those never registered as
    // skill waits. No delayed callback may submit work on a retired actor.
    auto delayed=Plan({{"wait",TaskWaitSeconds{1}},Submit("too-late"),{"child",TaskSerial{{0,1}}},
        {"call",TaskAbilityCall{"tail",2,false}}},3);
    TaskSequenceExecution removed({54},delayed,host.Services());removed.Advance(0.1,11);
    const auto count=host.requests.size();removed.Stop(TaskExitReason::WorldLeft);removed.Advance(9,12);
    Check(removed.Drained() && removed.ProducersComplete() && host.requests.size()==count &&
        !removed.Status().abilities[0].executing,"retired ability continued submitting work");

    // Request resource drain is later than the ability body becoming ready.
    Host draining;draining.immediate=true;draining.drain=false;
    auto quick=Plan({Submit("tail"),{"call",TaskAbilityCall{"quick",0,true}},
        {"join",TaskWaitAbilities{}},{"root",TaskSerial{{1,2}}}},3);
    TaskSequenceExecution resources({55},quick,draining.Services());resources.Advance(0,13);
    Check(resources.ProducersComplete() && !resources.Drained() && !resources.Status().abilities[0].executing,
        "resource tail was counted as an executing ability");
    TaskRequestState done;done.drained=true;resources.Observe(draining.requests[0].receipt,done);
    Check(resources.Drained(),"finished ability lost its resource drain receipt");

    Host scoped;
    std::vector<std::string> names;
    auto ports=scoped.Services();const auto submit=ports.submit;
    ports.submit=[&](auto token,auto id,const auto& input){names.push_back(input.request);return submit(token,id,input);};
    TaskSequenceExecution fresh({57},repeated,ports);fresh.Advance(0.1,1);fresh.Advance(0.2,2);
    Check(fresh.Status().issues.empty() && scoped.requests.size()==2 && names[0]!=names[1],
        "new ability invocation collided with its previous animation receipt");
    fresh.Stop(TaskExitReason::Deleted);

    // A launched child may wait on an animation the parent will submit later.
    // It binds that invocation's receipt, not another ability's same-name slot.
    auto inherited=Plan({{"hit-clock",TaskWaitRequest{"clock",TaskWaitSignal::Progress,0.5}},
        {"hit",TaskAbilityCall{"hit",0,true}},Submit("clock"),{"body-tail",TaskWaitSeconds{0.8}},
        {"parent",TaskSerial{{1,2,3}}},{"entry-call",TaskAbilityCall{"entry",4,true}},
        {"all",TaskWaitAbilities{}},{"root",TaskSerial{{5,6}}}},7);
    TaskSequenceExecution borrowed({58},inherited,scoped.Services());borrowed.Advance(0.1,3);
    Check(borrowed.Status().issues.empty() && !borrowed.ProducersComplete(),"forward parent clock binding failed");
    TaskRequestState progress;progress.progress=0.6;borrowed.Observe(scoped.requests.back().receipt,progress);
    borrowed.Advance(0.8,4);
    Check(borrowed.ProducersComplete() && borrowed.Status().issues.empty(),"child wait used an unrelated request namespace");
}
void AnimationBoundaries() {
    Host host;
    auto body=Submit("body");
    std::get<TaskAnimationCommand>(std::get<TaskSubmit>(body.operation).operation).normalized_end=1.5f;
    auto plan=Plan({body,{"end",TaskWaitRequest{"body",TaskWaitSignal::AnimationEnd}},Submit("notified"),
        {"left",TaskWaitRequest{"body",TaskWaitSignal::StateLeft}},Submit("departed"),
        {"root",TaskSerial{{0,1,2,3,4}}}},5);
    TaskSequenceExecution run({56},plan,host.Services());run.Advance(0,1);
    TaskRequestState clip;clip.performance_complete=true;clip.progress=1.1;
    run.Observe(host.requests[0].receipt,clip);run.Advance(0.1,2);
    Check(host.requests.size()==1 && run.Status().issues.empty(),"clip completion satisfied or failed a separate request End");
    clip.animation_end=true;clip.progress=1.6;
    run.Observe(host.requests[0].receipt,clip);run.Advance(0.1,3);
    Check(host.requests.size()==2 && !run.ProducersComplete(),"request End implied state departure");
    clip.state_left=true;run.Observe(host.requests[0].receipt,clip);run.Advance(0.1,4);
    Check(host.requests.size()==3 && run.Drained(),"observed state departure was not delivered");
    std::get<TaskAnimationCommand>(std::get<TaskSubmit>(plan->nodes[0].operation).operation).normalized_end.reset();
    bool rejected=false;try{ValidateTaskSequence(*plan);}catch(const std::invalid_argument&){rejected=true;}
    Check(rejected,"end wait without an authored End was accepted");
}
}
void TestTaskSequence(){Clocks();Receipts();Loops();TerminalProgress();Stops();SupersededWaits();AbilityInvocations();AnimationBoundaries();}
