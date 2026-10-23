#pragma once
#include <hksr/presentation/movement/OwnedMovementRequest.hpp>
#include <functional>
#include <string>

namespace hksr::framework {
struct MovementRequestEndpoint {
    std::uint64_t entity_generation=0;
    std::function<void()> service,cancel;
    std::function<api::MovementRequestStatus()> status;
};
MovementRequestEndpoint BindMovementRequest(std::uint64_t,std::unique_ptr<api::OwnedMovementRequest>);
enum class MovementDestination { Origin,Anchor,Target };
struct VisualMovementSegment {
    MovementDestination destination=MovementDestination::Origin;
    std::string name;
    api::Position3 offset;
    bool preserve_height=true;
    float stop_distance=0,start=0,end=1,speed=1,exit_time=-1;
};
struct VisualMovement {
    std::string state;
    api::MovementClock clock=api::MovementClock::Animation;
    bool force_start=false;
    std::vector<VisualMovementSegment> segments;
};
void ValidateVisualMovement(const VisualMovement&);
// Snapshots only; no gameplay selectors, formation access or per-frame motion.
// Origin is shared by the sequence; source is this request's live position.
api::AnimationMovePlan ResolveVisualMovement(const VisualMovement&,api::Position3 source,api::Position3 origin,
    const std::function<api::Position3(MovementDestination,const std::string&)>& resolve);
}
