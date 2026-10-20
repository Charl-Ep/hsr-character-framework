#include <hksr/framework/presentation/ModelActionHandoff.hpp>
#include <stdexcept>

namespace hksr::framework {
ModelActionHandoff::ModelActionHandoff(CharacterInstance& character,api::EntityToken owner,api::GameWorld* world,
    std::unique_ptr<api::ModelProfileRequest> request,HandoffServices services,DomainControllers controllers,std::string initial_state)
    :character_(character),owner_(owner),world_(world),
    transition_(services.transition(owner,world,std::move(request))),
    controllers_(std::move(controllers)),actions_(owner,world,services.animations(owner),
        services.effects,services.text),initial_state_name_(std::move(initial_state)) {
    if(character.Actions() || character.Creation().factory.entity!=owner.entity)
        throw std::invalid_argument("handoff requires its character and exclusive action consumer");
}
std::uint64_t ModelActionHandoff::Queue(ActionDomain domain,ActionRequest action) {
    if(removing_ || status_.closed) throw std::logic_error("handoff closing");
    if((std::holds_alternative<ProgramAction>(action) || std::holds_alternative<BattleIdle>(action)) && domain!=ActionDomain::Alternate)
        throw std::invalid_argument("visual program requires its battle model domain");
    if(domain==ActionDomain::Alternate && !transition_->RequestStatus())
        throw std::logic_error("alternate source unavailable; exploration remains usable");
    if(tasks_)tasks_->Reset(TaskExitReason::ActorLost);
    desired_=Intent{++status_.requested,domain,std::move(action)};
    if(domain==ActionDomain::Alternate && initial_state_name_.empty())
        status_.initialization_note="battle config has no readable initial state; automatic completion destination unavailable";
    return status_.requested;
}
void ModelActionHandoff::ConfigureTasks(std::shared_ptr<const TaskCatalog> catalog,std::shared_ptr<VisualProgramPlayer> scene) {
    if(tasks_)throw std::logic_error("task catalog already configured");
    if(!catalog || !scene)throw std::invalid_argument("task catalog and scene required");
    TaskPresentationServices services;
    services.animation=[this](const auto& participant)->std::optional<TaskAnimationEndpoint> {
        if(participant.name!="body")throw std::invalid_argument("unknown owned task participant: "+participant.name);
        if(removing_ || Ended())throw std::runtime_error("task actor ended");
        if(status_.phase!=HandoffPhase::Ready || status_.ready_domain!=task_catalog_->domain)return {};
        return BindTaskAnimation(actions_);
    };
    tasks_=std::make_unique<TaskController>(owner_.generation,catalog,scene,std::move(services));
    task_catalog_=std::move(catalog);actions_.ConfigureVisualProgram(std::move(scene));
}
TaskControlResult ModelActionHandoff::ControlTask(TaskControlInput input) {
    if(!tasks_)throw std::logic_error("no task catalog configured for this actor");
    if(removing_ || Ended() || status_.closed)throw std::logic_error("task actor is closing");
    TaskControlResult result;
    if(input.operation==TaskControlOperation::Select) {
        if(!input.selection)throw std::invalid_argument("task selection missing");
        if(!active_ || !transition_->Status().controller_ready || !VisualReady(task_catalog_->domain) ||
            status_.phase!=HandoffPhase::Ready || status_.ready_domain!=task_catalog_->domain ||
            (desired_ && desired_->serial!=status_.servicing))
            throw std::logic_error("task catalog model domain is not ready");
        input.selection->query.participants={{"body"}};
        result.selection=tasks_->Select(std::move(*input.selection));
    } else if(input.operation==TaskControlOperation::Query) {
        if(input.selection){input.selection->query.participants={{"body"}};result.selection=tasks_->Query(*input.selection);}
        else for(const auto& [name,plan]:task_catalog_->tasks) {
            (void)plan;TaskSelection selection{{task_catalog_->id,name}};selection.query.participants={{"body"}};
            result.catalog.emplace(name,tasks_->Query(selection).query);
        }
    } else if(input.operation==TaskControlOperation::Finish)tasks_->RequestFinish();
    else tasks_->Cancel();
    result.status=tasks_->Status();result.contexts=tasks_->Context().Owners();return result;
}
void ModelActionHandoff::Remove() {
    if(tasks_)tasks_->Close(TaskExitReason::Deleted);
    removing_=true;desired_.reset();actions_.CancelPending();transition_->Cancel();
}
bool ModelActionHandoff::Ended() const {
    return api::TrackedEntities().IsDisposed(owner_) || api::TrackedEntities().Current(owner_.entity).generation!=owner_.generation;
}
bool ModelActionHandoff::OwnFrame(const api::WorldFrame& frame) const {
    return frame.world==world_ && frame.disposing.read.returned && !frame.disposing.value;
}
void ModelActionHandoff::UpdateTransitionStatus() {
    const auto& result=transition_->Status();
    status_.phase=result.phase;status_.error=result.error;
    status_.event_value_note=result.event_value_note;status_.initial_state=result.initial_state;
    if(!result.initialization_note.empty())status_.initialization_note=result.initialization_note;
    if(result.phase==HandoffPhase::Failed)Fail(result.error);
}
bool ModelActionHandoff::VisualReady(ActionDomain domain) const {
    if(domain==ActionDomain::Exploration) {
        const auto* visual=character_.AppearanceConsumer();
        if(!visual) return true;
        const auto* active=visual->Active();
        return active && active->Slot().adopted && active->Slot().supply.returned;
    }
    const auto* overlay=character_.AlternatePayload();
    return !overlay || (overlay->Active() && overlay->Active()->published);
}
void ModelActionHandoff::Fail(std::string message) {
    status_.phase=HandoffPhase::Failed;status_.error=std::move(message);status_.ready_domain.reset();
    actions_.CancelPending();
    if(tasks_)tasks_->Reset(TaskExitReason::Failed);
}
void ModelActionHandoff::Finish(const api::WorldFrame& frame,AppearanceServices appearance,api::AnimationInputTypes types) {
    status_.phase=HandoffPhase::Closing;
    actions_.Close(frame);
    if(tasks_) {tasks_->Close(TaskExitReason::ActorLost);tasks_->Service(0,frame.sequence);}
    transition_->Close(frame); // cancel model work before entity retirement
    character_.Remove();character_.Service(frame,appearance,types);
    if(Ended()){transition_->Close(frame);actions_.Close(frame);}
    status_.event_value_note=transition_->Status().event_value_note;
    status_.closed=character_.Status().closed && actions_.Status().closed && transition_->Status().closed && (!tasks_ || tasks_->Drained());
    if(status_.closed)status_.phase=HandoffPhase::Closed;
}
void ModelActionHandoff::Service(const api::WorldFrame& frame,AppearanceServices appearance,api::AnimationInputTypes types) {
    if(busy_ || status_.closed)return;
    busy_=true;struct Exit{bool& flag;~Exit(){flag=false;}} exit{busy_};
    actions_.ServiceEffects(frame);
    if(removing_ || Ended() || character_.Creation().removal_requested || character_.Creation().closed) {
        removing_=true;Finish(frame,appearance,types);return;
    }
    if(!OwnFrame(frame)){character_.Service(frame,appearance,types);return;}
    try {
        if(desired_ && desired_->serial!=status_.servicing) {
            actions_.SetCompletionState(desired_->domain==ActionDomain::Alternate?initial_state_name_:std::string{});
            actions_.SetPresentationDomain(desired_->domain==ActionDomain::Alternate);
            const bool same_pending_domain=active_ && active_->domain==desired_->domain &&
                (status_.phase==HandoffPhase::Loading || status_.phase==HandoffPhase::Adopting ||
                 status_.phase==HandoffPhase::Binding || status_.phase==HandoffPhase::Initializing);
            active_=*desired_;status_.servicing=active_->serial;status_.error.clear();
            status_.playback_serial=0;status_.output.reset();
            if(!same_pending_domain)actions_.CancelPending(false);
            if(same_pending_domain){ /* Preserve the model operation; only replace the desired action. */ }
            else if(status_.phase==HandoffPhase::Ready && status_.ready_domain==active_->domain &&
                transition_->Status().controller_ready)status_.playback_serial=actions_.Queue(active_->action);
            else {
                api::ModelTransitionPlan plan;
                plan.destination=active_->domain==ActionDomain::Exploration?api::ModelDestination::Baseline:api::ModelDestination::Alternate;
                plan.controller=active_->domain==ActionDomain::Exploration?controllers_.exploration:controllers_.alternate;
                // Initial exploration already exists; do not reload it.
                plan.reload=!(status_.phase==HandoffPhase::Ready && status_.ready_domain==active_->domain);
                plan.require_animator=active_->domain==ActionDomain::Alternate || !character_.AppearanceConsumer();
                plan.initialize=active_->domain==ActionDomain::Alternate && !initial_state_name_.empty();
                initial_requested_=false;
                if(plan.reload) {
                    status_.ready_domain.reset();status_.initialization_note.clear();
                    character_.SetExplorationEventsEnabled(false);
                }
                transition_->Select(std::move(plan));UpdateTransitionStatus();
            }
        }
        const auto serial=status_.servicing;
        actions_.ServiceEffects(frame);
        auto interrupted=[&](){return removing_ || Ended() || status_.requested!=serial;};
        api::ModelTransitionConsumer consumer;
        consumer.interrupted=interrupted;
        consumer.detach=[&](){
            character_.SetExplorationInputsEnabled(false);character_.SetAlternatePayloadEnabled(false);
            character_.Service(frame,appearance,types);
            return character_.ExplorationInputsDetached() && character_.AlternatePayloadDetached();
        };
        consumer.adopt=[&](){
            if(active_->domain==ActionDomain::Exploration)character_.SetExplorationInputsEnabled(true);
            else character_.SetAlternatePayloadEnabled(true);
            character_.Service(frame,appearance,types);
            return VisualReady(active_->domain);
        };
        consumer.events_ready=[&](){
            if(active_->domain==ActionDomain::Exploration)character_.SetExplorationEventsEnabled(true);
            character_.Service(frame,appearance,types);
        };
        consumer.initialize=[&](){
            if(!initial_requested_){actions_.Queue(FreeStyleState{actions_.CurrentIdle()});initial_requested_=true;}
            actions_.Service(frame,true,appearance.assets.objects);
            const auto& result=actions_.Status();
            if(result.pending)return api::InitialOutput{};
            if(!result.last)return api::InitialOutput{api::InitialOutputPhase::Failed};
            if(result.last->outcome==ActionOutcome::StateAbsent)return api::InitialOutput{api::InitialOutputPhase::Absent};
            if(result.last->outcome==ActionOutcome::Returned)return api::InitialOutput{api::InitialOutputPhase::Returned,result.last->hash.value};
            return api::InitialOutput{api::InitialOutputPhase::Failed};
        };
        const auto before=transition_->Status().phase;
        transition_->Service(frame,appearance.assets.objects,types,consumer);
        UpdateTransitionStatus();
        if(interrupted())return;
        if(status_.phase==HandoffPhase::Ready) {
            if(visual_restore_) {
                // Receivers and model consumers must exist before effects and
                // phase resources can be reconstructed. Never run this during
                // baseline capture or the model's initialization request.
                if(active_->domain==ActionDomain::Alternate)actions_.RestoreVisualProgram(std::move(*visual_restore_));
                visual_restore_.reset();
            }
            if(before!=HandoffPhase::Ready){status_.ready_domain=active_->domain;status_.playback_serial=actions_.Queue(active_->action);}
            character_.Service(frame,appearance,types);
            if(interrupted())return;
            actions_.Service(frame,status_.ready_domain && VisualReady(*status_.ready_domain),appearance.assets.objects);
            if(tasks_)tasks_->Service(frame.delta_time,frame.sequence);
            const auto& output=actions_.Status().last;
            if(output && status_.playback_serial && output->serial==status_.playback_serial &&
                (!status_.output || status_.output->outcome==ActionOutcome::Waiting))status_.output=output;
        }else if(status_.phase==HandoffPhase::Loading || status_.phase==HandoffPhase::Initializing ||
            status_.phase==HandoffPhase::Failed) {
            character_.Service(frame,appearance,types);
            if(tasks_)tasks_->Service(0,frame.sequence);
        }
    }catch(const std::exception& error){Fail(error.what());}
}
}
