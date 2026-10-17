#include <hksr/framework/presentation/ActionPlayback.hpp>
#include <cmath>
#include <stdexcept>

namespace hksr::framework {
void ActionPlayback::ConfigureVisualProgram(std::shared_ptr<VisualProgramPlayer> player) {
    if(program_ && program_!=player)throw std::logic_error("close the existing visual program before replacing its services");
    program_=std::move(player);
}
std::optional<VisualProgramSnapshot> ActionPlayback::CaptureVisualProgram() const {
    if(!program_)return {};
    auto value=program_->Capture();
    if(!value.scene && value.contexts.empty())return {};
    return value;
}
void ActionPlayback::RestoreVisualProgram(VisualProgramSnapshot value) {
    if(!program_)throw std::runtime_error("saved visual program is not configured");
    program_->Restore(std::move(value));
}
void ActionPlayback::LeaveProgram() {
    program_playback_.reset();program_preparing_=program_intent_=program_return_=0;
    if(program_)program_->Leave();
}
bool ActionPlayback::PrepareProgram(ActionReceipt& receipt) {
    if(!program_ || !presentation_enabled_)return true;
    const auto* direct=std::get_if<AnimatorState>(&receipt.request);
    if(direct && direct->layer!=0)return true;
    if(receipt.serial==program_return_)return true; // successor is observed before tail instructions
    if(program_->Restoring()) {
        program_->Service();
        if(!program_->Status().issues.empty())throw std::runtime_error(program_->Status().issues.front());
        if(program_->Restoring()) {
            receipt.outcome=ActionOutcome::Waiting;receipt.detail="visual context restoring";return false;
        }
    }
    const auto* selected=std::get_if<ProgramAction>(&receipt.request);
    if(!selected) {
        program_playback_.reset();program_preparing_=program_intent_=program_return_=0;
        program_->InterruptAction();program_->Service();return true;
    }
    if(program_preparing_!=receipt.serial) {
        program_preparing_=receipt.serial;program_playback_.reset();
        program_intent_=program_->Select(selected->action,selected->choices);
    }
    program_->Service();
    const auto& state=program_->Status();
    status_.effect_note="visual program="+state.action;
    for(const auto& issue:state.issues)status_.effect_note+="; "+issue;
    if(!state.issues.empty())throw std::runtime_error(status_.effect_note);
    if(!state.prepared){receipt.outcome=ActionOutcome::Waiting;receipt.detail="visual scene preparing";return false;}
    return true;
}
void ActionPlayback::CommitProgram(const ActionReceipt& receipt,void* animator,std::uint64_t boundary) {
    if(!program_ || !receipt.call.returned)return;
    if(receipt.serial==program_return_) {
        const auto model=++program_model_;
        if(!program_->SuccessorPlayed(program_intent_,receipt.serial,model))throw std::runtime_error("visual program successor receipt not accepted");
        program_playback_=ProgramPlayback{receipt.serial,program_intent_,model,animator,receipt.hash.value,false,false,boundary,true};
        program_return_=0;return;
    }
    if(receipt.serial!=program_preparing_)return;
    const auto model=++program_model_;
    if(!program_->Played(program_intent_,receipt.serial,model))throw std::runtime_error("visual program playback receipt not accepted");
    program_playback_=ProgramPlayback{receipt.serial,program_intent_,model,animator,receipt.hash.value,false,false,boundary};
}
void ActionPlayback::TickProgram(const api::WorldFrame& frame,api::ManagedObjects& objects) {
    if(!program_ || !program_playback_ || program_playback_->complete || pending_ || frame.sequence<=program_playback_->boundary)return;
    const auto tracking=*program_playback_;const auto revision=intent_revision_;
    auto live=[&]{return !Ended() && !status_.closing && intent_revision_==revision &&
        program_playback_ && program_playback_->action==tracking.action;};
    try {
        const auto component=resources_.Component(api::AnimationComponent::Model,objects);
        if(!live())return;
        if(!component.call.returned || !component.object)throw std::runtime_error("program model unavailable");
        const auto model=models_.Read(static_cast<api::CharacterModelComponent*>(component.object));
        if(!live())return;
        if(!model.animator.read.returned || !model.changing.read.returned)throw std::runtime_error("program model unread");
        if(model.changing.value || model.animator.value!=tracking.animator){LeaveProgram();status_.effect_note="visual program model changed";return;}
        const auto state=actions_.CurrentState(tracking.animator,0);
        if(!live())return;
        if(!state.read.returned)throw std::runtime_error("program animation clock unread");
        if(state.short_name_hash!=tracking.hash && state.full_path_hash!=tracking.hash) {
            const auto* action=program_->CurrentAction();
            if(!tracking.returning && tracking.observed && action && !action->completion_state.empty() && program_->Status().finished) {
                const auto destination=action->completion_state;
                const auto hash=actions_.StateHash(resources_.Text(destination,objects));
                if(!live())return;
                if(!hash.read.returned)throw std::runtime_error("program successor hash unread");
                if(state.short_name_hash==hash.value || state.full_path_hash==hash.value) {
                    if(!program_->SuccessorPlayed(tracking.intent,tracking.action,tracking.model))throw std::runtime_error("official program successor not accepted");
                    program_playback_->returning=true;program_playback_->hash=hash.value;
                    program_->Observe(tracking.action,tracking.model,destination,state.normalized_time);
                    program_->Service();if(!live())return;
                    program_playback_->complete=program_->Status().completed;
                    status_.successor=AutomaticSuccessor{tracking.action,0,destination,false,true};
                    return;
                }
            }
            if(tracking.observed) {
                if(tracking.returning){LeaveProgram();status_.effect_note="successor left before visual tail finished";return;}
                program_playback_->complete=true;
                status_.effect_note="official successor retained; visual scene awaits explicit continuation or exit";
            }
            return;
        }
        program_playback_->observed=true;
        program_->Observe(tracking.action,tracking.model,program_->Status().state,state.normalized_time);
        program_->Service();if(!live())return;
        for(const auto& issue:program_->Status().issues)status_.effect_note=issue;
        if(tracking.returning) {
            program_playback_->complete=program_->Status().completed;
            return;
        }
        const auto* action=program_->CurrentAction();
        if(!action || !program_->Status().finished || !std::isfinite(state.normalized_time) || state.normalized_time<action->complete_at)return;
        program_playback_->complete=true;
        if(action->completion_state.empty())return; // authored persistent scene
        const auto destination=action->completion_state;
        const auto next=Queue(FreeStyleState{destination});program_return_=next;
        status_.successor=AutomaticSuccessor{tracking.action,next,destination,false};
    } catch(const std::exception& e) {
        status_.effect_note=e.what();LeaveProgram();
    }
}
}
