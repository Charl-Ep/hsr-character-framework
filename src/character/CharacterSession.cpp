#include <hksr/framework/character/CharacterSession.hpp>
#include <stdexcept>

namespace hksr::framework {
CharacterPlan CharacterPlan::FromPrepared(const PreparedSource& source,
    std::shared_ptr<const ModelAssetInput> payload,std::string look_source) {
    if(!source.finished || !source.character) throw std::invalid_argument("character source preparation has not produced a selection");
    CharacterPlan out; out.package=*source.character; out.payload=std::move(payload);
    out.npc_look_source=std::move(look_source); out.alternate=source.presentation; out.source_notes=source.errors;
    out.source_notes.insert(out.source_notes.end(),source.foundation.notes.begin(),source.foundation.notes.end());
    if(out.package.appearance && !out.package.appearance->character_config.empty())
        out.action_sources.push_back({ActionDomain::Exploration,{api::ActionConfigKind::Adventure,out.package.appearance->character_config}});
    if(out.alternate && source.battle.status==SourceResolution::Ready && source.battle.candidates.size()==1)
        out.action_sources.push_back({ActionDomain::Alternate,{api::ActionConfigKind::Battle,source.battle.candidates.front().character_config}});
    return out;
}
CharacterSession::CharacterSession(api::GameWorld* world,CharacterPlan plan,CharacterSessionServices services)
    :world_(world),plan_(std::move(plan)),services_(std::move(services)),
    character_(world,plan_.package,plan_.payload,plan_.npc_look_source,services_.character,services_.events,plan_.visual_root) {
    // Capture factory-created model/consumer inputs before private Art or
    // supplemental events mutate them. Without an alternate source no capture
    // is needed and none of its APIs become exploration prerequisites.
    if(plan_.alternate) character_.SetExplorationInputsEnabled(false);
}
void CharacterSession::Create(const api::NpcCreateInput& input) {
    if(status_.phase!=SessionPhase::NotCreated || removing_) throw std::logic_error("session creation epoch is single-use");
    character_.Create(input);
    status_.phase=SessionPhase::PreparingBaseline;
    for(const auto& receipt:character_.Creation().entities)
        if(receipt.token.entity==character_.Creation().factory.entity && !receipt.disposed) owner_=receipt.token;
    if(!character_.Creation().create_returned || !owner_.entity || character_.Creation().removal_requested) {
        status_.phase=SessionPhase::Failed; status_.errors.push_back("NPC creation did not produce a live owned result");
        const auto& errors=character_.Creation().errors;status_.errors.insert(status_.errors.end(),errors.begin(),errors.end());
        Remove();
    }
}
std::uint64_t CharacterSession::Queue(ActionDomain domain,ActionRequest action) {
    if(removing_ || status_.closed) throw std::logic_error("session closing");
    desired_=Intent{++status_.selected_action,domain,std::move(action)};
    return status_.selected_action;
}
void CharacterSession::Create(const api::StoryCreateInput& input) {
    if(status_.phase!=SessionPhase::NotCreated || removing_)throw std::logic_error("session creation epoch is single-use");
    character_.Create(input);status_.phase=SessionPhase::PreparingBaseline;
    for(const auto& receipt:character_.Creation().entities)
        if(receipt.token.entity==character_.Creation().factory.entity && !receipt.disposed)owner_=receipt.token;
    if(!character_.Creation().create_returned || !owner_.entity || character_.Creation().removal_requested) {
        status_.phase=SessionPhase::Failed;status_.errors.push_back("independent actor creation did not produce a live owned result");
        const auto& errors=character_.Creation().errors;status_.errors.insert(status_.errors.end(),errors.begin(),errors.end());Remove();
    }
}
void CharacterSession::Remove() { removing_=true; desired_.reset(); if(baseline_)baseline_->Cancel(); }
bool CharacterSession::Ended() const {
    return owner_.entity && (api::TrackedEntities().IsDisposed(owner_) ||
        api::TrackedEntities().Current(owner_.entity).generation!=owner_.generation);
}
bool CharacterSession::CloseBaseline() {
    return !baseline_ || baseline_->Close();
}
void CharacterSession::StartHandoff(std::unique_ptr<api::ModelProfileRequest> request) {
    status_.alternate_available=static_cast<bool>(request);
    handoff_=std::make_unique<ModelActionHandoff>(character_,owner_,world_,std::move(request),services_.handoff,
        DomainControllers{},plan_.alternate && plan_.alternate->initial_state.available?plan_.alternate->initial_state.value:std::string{});
    handoff_->ConfigurePresentation(plan_.presentation_profile,plan_.presentation_phase);
    character_.SetExplorationInputsEnabled(true);
    status_.phase=SessionPhase::Active; status_.waiting.clear();
}
bool CharacterSession::PrepareBaseline(const api::WorldFrame& frame,AppearanceServices appearance,api::AnimationInputTypes types) {
    if(!plan_.alternate) { StartHandoff(nullptr); return true; }
    try {
        if(!baseline_)baseline_=services_.baseline(owner_,world_,plan_.alternate->model);
        baseline_->Service(frame,appearance.assets.objects,appearance.assets.arrays,types);
        const auto& result=baseline_->Status();
        status_.baseline_attempted=result.attempted;status_.waiting=result.waiting;
        if(removing_ || Ended())return false;
        if(result.phase==api::BaselineCapturePhase::Ready)StartHandoff(baseline_->TakeRequest());
        else if(result.phase==api::BaselineCapturePhase::Failed) {
            status_.errors.insert(status_.errors.end(),result.errors.begin(),result.errors.end());
            // Product choice: a failed optional alternate must not remove the
            // independently usable exploration instance.
            StartHandoff(nullptr);
        }
    }catch(const std::exception& e) {
        status_.baseline_attempted=true;status_.errors.push_back(e.what());
        if(!removing_ && !Ended())StartHandoff(nullptr);
    }
    return static_cast<bool>(handoff_);
}
void CharacterSession::Service(const api::WorldFrame& frame,AppearanceServices appearance,api::AnimationInputTypes types) {
    if(busy_ || status_.closed || (status_.phase==SessionPhase::NotCreated && !removing_)) return;
    busy_=true; struct Exit { bool& flag; ~Exit(){flag=false;} } exit{busy_};
    if(Ended() || character_.Creation().removal_requested || character_.Creation().closed) removing_=true;
    if(removing_) {
        status_.phase=SessionPhase::Closing;
        if(handoff_) { handoff_->Remove(); handoff_->Service(frame,appearance,types); }
        else if(character_.Creation().create_started) { character_.Remove(); character_.Service(frame,appearance,types); }
        status_.closed=CloseBaseline() && (!character_.Creation().create_started || character_.Status().closed) && (!handoff_ || handoff_->Status().closed);
        if(status_.closed) status_.phase=SessionPhase::Closed;
        return;
    }
    if(!handoff_) {
        if(frame.world!=world_ || !frame.disposing.read.returned || frame.disposing.value) return;
        if(!PrepareBaseline(frame,appearance,types)) return;
    }
    CloseBaseline();
    if(removing_ || Ended()) return;
    if(desired_ && desired_->serial!=status_.submitted_action) {
        const auto intent=*desired_;
        status_.submitted_action=intent.serial; // failed selection is explicit, not retried every frame
        status_.handoff_request=0;status_.dispatch_error.clear();
        try { status_.handoff_request=handoff_->Queue(intent.domain,intent.action); }
        catch(const std::exception& error) { status_.dispatch_error=error.what();status_.errors.push_back(error.what()); }
    }
    handoff_->Service(frame,appearance,types);
}
}
