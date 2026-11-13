#pragma once
#include <hksr/presentation/movement/OwnedMovementRequest.hpp>
#include <hksr/presentation/movement/MovementGeometry.hpp>
#include <hksr/framework/presentation/scene/VisualProgramState.hpp>
#include <functional>
#include <string>

namespace hksr::framework {
struct MovementRequestEndpoint {
    std::uint64_t entity_generation = 0;
    std::function<void()> service, cancel;
    std::function<api::MovementRequestStatus()> status;
    std::function<bool()> superseded; // newer explicit placement, not a native motion failure
};
MovementRequestEndpoint BindMovementRequest(std::uint64_t, std::unique_ptr<api::OwnedMovementRequest>);
// Owner-thread arbitration. A user placement ends earlier scripted movement;
// its remaining movement/return steps cannot overwrite the new position.
// A subsequently selected script captures the new epoch and can move normally.
class MovementPlacementIntent {
  public:
    std::uint64_t Epoch() const {
        return epoch_;
    }
    void Reposition();
    MovementRequestEndpoint Bind(std::uint64_t epoch, std::uint64_t entity_generation,
                                 const std::function<MovementRequestEndpoint()>& start);

  private:
    struct Request;
    std::uint64_t epoch_ = 0;
    std::vector<std::weak_ptr<Request>> requests_;
};
enum class MovementDestination { Origin, Anchor, Target };
// Fully resolved source sample. Distance includes any radius contribution;
// this value is signed and is not the clamped preview stop_distance.
struct VisualMovementSample {
    float percentage = 1, distance = 0;
    api::Position3 direction;
    bool behind_target = false;
    bool include_radii = false;
};
struct VisualMovementSegment {
    MovementDestination destination = MovementDestination::Origin;
    std::string name;
    api::Position3 offset;
    bool preserve_height = true;
    float stop_distance = 0, start = 0, end = 1, speed = 1, exit_time = -1;
    std::optional<VisualMovementSample> sample;
    bool target_geometry = false;
};
struct VisualMovement {
    std::string state;
    api::MovementClock clock = api::MovementClock::Animation;
    bool force_start = false;
    std::vector<VisualMovementSegment> segments;
    bool instant = false;
};
void ValidateVisualMovement(const VisualMovement&);
api::Position3 ResolveVisualMovementSample(const VisualMovementSample&, api::Position3 origin,
                                           const VisualSpatialAnchor& target);
// Snapshots only; no gameplay selectors, formation access or per-frame motion.
// Origin is shared by the sequence; source is this request's live position.
api::AnimationMovePlan
ResolveVisualMovement(const VisualMovement&, api::Position3 source, api::Position3 origin,
                      const std::function<VisualSpatialAnchor(MovementDestination, const std::string&)>& resolve,
                      const std::function<api::MovementGeometry(const std::string&)>& geometry = {});
}
