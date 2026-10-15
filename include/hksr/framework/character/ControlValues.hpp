#pragma once
#include <hksr/control/ActorControlSession.hpp>

namespace hksr::framework {
enum class LookMode { Automatic, Disabled, Player, Instance, Point };
struct LookIntent {LookMode mode=LookMode::Automatic;std::uint64_t target=0;api::Position3 point;float max_distance=0;};
struct ControlSubmissionStatus {std::uint64_t creation=0;api::ControlResult result;};
// Official-thread receipt bridge. No engine calls or resources; publication
// copies Snapshot(). A saved gaze may wait for a new scene incarnation.
class ControlSubmission {
public:
    ControlSubmissionStatus Snapshot() const {
        auto out=status_;if(active_ && !out.result.Terminal())out.result=*active_;return out;
    }
    void Bind(std::uint64_t creation,api::ControlReceipt receipt) {
        status_=Snapshot();if(status_.result.Terminal())return;
        status_.creation=creation;active_=std::move(receipt);
    }
    void Complete(api::ControlPhase phase,std::string detail) {
        status_=Snapshot();active_.reset();if(status_.result.Terminal())return;
        status_.result.phase=phase;status_.result.detail=std::move(detail);
    }
    void Supersede(bool replaced_in_session) {
        // The SDK distinguishes a queued request from one whose synchronous
        // call is in flight. In the latter case retain its actual return.
        if(!replaced_in_session || !active_)Complete(api::ControlPhase::Superseded,"new saved gaze intent");
    }
    void AwaitCreation() {
        status_=Snapshot();active_.reset();if(status_.result.Terminal())return;
        status_.creation=0;status_.result={};status_.result.detail="waiting for saved gaze's active creation";
    }
private:
    ControlSubmissionStatus status_;
    api::ControlReceipt active_;
};
struct ControlStatus {
    api::PlayerPlacement live;
    LookIntent look;
    bool placement_pending=false;
    api::PlacementPhase placement_phase=api::PlacementPhase::Idle;
    api::GazePhase gaze_phase=api::GazePhase::Automatic;
    std::string placement_error,gaze_error;
};
const char* PlacementPhaseName(api::PlacementPhase);
const char* GazePhaseName(api::GazePhase);
}
