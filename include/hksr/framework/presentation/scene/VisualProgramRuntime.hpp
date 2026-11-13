#pragma once
#include <hksr/framework/presentation/scene/VisualProgram.hpp>
#include <hksr/framework/presentation/effects/VisualEventRoutes.hpp>
#include <hksr/framework/presentation/timeline/VisualTimelines.hpp>
#include <hksr/framework/presentation/movement/VisualEntityMotion.hpp>
#include <hksr/framework/presentation/partners/VisualPartners.hpp>
#include <hksr/framework/presentation/scene/VisualProgramLibrary.hpp>
#include <hksr/framework/presentation/scene/VisualProgramBindings.hpp>
#include <hksr/framework/presentation/scene/PresentationProfile.hpp>
#include <hksr/presentation/PresentationContext.hpp>
#include <hksr/presentation/events/AnimationEventConstruction.hpp>
#include <hksr/managed/ManagedRoots.hpp>

namespace hksr::framework {
class VisualProgramPlayer;
struct TaskCatalog;
class ActorRenderingScopes;
class SharedVisualServices;
class ActorEventEffects;
// Prepared with the host, activated before model construction. Owns observers
// and keeps retiring players until their effects and Timelines drain.
class VisualProgramRuntime {
  public:
    using PhaseReader = std::function<api::PhaseVisualConfigSources(const std::string&, const api::PhaseVisualSource&)>;
    using ProjectileReader = std::function<std::unique_ptr<api::ProjectileTemplate>(const api::DefinitionSource&)>;
    using RadiusReader = std::function<float(const std::string&)>;
    VisualProgramRuntime(api::PresentationContext&, api::ManagedObjects&, VisualEventRoutes::Trace = {});
    ~VisualProgramRuntime();
    void Activate();
    void BeginFrame(const api::WorldFrame&);
    void SetPhaseReader(PhaseReader reader) {
        phase_reader_ = std::move(reader);
    }
    void SetProjectileReader(ProjectileReader reader) {
        projectile_reader_ = std::move(reader);
    }
    void SetRadiusReader(RadiusReader reader) {
        radius_reader_ = std::move(reader);
    }
    std::shared_ptr<VisualProgramPlayer> Create(api::EntityOrigin, const VisualProgramInput&,
                                                const VisualProgramBindings&,
                                                const std::shared_ptr<const PresentationProfile>&);
    void Collect();
    bool PrepareModelChange(api::EntityOrigin);
    bool Close(); // after sessions close; no early detachment of producers
  private:
    struct ActorScene;
    void Attach(ActorScene&, std::shared_ptr<VisualEffectContext>, const VisualContextKey& context = {},
                std::uint64_t generation = 0);
    void Detach(ActorScene&, const VisualContextKey& context = {}, std::uint64_t generation = 0);
    VisualEffectAdmission CreateEffect(ActorScene&, VisualEffectContext&, const std::string&, const VisualEffectInput&);
    api::EntityOrigin ResolveTarget(ActorScene&, VisualEffectContext&, const std::string&);
    std::vector<std::string> AvailableEntities(ActorScene&, const VisualProgramParameters&,
                                               bool restored = false) const;
    std::optional<VisualSpatialAnchor> PositionTarget(ActorScene&, VisualEffectContext&, const std::string&) const;
    VisualSpatialAnchor TargetPose(ActorScene&, VisualEffectContext&, const std::string&);
    std::shared_ptr<VisualEntityMotion> Motion();
    VisualPartners& Partners(const std::shared_ptr<ActorScene>&);
    api::PresentationContext& api_;
    api::ManagedObjects& objects_;
    VisualEventRoutes routes_;
    VisualEventRoutes::Trace trace_;
    std::unique_ptr<api::AnimationSequenceExecutions> sequences_;
    std::unique_ptr<api::AnimationEventConstruction> constructions_;
    std::unique_ptr<api::EffectSubmissions> effects_;
    std::unique_ptr<api::EffectEntityFollow> entity_follow_;
    std::shared_ptr<VisualEntityMotion> motion_;
    std::unique_ptr<api::ModelScaleProgram> character_scale_;
    std::unique_ptr<ActorRenderingScopes> rendering_;
    std::shared_ptr<SharedVisualServices> shared_visual_;
    std::shared_ptr<api::EffectLayouts> layouts_;
    std::vector<std::shared_ptr<ActorScene>> scenes_;
    struct NativeActorEffects {
        api::EntityOrigin owner;
        std::shared_ptr<ActorEventEffects> effects;
    };
    std::vector<NativeActorEffects> native_effects_;
    PhaseReader phase_reader_;
    ProjectileReader projectile_reader_;
    RadiusReader radius_reader_;
    std::map<std::string, float> base_radii_;
    struct PhaseCache {
        std::shared_ptr<const VisualProgram> program;
        std::map<std::string, std::shared_ptr<const api::PhaseVisualConfigSources>> values;
    };
    std::map<const VisualProgram*, PhaseCache> phase_cache_;
    bool closing_ = false;
    std::optional<api::WorldFrame> frame_;
};
}
