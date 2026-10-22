#include <hksr/framework/tasks/TaskPresentation.hpp>
#include <stdexcept>

namespace hksr::framework {
TaskAnimationEndpoint BindTaskAnimation(ActionPlayback& player) {
    return {player.Owner().generation,[&player](const auto& command){return player.QueueAnimation(command);},
        [&player](auto serial){player.ReleaseAnimation(serial);}};
}
TaskSoundEndpoint BindTaskSound(std::uint64_t generation,std::unique_ptr<api::OwnedSoundRequest> request) {
    if(!generation || !request)throw std::invalid_argument("sound endpoint requires owned request");
    auto shared=std::shared_ptr<api::OwnedSoundRequest>(std::move(request));
    return {generation,[shared]{shared->Service();},[shared]{shared->Cancel();},[shared]{return shared->Status();}};
}
struct TaskPresentation::State {
    struct Request {
        TaskSubmission value;
        TaskLeafOperation operation;
        std::optional<TaskAnimationEndpoint> endpoint;
        std::shared_ptr<const AnimationRequestStatus> animation;
        std::shared_ptr<const VisualInstructionStatus> visual;
        std::optional<TaskSoundEndpoint> sound;
        bool released=false,finishing=false,submitting=false;
    };
    TaskExecutionToken token;
    std::uint64_t owner=0,scope=0;
    std::shared_ptr<VisualProgramPlayer> visual;
    TaskPresentationServices services;
    std::unique_ptr<TaskSequenceExecution> execution;
    std::map<std::uint64_t,std::shared_ptr<Request>> requests;
    bool servicing=false,forgotten=false,cancel_scope=false;
    TaskSubmission Submit(TaskExecutionToken actual,std::uint64_t serial,const TaskSubmit& input) {
        auto request=std::make_shared<Request>();request->operation=input.operation;
        const auto* animation=std::get_if<TaskAnimationCommand>(&input.operation);
        const auto* sound=std::get_if<TaskSoundCommand>(&input.operation);
        request->value.receipt={actual,serial,0,animation?animation->participant:sound?sound->participant:TaskParticipant{"visual"},animation || sound?0:owner};
        requests.emplace(serial,request);return request->value;
    }
    void Release(const TaskRequestReceipt& receipt,TaskExitReason reason) {
        const auto found=requests.find(receipt.request);if(found==requests.end())return;
        const auto request=found->second;
        if(request->released)return;
        if(std::holds_alternative<TaskSoundCommand>(request->operation)) {
            if(reason==TaskExitReason::Completed && (!request->sound || !request->sound->status().closed)) {
                request->finishing=true;return; // natural audio tail, not queue completion
            }
            request->released=true;
            if(request->sound) {
                if(reason!=TaskExitReason::Completed)request->sound->cancel();
                request->value.state.drained=request->sound->status().closed;
            } else request->value.state.drained=!request->submitting;
            return;
        }
        if(reason==TaskExitReason::Completed && !request->value.state.request_complete) {
            request->finishing=true;return; // a queued command must still be delivered
        }
        request->released=true;
        if(request->animation)request->endpoint->release(request->animation->serial);
        if(request->visual)visual->CancelInstruction(scope,request->visual);
        request->value.state.drained=true;
    }
    void Observe(const std::shared_ptr<Request>& request) {
        auto& value=request->value;
        if(request->animation && !value.state.superseded) {
            const auto& state=*request->animation;
            value.receipt.model=state.model_generation;
            value.state.submitted=state.accepted;value.state.request_complete=state.accepted || state.released;
            value.state.performance_complete=state.complete;value.state.progress=state.progress;
            value.state.drained=state.released;
            if(!request->released)value.state.error=state.error;
        } else if(request->sound) {
            request->sound->service();const auto state=request->sound->status();
            value.state.submitted=state.accepted;value.state.request_complete=state.accepted;
            value.state.started=state.started;value.state.performance_complete=state.ended && state.error.empty();
            value.state.drained=state.closed;
            if(!state.error.empty())value.state.error=state.error;
            else if(state.cancelled && !request->released)value.state.error="sound source retired";
        } else if(request->visual) {
            const auto& state=*request->visual;
            value.state.submitted=state.complete;value.state.request_complete=state.complete || state.cancelled;
            value.state.performance_complete=state.complete;
            // Resources belong to the sequence scope, not to each instruction.
            // Keep that scope until its effects, partners and Timelines drain.
            value.state.drained=state.complete || state.cancelled;
            if(!state.issues.empty())value.state.error=state.issues.front();
            else if(state.cancelled && !request->released)value.state.error="visual instruction cancelled";
        }
        if(request->released && !request->sound && !request->submitting)value.state.drained=true;
        if(request->finishing && (request->sound?value.state.drained:value.state.request_complete))Release(value.receipt,TaskExitReason::Completed);
        execution->Observe(value.receipt,value.state);
        // An error observation can synchronously cancel this request through
        // release. Publish that drain before dropping our adapter record.
        if(request->released && value.state.drained) {
            TaskRequestState drained;drained.drained=true;execution->Observe(value.receipt,drained);
        }
    }
    void Supersede(const std::shared_ptr<Request>& request) {
        // Freeze the last actual observation before releasing its observer.
        // Release does not write Animator state or claim a missing final cue.
        request->value.state.superseded=!request->value.state.performance_complete;
        request->released=true;
        if(request->animation)request->endpoint->release(request->animation->serial);
        request->value.state.drained=true;
        execution->Observe(request->value.receipt,request->value.state);
    }
    bool ClaimAnimationChannel(const std::shared_ptr<Request>& request,const TaskAnimationCommand& command) {
        std::vector<std::shared_ptr<Request>> prior;
        for(const auto& [id,other]:requests) {
            if(other==request || other->released)continue;
            const auto* animation=std::get_if<TaskAnimationCommand>(&other->operation);
            if(!animation || animation->participant.name!=command.participant.name || animation->layer!=command.layer)continue;
            if(id>request->value.receipt.request){Supersede(request);return false;}
            prior.push_back(other);
        }
        for(const auto& other:prior) {
            Observe(other); // external errors still fail; they are never relabelled
            if(request->released)return false;
            if(!other->released)Supersede(other);
            if(request->released)return false;
        }
        return true;
    }
    void Pump() {
        if(const auto error=visual->SequenceError(scope);!error.empty()) {
            cancel_scope=true;execution->Fail("visual scope: "+error);
        }
        std::vector<std::shared_ptr<Request>> snapshot;
        for(const auto& [id,request]:requests)snapshot.push_back(request);
        for(const auto& request:snapshot) {
            try {
                if(!request->released && !request->animation && !request->visual && !request->sound && request->value.state.error.empty()) {
                    if(const auto* command=std::get_if<TaskAnimationCommand>(&request->operation)) {
                        const bool partner=command->participant.name.compare(0,8,"partner:")==0;
                        if(!partner && !services.animation)throw std::runtime_error("task animation resolver unavailable");
                        auto endpoint=partner?visual->PartnerAnimation(command->participant.name.substr(8)):services.animation(command->participant);
                        if(request->released)continue;
                        if(endpoint) {
                            if(!endpoint->entity_generation || !endpoint->submit || !endpoint->release)
                                throw std::runtime_error("incomplete owned animation endpoint");
                            request->endpoint=std::move(endpoint);
                            request->value.receipt.entity_generation=request->endpoint->entity_generation;
                            if(!ClaimAnimationChannel(request,*command))continue;
                            request->animation=request->endpoint->submit({command->state,command->layer,command->transition});
                            if(!request->animation)throw std::runtime_error("animation endpoint returned no receipt");
                            if(request->animation->entity_generation!=request->value.receipt.entity_generation)
                                throw std::runtime_error("animation endpoint returned another entity generation");
                            if(request->released)request->endpoint->release(request->animation->serial);
                        }
                    } else if(const auto* sound=std::get_if<TaskSoundCommand>(&request->operation)) {
                        if(!services.sound)throw std::runtime_error("task sound service unavailable");
                        request->submitting=true;
                        try{request->sound=services.sound(*sound);}
                        catch(...){request->submitting=false;throw;}
                        request->submitting=false;
                        if(request->sound) {
                            if(!request->sound->entity_generation || !request->sound->service || !request->sound->cancel || !request->sound->status)
                                throw std::runtime_error("incomplete owned sound endpoint");
                            request->value.receipt.entity_generation=request->sound->entity_generation;
                            if(request->released)request->sound->cancel();
                        }
                    } else request->visual=visual->SubmitInstruction(scope,std::get<VisualInstruction>(request->operation));
                }
            }catch(const std::exception& error){request->value.state.error=error.what();}
            Observe(request);
        }
        for(auto it=requests.begin();it!=requests.end();) {
            if(it->second->released && it->second->value.state.drained)it=requests.erase(it);else ++it;
        }
    }
    void EndScope() {
        if(forgotten)return;
        const bool natural=execution->Status().reason==TaskExitReason::Completed && !cancel_scope;
        bool submitted=true;
        for(const auto& [id,request]:requests)submitted=submitted && (request->released || request->value.state.request_complete);
        if((execution->Status().phase==TaskExecutionPhase::Draining || execution->Status().phase==TaskExecutionPhase::Finished) && submitted)
            visual->EndSequence(scope,natural);
        if(execution->Drained() && visual->SequenceDrained(scope)) {
            visual->ForgetSequence(scope);forgotten=true;
        }
    }
};
TaskPresentation::TaskPresentation(TaskExecutionToken token,std::uint64_t owner,std::shared_ptr<const TaskSequence> plan,
    std::shared_ptr<VisualProgramPlayer> visual,VisualProgramParameters inputs,TaskPresentationServices services,bool captured) {
    if(!token.value || !owner || !visual || !plan)throw std::invalid_argument("task presentation requires owned actor and visual scene");
    ValidateTaskSequence(*plan);
    auto value=std::make_unique<State>();value->token=token;value->owner=owner;value->visual=std::move(visual);value->services=std::move(services);
    auto* state=value.get();TaskSequenceServices calls;
    calls.submit=[state](auto token,auto serial,const auto& node){return state->Submit(token,serial,node);};
    calls.release=[state](const auto& receipt,auto reason){state->Release(receipt,reason);};
    calls.condition=value->services.condition;calls.yield=value->services.yield;
    value->execution=std::make_unique<TaskSequenceExecution>(token,std::move(plan),std::move(calls));
    value->scope=value->visual->BeginSequence(std::move(inputs),captured);state_=std::move(value);
}
TaskPresentation::~TaskPresentation()=default;
void TaskPresentation::Service(double seconds,std::uint64_t frame) {
    auto& state=*state_;if(state.servicing)return;
    state.servicing=true;struct Guard{bool& value;~Guard(){value=false;}} guard{state.servicing};
    if(state.visual->Status().closing)state.execution->Stop(TaskExitReason::ActorLost);
    state.Pump();state.EndScope();state.visual->Service();state.Pump();
    state.execution->Advance(seconds,frame);state.EndScope();
    // Queue this boundary's leaf work without advancing a second clock tick.
    state.Pump();state.visual->Service();state.Pump();state.EndScope();
}
void TaskPresentation::Stop(TaskExitReason reason){state_->execution->Stop(reason);state_->cancel_scope=true;state_->EndScope();}
void TaskPresentation::RequestFinish(){state_->execution->RequestFinish();}
const TaskExecutionStatus& TaskPresentation::Status() const{return state_->execution->Status();}
std::vector<TaskRequestSnapshot> TaskPresentation::Requests() const{return state_->execution->Requests();}
bool TaskPresentation::Drained() const{return state_->execution->Drained() && state_->forgotten;}
bool TaskPresentation::YieldedControl() const {
    const auto phase=Status().phase;
    if(phase!=TaskExecutionPhase::Draining && phase!=TaskExecutionPhase::Finished)return false;
    for(const auto& [id,request]:state_->requests)
        if(!request->released && !request->value.state.request_complete)return false;
    return true;
}
}
