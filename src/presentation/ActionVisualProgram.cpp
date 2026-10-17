#include <hksr/framework/presentation/ActionPlayback.hpp>
#include <cmath>
#include <stdexcept>

namespace hksr::framework {
void ActionPlayback::ConfigureVisualProgram(std::shared_ptr<VisualProgramPlayer> player) {
    if(program_ && program_!=player)throw std::logic_error("close the existing visual program before replacing its services");
    program_=std::move(player);
}
void ActionPlayback::LeaveProgram() {
    program_playback_.reset();program_preparing_=program_intent_=program_return_=0;
    if(program_)program_->Leave();
}
bool ActionPlayback::PrepareProgram(ActionReceipt& receipt) {
    if(!program_ || !presentation_enabled_)return true;
    const auto* direct=std::get_if<AnimatorState>(&receipt.request);
    if(direct && direct->layer!=0)return true;
    if(receipt.serial==program_return_)return true; // StandBy plays before scene teardown
    const auto* style=std::get_if<FreeStyleState>(&receipt.request);
    const auto key=(direct || style)?program_->ActionForState(direct?direct->name:style->name):std::nullopt;
    if(!key){LeaveProgram();program_->Service();return true;}
    if(program_preparing_!=receipt.serial) {
        program_preparing_=receipt.serial;program_playback_.reset();
        program_intent_=program_->Select(*key);
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
    if(receipt.serial==program_return_) {LeaveProgram();program_->Service();return;}
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
            if(tracking.observed) {
                program_playback_->complete=true;
                status_.effect_note="official successor retained; visual scene awaits explicit continuation or exit";
            }
            return;
        }
        program_playback_->observed=true;
        program_->Observe(tracking.action,tracking.model,program_->Status().state,state.normalized_time);
        program_->Service();if(!live())return;
        for(const auto& issue:program_->Status().issues)status_.effect_note=issue;
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
