#include <hksr/framework/presentation/ActionPlayback.hpp>
#include <stdexcept>
#include <utility>
#include <cmath>

namespace hksr::framework {
namespace {
std::unique_ptr<api::ActorAnimationSession> RequireSession(api::EntityToken owner,
    std::unique_ptr<api::ActorAnimationSession> session) {
    if(!session || session->Owner().entity!=owner.entity || session->Owner().generation!=owner.generation)
        throw std::invalid_argument("action session belongs to another entity epoch");
    return session;
}
}
ActionPlayback::ActionPlayback(api::EntityToken owner,api::GameWorld* world,std::unique_ptr<api::ActorAnimationSession> engine,
    std::shared_ptr<const api::PresentationEffects> effects,const api::ComponentConfig* text)
    :owner_(owner),world_(world),engine_(RequireSession(owner,std::move(engine))),entities_(engine_->Entities()),models_(engine_->Models()),
    actions_(engine_->Control()),inputs_(engine_->Inputs()),resources_(engine_->Resources()),text_(std::move(text)),effect_services_(std::move(effects)) {
    if(!owner.entity || !owner.generation || !world) throw std::invalid_argument("action playback requires its owned entity epoch");
}
std::uint64_t ActionPlayback::Queue(ActionRequest request) {
    if(status_.closing) throw std::logic_error("action playback already closing");
    ++intent_revision_;
    completion_.reset();status_.completion_pending=false;status_.completion_note.clear();
    status_.successor.reset();
    ActionReceipt value; value.serial=++status_.latest_serial; value.request=std::move(request);
    pending_=std::move(value); status_.pending=true;
    return status_.latest_serial;
}
void ActionPlayback::CancelPending(bool interrupt_presentation) {
    ++intent_revision_;
    if(interrupt_presentation){presentation_.Interrupt();LeaveProgram();SyncPresentation();}
    visual_cancelled_=true;
    ready_cancelled_=true;
    retire_action_events_=true;
    completion_.reset();status_.completion_pending=false;
    status_.successor.reset();
    if(pending_) { pending_->outcome=ActionOutcome::Cancelled; status_.last=std::move(pending_); pending_.reset(); }
    status_.pending=false;
}
bool ActionPlayback::Ended() const {
    return api::TrackedEntities().IsDisposed(owner_) || api::TrackedEntities().Current(owner_.entity).generation!=owner_.generation;
}
bool ActionPlayback::OwnFrame(const api::WorldFrame& frame) const {
    return frame.world==world_ && frame.disposing.read.returned && !frame.disposing.value;
}
void ActionPlayback::Complete(const api::WorldFrame& frame,api::ManagedObjects& objects) {
    if(!completion_ || frame.sequence<=completion_->boundary) return;
    const auto revision=intent_revision_;
    const auto tracking=*completion_;
    try {
        const auto component=resources_.Component(api::AnimationComponent::Model,objects);
        if(revision!=intent_revision_ || Ended())return;
        if(!component.call.returned || !component.object) throw std::runtime_error("completion model unavailable");
        const auto model=models_.Read(static_cast<api::CharacterModelComponent*>(component.object));
        if(revision!=intent_revision_ || Ended())return;
        if(!model.animator.read.returned || !model.changing.read.returned) throw std::runtime_error("completion Animator unavailable");
        if(model.changing.value || model.animator.value!=tracking.animator) {completion_.reset();status_.completion_note="model changed; old completion cancelled";return;}
        const auto current=actions_.CurrentState(model.animator.value,tracking.layer);
        if(revision!=intent_revision_ || Ended())return;
        const auto transition=actions_.InTransition(model.animator.value,tracking.layer);
        if(revision!=intent_revision_ || !completion_)return;
        if(!current.read.returned || !transition.read.returned) throw std::runtime_error("completion state/transition read failed");
        if(Ended()) {completion_.reset();return;}
        if(current.short_name_hash==completion_->hash || current.full_path_hash==completion_->hash) {
            completion_->observed=true;
            if(presentation_enabled_){presentation_.Observe(current.normalized_time);SyncPresentation();}
        }
        if(transition.value) {
            status_.completion_note="official transition owns successor; observing its destination";
            return;
        }
        if(completion_->following_official || (current.short_name_hash!=completion_->hash && current.full_path_hash!=completion_->hash)) {
            if(completion_->observed) {
                completion_->following_official=true;
                const auto destination=CompletionState();
                const auto idle=actions_.StateHash(resources_.Text(destination,objects));
                if(revision!=intent_revision_ || Ended() || !completion_)return;
                if(idle.read.returned && (current.short_name_hash==idle.value || current.full_path_hash==idle.value)) {
                    status_.successor=AutomaticSuccessor{tracking.serial,0,destination,false,true};
                    status_.completion_note="official controller returned to configured battle initial state";
                } else if(!current.loop && (!std::isfinite(current.normalized_time) || current.normalized_time<1)) {
                    status_.completion_note="official intermediate successor retained; observing destination";
                    return;
                } else if(text_) {
                    const int identity=current.full_path_hash?current.full_path_hash:current.short_name_hash;
                    const auto name=resources_.StateName(model.animator.value,identity,actions_,*text_);
                    if(revision!=intent_revision_ || Ended() || !completion_)return;
                    const auto after=models_.Read(static_cast<api::CharacterModelComponent*>(component.object));
                    if(revision!=intent_revision_ || Ended() || !completion_)return;
                    if(!after.animator.read.returned || !after.changing.read.returned || after.changing.value || after.animator.value!=tracking.animator) {
                        completion_.reset();status_.completion_note="model changed during successor read; old observation cancelled";return;
                    }
                    if(name.read.returned && !name.utf8.empty()) {
                        status_.successor=AutomaticSuccessor{tracking.serial,0,name.utf8,false,true,true};
                        status_.completion_note="official successor resolved from current Animator; no replay";
                    } else status_.completion_note="official successor retained; name unread: "+name.error;
                } else status_.completion_note="official state changed; successor retained; name reader unavailable";
                completion_.reset();presentation_.Abandon();
            }
            return;
        }
        completion_->observed=true;
        if(current.loop) {if(!presentation_.PendingCues())completion_.reset();status_.completion_note="looping state retained";return;}
        const auto threshold=presentation_enabled_?presentation_.CompletionTime():1;
        if(!std::isfinite(current.normalized_time) || current.normalized_time<threshold) return;
        const bool return_idle=!presentation_enabled_ || presentation_.ReturnToIdle();
        if(presentation_enabled_){presentation_.Complete();SyncPresentation();}
        const auto state=CompletionState();
        completion_.reset();
        if(!return_idle){status_.completion_note="authored completion retains final pose";return;}
        const auto next=Queue(FreeStyleState{state});
        status_.successor=AutomaticSuccessor{tracking.serial,next,state,false};
        status_.completion_note="completed non-loop state; returning to current presentation idle";
    } catch(const std::exception& e) {completion_.reset();status_.completion_note=e.what();}
}
void ActionPlayback::Service(const api::WorldFrame& frame,bool ready,api::ManagedObjects& objects) {
    if(servicing_ || status_.closing || status_.closed) return;
    ServiceEffects(frame);
    if(Ended()) {
        if(pending_) { pending_->outcome=ActionOutcome::OwnerEnded; status_.last=std::move(pending_); pending_.reset(); }
        status_.pending=false; return;
    }
    if(!OwnFrame(frame)) return;
    resources_.Drain();
    if(!ready) return;
    servicing_=true;
    struct Exit { bool& flag; ~Exit(){flag=false;} } exit{servicing_};
    TickProgram(frame,objects);
    if(ready_ && !pending_) {
        try {
            const auto component=resources_.Component(api::AnimationComponent::Model,objects);
            if(!component.call.returned || !component.object)throw std::runtime_error("ready model unavailable");
            const auto model=models_.Read(static_cast<api::CharacterModelComponent*>(component.object));
            if(Ended()){ready_.reset();return;}
            if(!model.animator.read.returned || !model.changing.read.returned)throw std::runtime_error("ready model unread");
            if(model.changing.value || model.animator.value!=ready_->animator || ready_cancelled_)ReleaseReady(model.animator.value);
            else {
                const auto style=resources_.Component(api::AnimationComponent::FreeStyle,objects);
                if(!style.call.returned || !style.object)throw std::runtime_error("ready FreeStyle unavailable");
                if(!Ended())TickReady(model.animator.value,style.object,frame,objects);
            }
        }catch(const std::exception& e){status_.ready_note=e.what();ready_cancelled_=true;}
    }
    if(visual_ && !pending_) {
        try {
            const auto component=resources_.Component(api::AnimationComponent::Model,objects);
            if(!component.call.returned || !component.object) throw std::runtime_error("visual model unavailable");
            const auto model=models_.Read(static_cast<api::CharacterModelComponent*>(component.object));
            if(Ended()) {visual_.reset();return;}
            if(!model.animator.read.returned || !model.changing.read.returned) throw std::runtime_error("visual model read failed");
            if(model.changing.value || model.animator.value!=visual_->animator) {
                ReleaseVisual(model.animator.value);status_.visual_note="model changed; visual ownership ended";
            } else if(visual_cancelled_) ReleaseVisual(model.animator.value);
            else {
                const auto style=resources_.Component(api::AnimationComponent::FreeStyle,objects);
                if(!style.call.returned || !style.object)throw std::runtime_error("visual FreeStyle unavailable");
                if(!Ended()) TickVisual(model.animator.value,style.object,frame);
            }
        }catch(const std::exception& e){status_.visual_note=e.what();visual_cancelled_=true;}
    }
    if(!pending_ && completion_) {Complete(frame,objects);status_.completion_pending=completion_.has_value();}
    if(!pending_) return;
    auto receipt=std::move(*pending_); pending_.reset(); status_.pending=false;
    const auto revision=intent_revision_;
    auto wait=[&](const char* reason) { receipt.outcome=ActionOutcome::Waiting; receipt.detail=reason; };
    try {
        resources_.RetainOwner();
        const auto entity=entities_.Read(owner_.entity);
        if(!entity.owner_world.read.returned || entity.owner_world.value!=world_ || !entity.loaded.read.returned)
            throw std::runtime_error("owned entity world/loading observation unavailable");
        if(!entity.loaded.value) wait("entity loading");
        else if(Ended()) receipt.outcome=ActionOutcome::OwnerEnded;
        else {
            void* animator=nullptr;
            const auto* npc=std::get_if<NpcMotion>(&receipt.request);
            const auto* state=std::get_if<AnimatorState>(&receipt.request);
            const auto* free_state=std::get_if<FreeStyleState>(&receipt.request);
            std::optional<FreeStyleState> program_body;
            if(const auto* selected=std::get_if<ProgramAction>(&receipt.request)) {
                if(!program_ || !presentation_enabled_)throw std::runtime_error("visual program unavailable in this action domain");
                program_body=FreeStyleState{program_->ResolveAction(selected->program,selected->action).state};
                free_state=&*program_body;
            }
            const auto* visual=std::get_if<VisualState>(&receipt.request);
            const auto* ready_state=std::get_if<ReadyState>(&receipt.request);
            if(!npc || visual_ || ready_) {
                const auto component=resources_.Component(api::AnimationComponent::Model,objects);
                if(!component.call.returned || !component.object) throw std::runtime_error("own model unavailable");
                const auto model=models_.Read(static_cast<api::CharacterModelComponent*>(component.object));
                if(!model.changing.read.returned || !model.animator.read.returned) throw std::runtime_error("current model/Animator observation unavailable");
                if(model.changing.value || !model.animator.value) wait("model/Animator loading");
                else {
                    animator=model.animator.value;
                    receipt.initialized=inputs_.AnimatorInitialized(animator);
                    if(!receipt.initialized.read.returned) throw std::runtime_error("Animator initialization observation unavailable");
                    if(!receipt.initialized.value) { animator=nullptr; wait("Animator initialization"); }
                }
            }
            if((npc && !visual_ && !ready_) || animator) {
                if(!npc && !visual && !ready_state) {
                    const auto& name=state?state->name:free_state->name;
                    if(name.empty() || name.find('\0')!=std::string::npos) throw std::runtime_error("invalid animation state name");
                    receipt.hash=actions_.StateHash(resources_.Text(name,objects));
                    if(!receipt.hash.read.returned) throw std::runtime_error("official state hash failed");
                    receipt.has_state=actions_.HasState(animator,state?state->layer:0,receipt.hash.value);
                    if(!receipt.has_state.read.returned) throw std::runtime_error("current Animator state query failed");
                    if(!receipt.has_state.value) receipt.outcome=ActionOutcome::StateAbsent;
                }
                if(receipt.outcome!=ActionOutcome::StateAbsent) {
                    void* style=nullptr;
                    if(!state) {
                        const auto component=resources_.Component(api::AnimationComponent::FreeStyle,objects);
                        if(!component.call.returned || !component.object) throw std::runtime_error("own FreeStyle executor unavailable");
                        style=component.object;
                    }
                    // A read can enter managed code; don't write after a
                    // synchronous disposal notification from that code.
                    if(Ended()) receipt.outcome=ActionOutcome::OwnerEnded;
                    else if(status_.closing || revision!=intent_revision_) receipt.outcome=ActionOutcome::Cancelled;
                    else {
                        // Only a body action replaces the lifetime. Selecting a
                        // face layer must not retire body effects.
                        const bool program_ready=PrepareProgram(receipt);
                        if(program_ready && (!state || state->layer==0)) {
                            RetireActionEvents();
                            if(free_state && program_preparing_!=receipt.serial && revision==intent_revision_ && !Ended() && !status_.closing)PrepareRetirements(*free_state);
                        }
                        if(Ended() || status_.closing || revision!=intent_revision_)receipt.outcome=ActionOutcome::Cancelled;
                        else if(program_ready) {
                        if(!ready_state && ready_)ReleaseReady(animator);
                        if(ready_state)StartReady(*ready_state,animator,style,frame,objects,receipt);
                        else if(visual) StartVisual(*visual,animator,style,frame,objects,receipt);
                        else {
                            if(visual_) ReleaseVisual(animator);
                            if(Ended() || status_.closing || revision!=intent_revision_)receipt.outcome=ActionOutcome::Cancelled;
                            else {
                                if(npc) receipt.call=actions_.PlayNpcMotion(style,npc->id);
                                else if(state) receipt.call=actions_.PlayAnimator(animator,receipt.hash.value,state->layer,state->normalized_time);
                                else receipt.call=actions_.PlayFreeStyleState(style,receipt.hash.value,free_state->first,free_state->second);
                                receipt.outcome=receipt.call.returned?ActionOutcome::Returned:ActionOutcome::CallFailed;
                            }
                        }
                        if(receipt.call.returned && presentation_enabled_ && !npc && !visual && (!state || state->layer==0) && revision==intent_revision_ && !Ended()) {
                            if(std::holds_alternative<ProgramAction>(receipt.request))presentation_.Interrupt();
                            else if(ready_state && ready_state->actions && ready_state->pose<ready_state->actions->poses.size())
                                presentation_.Begin(ready_state->actions->poses[ready_state->pose].state);
                            else if(state || free_state)presentation_.Begin(state?state->name:free_state->name);
                            SyncPresentation();
                        }
                        if(receipt.call.returned && revision==intent_revision_ && !Ended() && !status_.closing)CommitProgram(receipt,animator,frame.sequence);
                        if(receipt.call.returned && !npc && !visual && !ready_state && (!state || state->layer==0) && revision==intent_revision_ && !Ended() && !CompletionState().empty() &&
                            !program_playback_ && ((state?state->name:free_state->name)!=CompletionState() || presentation_.PendingCues()) && (!free_state || !free_state->controller_observed)) {
                            completion_=Completion{animator,receipt.hash.value,state?state->layer:0,frame.sequence,false,receipt.serial};
                            status_.completion_pending=true;
                        }
                        }
                    }
                }
            }
        }
    } catch(const std::exception& error) {
        receipt.outcome=Ended()?ActionOutcome::OwnerEnded:
            (status_.closing || revision!=intent_revision_)?ActionOutcome::Cancelled:ActionOutcome::Unavailable;
        receipt.detail=error.what();
    }
    // A callback may have queued a newer intent. Never overwrite it with the
    // command just completed, including a still-waiting older command.
    if(receipt.outcome==ActionOutcome::Waiting) {
        if(!status_.closing && revision==intent_revision_) pending_=receipt;
        else receipt.outcome=ActionOutcome::Cancelled;
    }
    if(program_ && (program_preparing_==receipt.serial || program_return_==receipt.serial) && receipt.outcome!=ActionOutcome::Waiting && receipt.outcome!=ActionOutcome::Returned)LeaveProgram();
    if(status_.successor && status_.successor->serial==receipt.serial)
        status_.successor->returned=receipt.outcome==ActionOutcome::Returned;
    status_.last=std::move(receipt); status_.pending=pending_.has_value();
    resources_.Drain();
}
bool ActionPlayback::Close(const api::WorldFrame& frame) {
    status_.closing=true; CancelPending();
    if(program_)program_->End();
    if(servicing_ || (!Ended() && !OwnFrame(frame))) return false;
    CancelReadyEffect();ServiceEffects(frame);
    // Entity retirement already owns Art teardown. Do not call its old Animator.
    visual_.reset();
    ready_.reset();
    const bool inputs_closed=engine_->Close();
    status_.closed=inputs_closed && effects_.empty() && !action_exit_ && retired_exits_.empty() && (!program_ || program_->Drained());
    return status_.closed;
}
}
