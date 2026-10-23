#include <hksr/framework/presentation/VisualMovement.hpp>
#include <cmath>
#include <stdexcept>

namespace hksr::framework {
MovementRequestEndpoint BindMovementRequest(std::uint64_t generation,std::unique_ptr<api::OwnedMovementRequest> request) {
    if(!generation || !request)throw std::invalid_argument("movement endpoint requires an owned request");
    auto shared=std::shared_ptr<api::OwnedMovementRequest>(std::move(request));
    return {generation,[shared]{shared->Service();},[shared]{shared->Cancel();},[shared]{return shared->Status();}};
}
namespace {bool Finite(api::Position3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}}
void ValidateVisualMovement(const VisualMovement& movement) {
    if(movement.state.empty() || movement.state.find('\0')!=std::string::npos || movement.segments.empty())
        throw std::invalid_argument("movement requires an animation state and segments");
    if(movement.clock!=api::MovementClock::Animation && movement.clock!=api::MovementClock::DistanceSpeed)
        throw std::invalid_argument("unknown movement clock");
    for(const auto& s:movement.segments) {
        if(s.destination!=MovementDestination::Origin && s.destination!=MovementDestination::Anchor && s.destination!=MovementDestination::Target)
            throw std::invalid_argument("unknown movement destination");
        if((s.destination==MovementDestination::Origin)!=s.name.empty() || s.name.find('\0')!=std::string::npos)
            throw std::invalid_argument("movement origin is unnamed; anchor/target requires a name");
        if(!Finite(s.offset) || !std::isfinite(s.stop_distance) || s.stop_distance<0 || !std::isfinite(s.exit_time))
            throw std::invalid_argument("invalid movement offset/distance/cue");
        if(movement.clock==api::MovementClock::Animation && (!std::isfinite(s.start) || !std::isfinite(s.end) || s.end<=s.start))
            throw std::invalid_argument("movement interval must have a positive finite span");
        if(movement.clock==api::MovementClock::DistanceSpeed && (!std::isfinite(s.speed) || s.speed<=0))
            throw std::invalid_argument("movement speed must be positive and finite");
    }
}
api::AnimationMovePlan ResolveVisualMovement(const VisualMovement& movement,api::Position3 source,api::Position3 origin,
    const std::function<api::Position3(MovementDestination,const std::string&)>& resolve) {
    ValidateVisualMovement(movement);
    if(!Finite(source)||!Finite(origin))throw std::invalid_argument("movement origin is not finite");
    api::AnimationMovePlan out;out.origin=source;out.clock=movement.clock;out.force_start=movement.force_start;
    for(const auto& segment:movement.segments) {
        auto target=segment.destination==MovementDestination::Origin?origin:resolve(segment.destination,segment.name);
        if(segment.preserve_height)target.y=source.y;
        target.x+=segment.offset.x;target.y+=segment.offset.y;target.z+=segment.offset.z;
        if(!Finite(target))throw std::invalid_argument("resolved movement target is not finite");
        const double x=double(target.x)-source.x,y=double(target.y)-source.y,z=double(target.z)-source.z;
        const double distance=std::sqrt(x*x+y*y+z*z);
        if(distance>0 && segment.stop_distance>0) {
            const double fraction=distance>segment.stop_distance?(distance-segment.stop_distance)/distance:0;
            target={static_cast<float>(source.x+x*fraction),static_cast<float>(source.y+y*fraction),static_cast<float>(source.z+z*fraction)};
        }
        out.segments.push_back({target,segment.start,segment.end,segment.speed,segment.exit_time,nullptr});source=target;
    }
    // Includes speed-derived zero interval and native precision checks.
    (void)api::CompileAnimationMove(out);return out;
}
}
