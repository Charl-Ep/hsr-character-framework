#pragma once
#include <hksr/entity/EntityLifecycle.hpp>
#include <hksr/world/WorldFrames.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hksr::api {
class ActorModelChange;
}
namespace hksr::framework {
class VisualEffectContext;
class SpatialHitEffects;
class VisualProjectileBatch;
namespace detail {
// Requests which may outlive their submitting action. Retain each request
// through its actual drain; a model handoff releases only its participants.
class VisualSceneTransients {
  public:
    explicit VisualSceneTransients(api::EntityOrigin owner);
    ~VisualSceneTransients(); // native destruction only
    std::shared_ptr<SpatialHitEffects> FindPointHit(const std::shared_ptr<VisualEffectContext>&,
                                                   const std::string& target) const;
    void KeepPointHit(std::shared_ptr<VisualEffectContext>, std::string target, std::shared_ptr<SpatialHitEffects>,
                      std::uint64_t frame);
    void KeepProjectile(std::shared_ptr<VisualProjectileBatch>);
    api::ActorModelChange& KeepModelChange(std::unique_ptr<api::ActorModelChange>);
    void CollectModelChanges(const std::function<void(const std::string&)>& report);
    void Service(const api::WorldFrame* frame, bool retiring);
    bool ReleaseParticipant(api::EntityOrigin);
    bool Drained() const;

  private:
    api::EntityOrigin owner_;
    struct PointHit {
        std::shared_ptr<VisualEffectContext> context;
        std::string target;
        std::shared_ptr<SpatialHitEffects> effects;
        std::uint64_t frame = 0;
    };
    std::vector<PointHit> point_hits_;
    std::vector<std::shared_ptr<VisualProjectileBatch>> projectiles_;
    std::vector<std::unique_ptr<api::ActorModelChange>> model_changes_;
    std::string cleanup_error_;
};
}
}
