#include <hksr/framework/character/CharacterControls.hpp>
#include <stdexcept>

namespace hksr::framework {
CharacterControls::CharacterControls(std::unique_ptr<api::ActorControlSession> session):session_(std::move(session)) {
    if(!session_)throw std::invalid_argument("character control session is null");
}
api::ControlReceipt CharacterControls::Place(api::Position3 p,api::Rotation4 q,bool relative,std::optional<bool> snap_to_ground) {
    auto receipt=session_->Place(p,q,relative,snap_to_ground);RefreshStatus();return receipt;
}
api::ControlReceipt CharacterControls::Look(LookIntent intent) {
    api::GazeMode mode=api::GazeMode::Automatic;
    switch(intent.mode) {
    case LookMode::Automatic:break;
    case LookMode::Disabled:mode=api::GazeMode::Disabled;break;
    case LookMode::Point:mode=api::GazeMode::Point;break;
    case LookMode::Player:case LookMode::Instance:mode=api::GazeMode::Entity;break;
    }
    auto receipt=session_->Look({mode,intent.point,intent.max_distance});status_.look=intent;return receipt;
}
void CharacterControls::Service(const api::WorldFrame& frame,AppearanceServices s,api::GameEntity* target,bool ready) {
    session_->Service(frame,{s.assets.objects,s.scene,s.visuals},target,ready);RefreshStatus();
}
void CharacterControls::Cancel() {session_->Cancel();RefreshStatus();}
bool CharacterControls::Close(const api::WorldFrame& frame) {const bool done=session_->Close(frame);RefreshStatus();return done;}
api::PlayerPlacement CharacterControls::PlannedPlacement(api::PlayerPlacement fallback) const {return session_->PlannedPlacement(fallback);}
void CharacterControls::RefreshStatus() {
    const auto& value=session_->Status();status_.live=value.live;status_.placement_pending=value.placement_pending;
    status_.placement_phase=value.placement;status_.gaze_phase=value.gaze;
    status_.placement_error=value.placement_error;status_.gaze_error=value.gaze_error;
}
const char* PlacementPhaseName(api::PlacementPhase phase) {
    switch(phase) {
    case api::PlacementPhase::Idle:return "idle";
    case api::PlacementPhase::Waiting:return "waiting";
    case api::PlacementPhase::Applied:return "applied";
    case api::PlacementPhase::Failed:return "failed";
    }return "unknown";
}
const char* GazePhaseName(api::GazePhase phase) {
    switch(phase) {
    case api::GazePhase::Automatic:return "automatic";
    case api::GazePhase::Following:return "following";
    case api::GazePhase::Disabled:return "disabled";
    case api::GazePhase::TargetUnavailable:return "target_unavailable";
    case api::GazePhase::Failed:return "failed";
    case api::GazePhase::OutOfRange:return "out_of_range";
    case api::GazePhase::Restoring:return "restoring";
    }return "unknown";
}
}
