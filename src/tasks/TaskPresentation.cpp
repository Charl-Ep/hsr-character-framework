#include <hksr/framework/tasks/TaskPresentation.hpp>
#include <stdexcept>

namespace hksr::framework {
TaskAnimationEndpoint BindTaskAnimation(ActionPlayback& player) {
    return {player.Owner().generation,[&player](const auto& command){return player.QueueAnimation(command);},
        [&player](auto serial){player.ReleaseAnimation(serial);},[&player]{return player.QueueSkillRecovery();},
        [&player](auto serial){player.ReleaseSkillRecovery(serial);}};
}
PoseRequestEndpoint BindTaskPose(ActionPlayback& player,PoseResolver resolve) {
    return {player.Owner().generation,[&player,resolve=std::move(resolve)](const auto& selection) {
        if(!resolve)throw std::runtime_error("pose source resolver unavailable");
        const auto command=resolve(selection);
        return command?player.QueuePose(*command):std::shared_ptr<const AnimationRequestStatus>{};
    },[&player](auto serial,bool cancel){player.ReleasePose(serial,cancel);}};
}
TaskSoundEndpoint BindTaskSound(std::uint64_t generation,std::unique_ptr<api::OwnedSoundRequest> request) {
    if(!generation || !request)throw std::invalid_argument("sound endpoint requires owned request");
    auto shared=std::shared_ptr<api::OwnedSoundRequest>(std::move(request));
    return {generation,[shared]{shared->Service();},[shared]{shared->Cancel();},[shared]{return shared->Status();}};
}
struct TaskPresentation::State {
    struct Request {
        TaskSubmission value;
        std::string name;
        TaskLeafOperation operation;
        std::shared_ptr<Request> animation_dependency;
        std::optional<TaskAnimationEndpoint> endpoint;
        std::optional<PoseRequestEndpoint> pose_endpoint;
        std::optional<HitDelivery> hit;
        std::optional<SpatialHitEndpoint> point_hit;
        std::shared_ptr<const AnimationRequestStatus> animation;
        std::shared_ptr<const VisualInstructionStatus> visual;
        std::optional<TaskSoundEndpoint> sound;
        std::optional<MovementRequestEndpoint> movement;
        bool released=false,finishing=false,submitting=false;
    };
    TaskExecutionToken token;
    std::uint64_t owner=0,scope=0;
    std::shared_ptr<VisualProgramPlayer> visual;
    TaskPresentationServices services;
    std::unique_ptr<TaskSequenceExecution> execution;
    std::map<std::uint64_t,std::shared_ptr<Request>> requests;
    bool servicing=false,forgotten=false,cancel_scope=false,started=false;
    TaskSubmission Submit(TaskExecutionToken actual,std::uint64_t serial,const TaskSubmit& input) {
        auto request=std::make_shared<Request>();request->operation=input.operation;request->name=input.request;
        if(const auto* control=std::get_if<TaskControlCommand>(&input.operation)) {
            if(!services.control)throw std::logic_error("task control owner is unavailable");
            services.control(control->changes);
            request->value.receipt={actual,serial,0,{"control"},owner};
            request->value.state.submitted=request->value.state.request_complete=request->value.state.performance_complete=true;
            request->value.state.drained=true;
            request->released=true;
            requests.emplace(serial,request);return request->value;
        }
        const auto* animation=std::get_if<TaskAnimationCommand>(&input.operation);
        const auto* sound=std::get_if<TaskSoundCommand>(&input.operation);
        const auto* movement=std::get_if<TaskMovementCommand>(&input.operation);
        const auto* hit=std::get_if<TaskHitCommand>(&input.operation);
        const bool pose=std::holds_alternative<TaskPoseCommand>(input.operation);
        const bool recovery=std::holds_alternative<TaskSkillRecovery>(input.operation);
        request->value.receipt={actual,serial,0,animation?animation->participant:sound?sound->participant:TaskParticipant{movement || pose || recovery?"body":"visual"},animation || sound || movement || pose || recovery?0:owner};
        if(hit){request->value.receipt.participant={"target:"+hit->target};request->value.receipt.entity_generation=0;}
        if(movement) {
            for(const auto& [id,prior]:requests)if(id<serial && prior->name==movement->animation)request->animation_dependency=prior;
            if(!request->animation_dependency)request->value.state.error="movement animation request has not been submitted";
        }
        requests.emplace(serial,request);return request->value;
    }
    void Release(const TaskRequestReceipt& receipt,TaskExitReason reason) {
        const auto found=requests.find(receipt.request);if(found==requests.end())return;
        const auto request=found->second;
        if(request->released)return;
        if(std::holds_alternative<TaskSkillRecovery>(request->operation)) {
            if(reason==TaskExitReason::Completed && (!request->animation || !request->animation->complete)) {
                request->finishing=true;return;
            }
            request->released=true;
            if(request->animation)request->endpoint->release_recovery(request->animation->serial);
            request->value.state.drained=true;return;
        }
        if(std::holds_alternative<TaskHitCommand>(request->operation)) {
            if(reason==TaskExitReason::Completed && (request->point_hit?!request->point_hit->status->closed:!request->value.state.request_complete)) {
                request->finishing=true;return;
            }
            request->released=true;
            if(request->hit)request->hit->cancel(); // delivered hits remain receiver-owned
            if(request->point_hit && reason!=TaskExitReason::Completed)request->point_hit->cancel();
            request->value.state.drained=request->point_hit?request->point_hit->status->closed:!request->submitting;
            return;
        }
        if(reason==TaskExitReason::Completed && std::holds_alternative<TaskAnimationCommand>(request->operation)) {
            for(const auto& [id,dependent]:requests)if(dependent->animation_dependency==request && !dependent->value.state.drained) {
                request->finishing=true;return; // retain the bound animation through motion drain
            }
            const auto& animation=std::get<TaskAnimationCommand>(request->operation);
            if(animation.track && animation.participant.name=="body" && animation.layer==0)
                for(const auto& [id,dependent]:requests)
                    if(std::holds_alternative<TaskSkillRecovery>(dependent->operation) && !dependent->released &&
                        !dependent->value.state.performance_complete) {
                        request->finishing=true;return; // the queued return still consumes this record's End
                    }
        }
        if(std::holds_alternative<TaskMovementCommand>(request->operation)) {
            if(reason==TaskExitReason::Completed && (!request->movement || !request->movement->status().closed)) {
                request->finishing=true;return;
            }
            request->released=true;
            if(reason==TaskExitReason::Replaced || reason==TaskExitReason::Interrupted)
                request->value.state.superseded=!request->value.state.performance_complete;
            if(request->movement) {
                if(reason!=TaskExitReason::Completed)request->movement->cancel();
                request->value.state.drained=request->movement->status().closed;
            }else request->value.state.drained=!request->submitting;
            return;
        }
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
        if(request->animation) {
            if(request->pose_endpoint)request->pose_endpoint->release(request->animation->serial,reason!=TaskExitReason::Completed && reason!=TaskExitReason::Replaced);
            else request->endpoint->release(request->animation->serial);
        }
        if(request->visual)visual->CancelInstruction(scope,request->visual);
        request->value.state.drained=true;
    }
    void Observe(const std::shared_ptr<Request>& request) {
        auto& value=request->value;
        if(request->point_hit) {
            const auto& state=*request->point_hit->status;
            value.state.submitted=state.admitted;
            value.state.request_complete=state.admitted || state.cancelled || !state.error.empty();
            value.state.performance_complete=state.admitted;
            value.state.started=state.submitted;
            value.state.drained=state.closed;
            if(!request->released)value.state.error=state.error;
        } else if(request->hit) {
            const auto& state=*request->hit->status;
            value.receipt.entity_generation=state.entity_generation;
            value.state.submitted=state.delivered;
            value.state.request_complete=state.delivered || state.cancelled || !state.error.empty();
            // This operation dispatches an impact; it does not own or wait for
            // the receiving actor's reaction or logical return.
            value.state.performance_complete=state.delivered;
            value.state.started=state.delivered && (!state.ignored || !state.effects_ignored);
            value.state.drained=request->released;
            if(!request->released) {
                value.state.error=state.error;
                if(state.cancelled && state.error.empty())value.state.error="hit delivery cancelled before admission";
            }
        } else if(request->animation && !value.state.superseded) {
            const auto& state=*request->animation;
            value.receipt.model=state.model_generation;
            value.state.submitted=state.accepted;value.state.request_complete=state.accepted || state.released || state.request_finished;
            value.state.performance_complete=state.complete;value.state.progress=state.progress;
            value.state.started=state.entered;
            value.state.animation_end=state.end_reached;value.state.state_left=state.state_left;
            value.state.animation_finished=state.request_finished;
            value.state.superseded=state.advanced;
            value.state.drained=state.released;
            if(!request->released)value.state.error=state.error;
        } else if(request->movement) {
            request->movement->service();const auto state=request->movement->status();
            value.state.submitted=state.accepted;value.state.request_complete=state.accepted;
            value.state.started=state.started;value.state.performance_complete=state.completed && state.error.empty();
            value.state.progress=state.movement_time;value.state.drained=state.closed;
            if(!state.error.empty())value.state.error=state.error;
            else if(state.cancelled && !request->released)value.state.error="movement source retired";
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
            value.state.started=state.complete && state.issues.empty() && !state.cancelled;
            // Resources belong to the sequence scope, not to each instruction.
            // Keep that scope until its effects, partners and Timelines drain.
            value.state.drained=state.complete || state.cancelled;
            if(!state.issues.empty())value.state.error=state.issues.front();
            else if(state.cancelled && !request->released)value.state.error="visual instruction cancelled";
        }
        if(request->released && !request->sound && !request->movement && !request->point_hit && !request->submitting)value.state.drained=true;
        if(request->finishing && (std::holds_alternative<TaskSkillRecovery>(request->operation)?value.state.performance_complete:
            request->sound || request->movement || request->point_hit?value.state.drained:value.state.request_complete))Release(value.receipt,TaskExitReason::Completed);
        started=started || value.state.started;
        if(!execution->Observe(value.receipt,value.state) && !request->released)
            execution->Fail("request observation identity changed: "+request->name);
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
        if(request->animation) {
            if(request->pose_endpoint)request->pose_endpoint->release(request->animation->serial,false);
            else request->endpoint->release(request->animation->serial);
        }
        request->value.state.drained=true;
        execution->Observe(request->value.receipt,request->value.state);
    }
    bool ClaimAnimationChannel(const std::shared_ptr<Request>& request,const TaskAnimationCommand& command) {
        std::vector<std::shared_ptr<Request>> motions;
        for(const auto& [id,move]:requests)if((!command.track || command.track->force) && id<request->value.receipt.request && move->animation_dependency) {
            const auto* animation=std::get_if<TaskAnimationCommand>(&move->animation_dependency->operation);
            if(animation && animation->participant.name==command.participant.name && animation->layer==command.layer)motions.push_back(move);
        }
        bool drained=true;
        for(const auto& move:motions){Release(move->value.receipt,TaskExitReason::Replaced);Observe(move);drained=drained && move->value.state.drained;}
        if(!drained || request->released)return false;
        std::vector<std::shared_ptr<Request>> prior;
        for(const auto& [id,other]:requests) {
            if(other==request || other->released)continue;
            const auto* animation=std::get_if<TaskAnimationCommand>(&other->operation);
            const bool pose=std::holds_alternative<TaskPoseCommand>(other->operation);
            if(pose?(command.participant.name!="body" || command.layer!=0):
                (!animation || animation->participant.name!=command.participant.name || animation->layer!=command.layer))continue;
            if(command.track && animation && animation->track)continue;
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
    bool ClaimMovementChannel(const std::shared_ptr<Request>& request) {
        bool ready=true;
        std::vector<std::shared_ptr<Request>> previous;
        for(const auto& [id,other]:requests)if(id<request->value.receipt.request && std::holds_alternative<TaskMovementCommand>(other->operation))previous.push_back(other);
        for(const auto& other:previous) {
            if(!other->released)Release(other->value.receipt,TaskExitReason::Interrupted);
            Observe(other);ready=ready && other->value.state.drained;
        }
        return ready && !request->released;
    }
    void Pump() {
        if(const auto error=visual->SequenceError(scope);!error.empty()) {
            cancel_scope=true;execution->Fail("visual scope: "+error);
        }
        std::vector<std::shared_ptr<Request>> snapshot;
        for(const auto& [id,request]:requests)snapshot.push_back(request);
        for(const auto& request:snapshot) {
            try {
                if(!request->released && request->animation_dependency && request->animation_dependency->animation &&
                    request->animation_dependency->animation->advanced)
                    Release(request->value.receipt,TaskExitReason::Replaced);
                if(!request->released && !request->animation && !request->visual && !request->sound && !request->movement && !request->hit && !request->point_hit && request->value.state.error.empty()) {
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
                            request->animation=request->endpoint->submit({command->state,command->layer,command->transition,command->normalized_start,command->normalized_end,command->track});
                            if(!request->animation)throw std::runtime_error("animation endpoint returned no receipt");
                            if(request->animation->entity_generation!=request->value.receipt.entity_generation)
                                throw std::runtime_error("animation endpoint returned another entity generation");
                            if(request->released)request->endpoint->release(request->animation->serial);
                        }
                    } else if(std::holds_alternative<TaskSkillRecovery>(request->operation)) {
                        if(!services.animation)throw std::runtime_error("skill recovery body resolver unavailable");
                        auto endpoint=services.animation({"body"});if(request->released)continue;
                        if(endpoint) {
                            if(endpoint->entity_generation!=owner || !endpoint->recover || !endpoint->release_recovery)
                                throw std::runtime_error("owned skill recovery endpoint unavailable");
                            request->endpoint=std::move(endpoint);request->value.receipt.participant={"body"};
                            request->value.receipt.entity_generation=owner;
                            request->animation=request->endpoint->recover();
                            if(!request->animation || request->animation->entity_generation!=owner)
                                throw std::runtime_error("skill recovery returned a foreign or absent receipt");
                            if(request->released)request->endpoint->release_recovery(request->animation->serial);
                        }
                    } else if(const auto* hit=std::get_if<TaskHitCommand>(&request->operation)) {
                        request->submitting=true;
                        try {
                            if(hit->allow_position)request->point_hit=visual->SendSequencePointHit(scope,hit->target,hit->hit);
                            if(!request->point_hit)request->hit=visual->SendSequenceHit(scope,hit->target,hit->hit);
                        }
                        catch(...){request->submitting=false;throw;}
                        request->submitting=false;
                        if(request->point_hit) {
                            if(!request->point_hit->status || !request->point_hit->cancel)throw std::runtime_error("incomplete position hit delivery");
                            request->value.receipt.entity_generation=owner; // sender-owned effect; no receiver
                            if(request->released)request->point_hit->cancel();
                        }
                        if(request->hit) {
                            if(!request->hit->status || !request->hit->status->entity_generation || !request->hit->cancel)
                                throw std::runtime_error("incomplete owned hit delivery");
                            request->value.receipt.entity_generation=request->hit->status->entity_generation;
                            if(request->released)request->hit->cancel();
                        }
                    } else if(const auto* pose=std::get_if<TaskPoseCommand>(&request->operation)) {
                        if(!services.pose || services.pose->entity_generation!=owner || !services.pose->submit || !services.pose->release)
                            throw std::runtime_error("owned pose endpoint unavailable");
                        request->pose_endpoint=services.pose;request->value.receipt.entity_generation=owner;
                        if(!ClaimAnimationChannel(request,TaskAnimationCommand{{"body"}}))continue;
                        request->animation=request->pose_endpoint->submit(pose->selection);
                        if(request->animation && request->animation->entity_generation!=owner)
                            throw std::runtime_error("pose endpoint returned another entity generation");
                        if(request->released && request->animation)request->pose_endpoint->release(request->animation->serial,true);
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
                    } else if(const auto* movement=std::get_if<TaskMovementCommand>(&request->operation)) {
                        const auto dependency=request->animation_dependency;
                        if(!dependency)throw std::runtime_error("movement animation receipt unavailable");
                        if(dependency->value.state.superseded || !dependency->value.state.error.empty())throw std::runtime_error("movement animation request was superseded or failed");
                        if(!dependency->animation || !dependency->animation->entered) {
                            if(dependency->released)throw std::runtime_error("movement animation request ended before entry");
                            continue;
                        }
                        const auto* animation=std::get_if<TaskAnimationCommand>(&dependency->operation);
                        if(!animation || animation->participant.name!="body" || animation->layer!=0)throw std::runtime_error("movement requires a base-layer body animation");
                        if(!ClaimMovementChannel(request))continue;
                        auto motion=movement->movement;motion.state=animation->state;
                        request->submitting=true;
                        try{request->movement=visual->StartSequenceMovement(scope,motion);}
                        catch(...){request->submitting=false;throw;}
                        request->submitting=false;
                        if(request->movement) {
                            if(request->movement->entity_generation!=owner || !request->movement->service || !request->movement->cancel || !request->movement->status)
                                throw std::runtime_error("incomplete or foreign owned movement endpoint");
                            request->value.receipt.entity_generation=request->movement->entity_generation;
                            if(request->released)request->movement->cancel();
                        }
                    } else request->visual=visual->SubmitInstruction(scope,std::get<VisualInstruction>(request->operation));
                }
            }catch(const std::exception& error){request->value.state.error=error.what();}
            Observe(request);
        }
        for(auto it=requests.begin();it!=requests.end();) {
            if(it->second->released && it->second->value.state.drained) {
                // A later provider may cancel an earlier snapshot entry after
                // that entry was already observed. Publish its drain before
                // erasing it; otherwise the sequence keeps a stranded receipt.
                TaskRequestState drained;drained.drained=true;
                execution->Observe(it->second->value.receipt,drained);
                it=requests.erase(it);
            }else ++it;
        }
    }
    void EndScope() {
        if(forgotten)return;
        const bool natural=execution->Status().reason==TaskExitReason::Completed && !cancel_scope;
        bool submitted=true;
        for(const auto& [id,request]:requests)submitted=submitted && (request->released || request->value.state.request_complete);
        if(execution->ProducersComplete() && submitted)
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
bool TaskPresentation::Started() const{return state_->started;}
bool TaskPresentation::YieldedControl() const {
    const auto phase=Status().phase;
    if(phase!=TaskExecutionPhase::Draining && phase!=TaskExecutionPhase::Finished)return false;
    for(const auto& [id,request]:state_->requests)
        if(!request->released && !request->value.state.request_complete)return false;
    return true;
}
}
