#pragma once
#include "ControlValues.hpp"
#include "../appearance/NpcAppearance.hpp"
#include <hksr/control/ActorControlSession.hpp>

namespace hksr::framework {
class CharacterControls {
public:
    explicit CharacterControls(std::unique_ptr<api::ActorControlSession> session);
    api::ControlReceipt Place(api::Position3 position,api::Rotation4 rotation,bool relative,std::optional<bool> snap_to_ground={});
    api::ControlReceipt Look(LookIntent intent);
    // target is resolved from this registry on the same official frame only.
    void Service(const api::WorldFrame& frame,AppearanceServices services,api::GameEntity* target,bool presentation_ready);
    void Cancel();
    api::ControlPhase Suspend() {const auto phase=session_->Suspend();RefreshStatus();return phase;}
    bool Close(const api::WorldFrame& frame);
    const ControlStatus& Status() const {return status_;}
    api::PlayerPlacement PlannedPlacement(api::PlayerPlacement fallback={}) const; // copied pose plus queued UI intent, no game call
private:
    void RefreshStatus();
    std::unique_ptr<api::ActorControlSession> session_;
    ControlStatus status_;
};
}
