#include <hksr/framework/presentation/movement/VisualMovement.hpp>
#include <hksr/framework/presentation/scene/VisualTargets.hpp>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace hksr::framework {
MovementRequestEndpoint BindMovementRequest(std::uint64_t generation,
                                            std::unique_ptr<api::OwnedMovementRequest> request) {
    if (!generation || !request)
        throw std::invalid_argument("movement endpoint requires an owned request");
    auto shared = std::shared_ptr<api::OwnedMovementRequest>(std::move(request));
    return {generation, [shared] { shared->Service(); }, [shared] { shared->Cancel(); },
            [shared] { return shared->Status(); }};
}
struct MovementPlacementIntent::Request {
    MovementRequestEndpoint endpoint;
    bool superseded = false;
};
void MovementPlacementIntent::Reposition() {
    ++epoch_;
    const auto requests = requests_;
    for (const auto& weak : requests)
        if (const auto request = weak.lock()) {
            if (request->superseded || request->endpoint.status().closed)
                continue;
            request->superseded = true;
            request->endpoint.cancel();
            request->endpoint.service();
        }
}
MovementRequestEndpoint MovementPlacementIntent::Bind(std::uint64_t epoch, std::uint64_t generation,
                                                      const std::function<MovementRequestEndpoint()>& start) {
    if (epoch != epoch_)
        return {generation, [] {}, [] {},
                [] {
                    api::MovementRequestStatus status;
                    status.closed = true;
                    return status;
                },
                [] { return true; }};
    auto request = std::make_shared<Request>();
    request->endpoint = start();
    requests_.erase(std::remove_if(requests_.begin(), requests_.end(), [](const auto& weak) { return weak.expired(); }),
                    requests_.end());
    requests_.push_back(request);
    if (epoch != epoch_) {
        request->superseded = true;
        request->endpoint.cancel();
        request->endpoint.service();
    }
    return {request->endpoint.entity_generation, [request] { request->endpoint.service(); },
            [request] { request->endpoint.cancel(); }, [request] { return request->endpoint.status(); },
            [request] { return request->superseded; }};
}
namespace {
bool Finite(api::Position3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
}
void ValidateVisualMovement(const VisualMovement& movement) {
    if ((!movement.instant && movement.state.empty()) || movement.state.find('\0') != std::string::npos ||
        movement.segments.empty())
        throw std::invalid_argument("movement requires an animation state and segments");
    if (movement.instant && (!movement.state.empty() || movement.segments.size() != 1 || movement.force_start))
        throw std::invalid_argument("instant movement requires one destination and no animation state");
    if (movement.clock != api::MovementClock::Animation && movement.clock != api::MovementClock::DistanceSpeed)
        throw std::invalid_argument("unknown movement clock");
    for (const auto& s : movement.segments) {
        if (s.destination != MovementDestination::Origin && s.destination != MovementDestination::Anchor &&
            s.destination != MovementDestination::Target)
            throw std::invalid_argument("unknown movement destination");
        if ((s.destination == MovementDestination::Origin) != s.name.empty() || s.name.find('\0') != std::string::npos)
            throw std::invalid_argument("movement origin is unnamed; anchor/target requires a name");
        if (!Finite(s.offset) || !std::isfinite(s.stop_distance) || s.stop_distance < 0 || !std::isfinite(s.exit_time))
            throw std::invalid_argument("invalid movement offset/distance/cue");
        if (s.sample && (!std::isfinite(s.sample->percentage) || !std::isfinite(s.sample->distance) ||
                         !Finite(s.sample->direction) || s.stop_distance != 0))
            throw std::invalid_argument("source movement sample requires finite values and no preview stop distance");
        if (s.sample && s.sample->behind_target &&
            (s.destination == MovementDestination::Origin || s.sample->direction.x != 0 || s.sample->direction.y != 0 ||
             s.sample->direction.z != 0))
            throw std::invalid_argument("behind-target sample requires a target pose and no explicit direction");
        if (s.target_geometry && s.destination != MovementDestination::Target)
            throw std::invalid_argument("target geometry requires an entity/point target");
        if (!movement.instant && movement.clock == api::MovementClock::Animation &&
            (!std::isfinite(s.start) || !std::isfinite(s.end) || s.end <= s.start))
            throw std::invalid_argument("movement interval must have a positive finite span");
        if (!movement.instant && movement.clock == api::MovementClock::DistanceSpeed &&
            (!std::isfinite(s.speed) || s.speed <= 0))
            throw std::invalid_argument("movement speed must be positive and finite");
    }
}
api::Position3 ResolveVisualMovementSample(const VisualMovementSample& sample, api::Position3 origin,
                                           const VisualSpatialAnchor& target) {
    if (!Finite(origin) || !Finite(target.position) || !Finite(sample.direction) || !std::isfinite(sample.percentage) ||
        !std::isfinite(sample.distance))
        throw std::invalid_argument("nonfinite movement sample");
    auto direction = sample.direction;
    if (sample.behind_target) {
        const auto pose = TransformVisualTarget({{}, target.rotation}, {0, 0, -1});
        direction = pose.position;
    }
    // DDF sample evaluation uses separate squared-direction and normalization
    // thresholds. Keep both: a nonzero tiny vector is not a unit direction.
    const float square = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
    const bool directed = square >= 1e-10f;
    auto point = target.position;
    if (directed) {
        point.x += direction.x * sample.distance;
        point.y += direction.y * sample.distance;
        point.z += direction.z * sample.distance;
    }
    api::Position3 delta{point.x - origin.x, point.y - origin.y, point.z - origin.z};
    const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    if (length > 1e-5f) {
        delta.x /= length;
        delta.y /= length;
        delta.z /= length;
    } else
        delta = {};
    const float travel = directed ? length : length - sample.distance;
    api::Position3 result{origin.x + delta.x * travel * sample.percentage,
                          origin.y + delta.y * travel * sample.percentage,
                          origin.z + delta.z * travel * sample.percentage};
    if (!Finite(result))
        throw std::invalid_argument("movement sample overflow");
    return result;
}
api::AnimationMovePlan
ResolveVisualMovement(const VisualMovement& movement, api::Position3 source, api::Position3 origin,
                      const std::function<VisualSpatialAnchor(MovementDestination, const std::string&)>& resolve,
                      const std::function<api::MovementGeometry(const std::string&)>& geometry) {
    ValidateVisualMovement(movement);
    if (!Finite(source) || !Finite(origin))
        throw std::invalid_argument("movement origin is not finite");
    api::AnimationMovePlan out;
    out.origin = source;
    out.clock = movement.clock;
    out.force_start = movement.force_start;
    std::map<std::string, api::MovementGeometry> geometries;
    auto shape = [&](const std::string& name) {
        if (const auto it = geometries.find(name); it != geometries.end())
            return it->second;
        if (!geometry)
            throw std::invalid_argument("movement geometry provider missing");
        auto value = geometry(name);
        if (!std::isfinite(value.radius) || !Finite(value.local_offset))
            throw std::invalid_argument("invalid movement geometry input");
        geometries.emplace(name, value);
        return value;
    };
    for (const auto& segment : movement.segments) {
        auto pose = segment.destination == MovementDestination::Origin ? VisualSpatialAnchor{origin, {}}
                                                                       : resolve(segment.destination, segment.name);
        auto& target = pose.position;
        if (segment.target_geometry) {
            const auto offset = shape(segment.name).local_offset;
            // The native producer projects X/Z through target right/forward;
            // Y remains vertical, rather than rotating the entire offset.
            const auto horizontal = TransformVisualTarget({{}, pose.rotation}, {offset.x, 0, offset.z}).position;
            target.x += horizontal.x;
            target.y += offset.y;
            target.z += horizontal.z;
        }
        if (!segment.sample && segment.preserve_height)
            target.y = source.y;
        target.x += segment.offset.x;
        target.y += segment.offset.y;
        target.z += segment.offset.z;
        if (segment.sample && segment.preserve_height)
            target.y = out.origin.y;
        if (!Finite(target))
            throw std::invalid_argument("resolved movement target is not finite");
        const double x = double(target.x) - source.x, y = double(target.y) - source.y, z = double(target.z) - source.z;
        const double distance = std::sqrt(x * x + y * y + z * z);
        if (segment.sample) {
            auto sample = *segment.sample;
            if (sample.include_radii) {
                sample.distance += shape("Caster").radius;
                if (segment.destination == MovementDestination::Target)
                    sample.distance += shape(segment.name).radius;
            }
            target = ResolveVisualMovementSample(sample, source, pose);
        } else if (distance > 0 && segment.stop_distance > 0) {
            const double fraction =
                distance > segment.stop_distance ? (distance - segment.stop_distance) / distance : 0;
            target = {static_cast<float>(source.x + x * fraction), static_cast<float>(source.y + y * fraction),
                      static_cast<float>(source.z + z * fraction)};
        }
        out.segments.push_back({target, segment.start, segment.end, segment.speed, segment.exit_time, nullptr});
        source = target;
    }
    // Includes speed-derived zero interval and native precision checks.
    // Instant consumers use only the resolved endpoint, never an animation graph.
    if (!movement.instant)
        (void)api::CompileAnimationMove(out);
    return out;
}
}
