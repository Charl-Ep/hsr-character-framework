#pragma once
#include <hksr/presentation/projectiles/OwnedProjectileGroup.hpp>
#include <hksr/presentation/projectiles/ProjectileTemplate.hpp>
#include <hksr/world/WorldFrames.hpp>
#include <memory>

namespace hksr::framework {
struct VisualProjectileShot {
    float at = 0;
    std::string parameters, target;
    std::map<std::string, float> values;
    bool ordered = true;
};
// One emission group: chain start/velocity and predecessor order are shared
// across these shots. Simultaneous shots and timed waves use the same plan.
struct VisualProjectilePlan {
    std::vector<VisualProjectileShot> shots;
};
void ValidateVisualProjectiles(const VisualProjectilePlan&);
struct ProjectileTemplateEndpoint {
    std::function<void*()> parameters;
    std::function<bool()> close;
};
struct ProjectileGroupEndpoint {
    std::function<bool()> prepare;
    std::function<api::ProjectileId(const api::ProjectileLaunch&)> fire;
    std::function<void(float)> service;
    std::function<void()> finish, cancel;
    std::function<api::ProjectileGroupStatus()> status;
};
ProjectileTemplateEndpoint BindProjectileTemplate(std::unique_ptr<api::ProjectileTemplate>);
ProjectileGroupEndpoint BindProjectileGroup(std::unique_ptr<api::OwnedProjectileGroup>);
struct ProjectileBatchServices {
    std::function<ProjectileGroupEndpoint()> create;
    std::function<ProjectileTemplateEndpoint(const std::string&)> parameters;
    std::function<api::ProjectileLaunch(const std::string&)> target;
};
struct ProjectileBatchStatus {
    bool accepted = false, started = false, emitted = false, arrived = false, cancelled = false, closed = false;
    std::size_t submitted = 0, arrivals = 0;
    std::string error;
};
class VisualProjectileBatch {
  public:
    VisualProjectileBatch(api::EntityOrigin, VisualProjectilePlan, ProjectileBatchServices);
    ~VisualProjectileBatch(); // explicit Service drains native work
    void Service(const api::WorldFrame&);
    void Cancel();
    bool ReleaseParticipant(api::EntityOrigin); // stop/drain before model mutation
    const ProjectileBatchStatus& Status() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
struct ProjectileRequestEndpoint {
    std::uint64_t entity_generation = 0;
    std::function<void()> service, cancel;
    std::function<ProjectileBatchStatus()> status;
};
}
