#pragma once
#include <hksr/control/ActorControlSession.hpp>
#include <cstdint>
#include <string>
#include <utility>

namespace hksr::framework {
struct ControlSubmissionStatus {
    std::uint64_t creation = 0;
    api::ControlResult result;
};
// Official-thread receipt bridge. Publication copies Snapshot(); a saved gaze
// can remain pending while its next creation is prepared.
class ControlSubmission {
  public:
    ControlSubmissionStatus Snapshot() const {
        auto out = status_;
        if (active_ && !out.result.Terminal())
            out.result = *active_;
        return out;
    }
    void Bind(std::uint64_t creation, api::ControlReceipt receipt) {
        status_ = Snapshot();
        if (status_.result.Terminal())
            return;
        status_.creation = creation;
        active_ = std::move(receipt);
    }
    void Complete(api::ControlPhase phase, std::string detail) {
        status_ = Snapshot();
        active_.reset();
        if (status_.result.Terminal())
            return;
        status_.result.phase = phase;
        status_.result.detail = std::move(detail);
    }
    void Supersede(bool replaced_in_session) {
        // Preserve the actual return of a synchronous call already in flight.
        if (!replaced_in_session || !active_)
            Complete(api::ControlPhase::Superseded, "new saved gaze intent");
    }
    void AwaitCreation() {
        status_ = Snapshot();
        active_.reset();
        if (status_.result.Terminal())
            return;
        status_.creation = 0;
        status_.result = {};
        status_.result.detail = "waiting for saved gaze's active creation";
    }

  private:
    ControlSubmissionStatus status_;
    api::ControlReceipt active_;
};
}
