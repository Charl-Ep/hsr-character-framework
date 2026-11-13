#include <hksr/framework/presentation/projectiles/VisualProjectiles.hpp>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace hksr::framework {
namespace {
bool Same(api::EntityOrigin a, api::EntityOrigin b) {
    return a.token.entity == b.token.entity && a.token.generation == b.token.generation && a.world == b.world;
}
}
void ValidateVisualProjectiles(const VisualProjectilePlan& plan) {
    if (plan.shots.empty())
        throw std::invalid_argument("projectile group has no emissions");
    float previous = 0;
    for (const auto& shot : plan.shots) {
        if (!std::isfinite(shot.at) || shot.at < previous || shot.parameters.empty() || shot.target.empty() ||
            shot.parameters.find('\0') != std::string::npos || shot.target.find('\0') != std::string::npos)
            throw std::invalid_argument("invalid projectile emission");
        for (const auto& [name, value] : shot.values)
            if (name.empty() || name.find('\0') != std::string::npos || !std::isfinite(value))
                throw std::invalid_argument("invalid projectile numeric input");
        previous = shot.at;
    }
}
ProjectileTemplateEndpoint BindProjectileTemplate(std::unique_ptr<api::ProjectileTemplate> value) {
    if (!value)
        throw std::invalid_argument("projectile template absent");
    auto shared = std::shared_ptr<api::ProjectileTemplate>(std::move(value));
    return {[shared] { return shared->Parameters(); }, [shared] { return shared->Close(); }};
}
ProjectileGroupEndpoint BindProjectileGroup(std::unique_ptr<api::OwnedProjectileGroup> value) {
    if (!value)
        throw std::invalid_argument("projectile group absent");
    auto shared = std::shared_ptr<api::OwnedProjectileGroup>(std::move(value));
    return {[shared] { return shared->Prepare(); },
            [shared](const auto& launch) { return shared->Fire(launch); },
            [shared](float dt) { shared->Service(dt); },
            [shared] { shared->Finish(); },
            [shared] { shared->Cancel(); },
            [shared] { return shared->Status(); }};
}
struct VisualProjectileBatch::State {
    api::EntityOrigin owner;
    VisualProjectilePlan plan;
    ProjectileBatchServices services;
    ProjectileBatchStatus status;
    std::optional<ProjectileGroupEndpoint> group;
    std::map<std::string, ProjectileTemplateEndpoint> parameters;
    std::vector<api::EntityOrigin> participants;
    std::uint64_t last_frame = 0;
    bool frame_seen = false, servicing = false;
    double elapsed = 0;
    std::size_t next = 0;
    void ReadStatus() {
        if (!group)
            return;
        const auto value = group->status();
        status.submitted = value.submitted;
        status.arrivals = value.arrived;
        status.arrived = status.emitted && value.arrived == plan.shots.size();
        if (!value.errors.empty()) {
            if (status.error.empty())
                status.error = value.errors.front();
            status.cancelled = true;
        }
        if (value.cancelled && !status.cancelled) {
            status.cancelled = true;
            if (status.error.empty())
                status.error = "projectile participant retired";
        }
    }
    void Drain() {
        if (group) {
            group->cancel();
            group->service(0);
            ReadStatus();
            if (!group->status().closed)
                return;
        }
        CloseParameters();
    }
    void CloseParameters() {
        for (auto it = parameters.begin(); it != parameters.end();) {
            if (it->second.close()) {
                it = parameters.erase(it);
            } else
                ++it;
        }
        status.closed = parameters.empty();
    }
};
VisualProjectileBatch::VisualProjectileBatch(api::EntityOrigin owner, VisualProjectilePlan plan,
                                             ProjectileBatchServices services) {
    ValidateVisualProjectiles(plan);
    if (!owner.token.entity || !owner.token.generation || !owner.world || !services.create || !services.parameters ||
        !services.target)
        throw std::invalid_argument("projectile batch requires owned services");
    state_ = std::make_unique<State>();
    state_->owner = owner;
    state_->plan = std::move(plan);
    state_->services = std::move(services);
    state_->status.accepted = true;
}
VisualProjectileBatch::~VisualProjectileBatch() = default;
void VisualProjectileBatch::Cancel() {
    state_->status.cancelled = true;
}
const ProjectileBatchStatus& VisualProjectileBatch::Status() const {
    return state_->status;
}
bool VisualProjectileBatch::ReleaseParticipant(api::EntityOrigin origin) {
    auto& s = *state_;
    bool matches = Same(origin, s.owner);
    for (const auto& target : s.participants)
        matches = matches || Same(origin, target);
    if (!matches)
        return true;
    Cancel();
    api::WorldFrame frame;
    frame.world = s.owner.world;
    Service(frame);
    return s.status.closed;
}
void VisualProjectileBatch::Service(const api::WorldFrame& frame) {
    auto& s = *state_;
    if (s.servicing || s.status.closed)
        return;
    s.servicing = true;
    struct Guard {
        bool& flag;
        ~Guard() {
            flag = false;
        }
    } guard{s.servicing};
    try {
        if (s.status.cancelled) {
            s.Drain();
            return;
        }
        if (frame.world != s.owner.world) {
            s.status.error = "projectile batch left its world";
            s.status.cancelled = true;
            s.Drain();
            return;
        }
        if (!std::isfinite(frame.delta_time) || frame.delta_time < 0)
            throw std::invalid_argument("invalid projectile frame delta");
        if (s.frame_seen && frame.sequence == s.last_frame)
            return;
        const bool first = !s.frame_seen;
        s.frame_seen = true;
        s.last_frame = frame.sequence;
        if (!first)
            s.elapsed += frame.delta_time;
        if (!s.group) {
            s.group = s.services.create();
            auto& g = *s.group;
            if (!g.prepare || !g.fire || !g.service || !g.finish || !g.cancel || !g.status)
                throw std::runtime_error("incomplete projectile group endpoint");
            if (!g.prepare()) {
                s.ReadStatus();
                throw std::runtime_error(s.status.error.empty() ? "projectile group preparation failed"
                                                                : s.status.error);
            }
            if (s.status.cancelled) {
                s.Drain();
                return;
            }
        } else {
            s.group->service(frame.delta_time);
            s.ReadStatus();
            if (s.status.cancelled) {
                s.Drain();
                return;
            }
        }
        while (s.next < s.plan.shots.size() && s.plan.shots[s.next].at <= s.elapsed) {
            const auto& shot = s.plan.shots[s.next];
            auto found = s.parameters.find(shot.parameters);
            if (found == s.parameters.end()) {
                auto input = s.services.parameters(shot.parameters);
                found = s.parameters.emplace(shot.parameters, std::move(input)).first;
                if (!found->second.parameters || !found->second.close)
                    throw std::runtime_error("incomplete projectile parameter endpoint");
            }
            if (s.status.cancelled) {
                s.Drain();
                return;
            }
            auto launch = s.services.target(shot.target);
            s.participants.push_back(launch.target);
            launch.parameters = found->second.parameters();
            launch.values = shot.values;
            launch.ordered = shot.ordered;
            if (s.status.cancelled) {
                s.Drain();
                return;
            }
            if (!s.group->fire(launch)) {
                s.ReadStatus();
                throw std::runtime_error(s.status.error.empty() ? "projectile emission failed" : s.status.error);
            }
            ++s.next;
            s.status.started = true;
            if (s.status.cancelled) {
                s.Drain();
                return;
            }
        }
        if (s.next == s.plan.shots.size() && !s.status.emitted) {
            s.status.emitted = true;
            s.group->finish();
        }
        s.ReadStatus();
        if (s.status.cancelled) {
            s.Drain();
            return;
        }
        if (s.group->status().closed)
            s.CloseParameters();
    } catch (const std::exception& e) {
        if (s.status.error.empty())
            s.status.error = e.what();
        s.status.cancelled = true;
        // The next service boundary drains retained partial preparation; do
        // not retry the native submission that produced an unknown outcome.
    }
}
}
