#pragma once
#include "VisualEventRoutes.hpp"
#include "VisualTimelines.hpp"
#include "VisualEntityMotion.hpp"
#include "VisualPartners.hpp"
#include "PresentationProfile.hpp"
#include <hksr/presentation/PresentationContext.hpp>
#include <hksr/presentation/events/AnimationEventConstruction.hpp>
#include <hksr/managed/ManagedRoots.hpp>

namespace hksr::framework {
struct TaskCatalog;
struct VisualProgramSignal {
    std::uint64_t generation=0,intent=0;
    std::string name;
};
// Per-creation inputs. Targets are explicit entity generations, never a
// gameplay target selector or a fallback to the caster. Not layout data.
struct VisualProgramInput {
    std::shared_ptr<const VisualProgram> program;
    std::shared_ptr<const TaskCatalog> tasks;
    std::map<std::string,api::EntityOrigin> targets;
    std::map<std::string,std::string> choices;
    // Application instance IDs are resolved on the engine thread at submission,
    // after restoration/creation. Direct targets take precedence.
    std::map<std::string,std::uint64_t> target_characters;
    std::function<api::EntityOrigin(std::uint64_t)> resolve_character;
    // Optional value snapshot, read at action selection, not per frame. Values
    // overlay the startup defaults. Existing requests retain their old owners.
    std::uint64_t character=0;
    std::function<VisualProgramParameters(std::uint64_t)> read_parameters;
    // Data-only mailbox. Consumed on the engine thread; stale creation/action
    // receipts are reported without dispatching into a replacement scene.
    std::function<std::vector<VisualProgramSignal>(std::uint64_t)> take_signals;
    VisualSpatialAnchors anchors;
};
// Source defaults and explicitly selected task catalogs are separate namespaces.
// Registering a task catalog must not replace an existing source's visual scene.
struct VisualProgramLibrary {
    std::map<std::string,std::shared_ptr<const VisualProgramInput>> sources;
    std::map<std::string,std::shared_ptr<const VisualProgramInput>> catalogs;
    bool empty() const {return sources.empty() && catalogs.empty();}
};
// Prepared with the host, activated before model construction. Owns observers
// and keeps retiring players until their effects and Timelines drain.
class VisualProgramRuntime {
public:
    using PhaseReader=std::function<api::PhaseVisualConfigSources(const std::string&,const api::PhaseVisualSource&)>;
    VisualProgramRuntime(api::PresentationContext&,api::ManagedObjects&,VisualEventRoutes::Trace={});
    ~VisualProgramRuntime();
    void Activate();
    void BeginFrame(const api::WorldFrame&);
    void SetPhaseReader(PhaseReader reader) {phase_reader_=std::move(reader);}
    std::shared_ptr<VisualProgramPlayer> Create(api::EntityOrigin,const VisualProgramInput&,
        const std::shared_ptr<const PresentationProfile>&);
    void Collect();
    bool Close(); // after sessions close; no early detachment of producers
private:
    struct Scene;
    void Attach(Scene&,std::shared_ptr<VisualEffectContext>,const std::string& context={});
    void Detach(Scene&,const std::string& context={});
    VisualEffectId CreateEffect(Scene&,VisualEffectContext&,const std::string&,const VisualEffectInput&);
    api::EntityOrigin ResolveTarget(Scene&,VisualEffectContext&,const std::string&);
    std::shared_ptr<VisualEntityMotion> Motion();
    VisualPartners& Partners(const std::shared_ptr<Scene>&);
    api::PresentationContext& api_;
    api::ManagedObjects& objects_;
    VisualEventRoutes routes_;
    VisualEventRoutes::Trace trace_;
    std::unique_ptr<api::AnimationSequenceExecutions> sequences_;
    std::unique_ptr<api::AnimationEventConstruction> constructions_;
    std::unique_ptr<api::EffectSubmissions> effects_;
    std::unique_ptr<api::EffectEntityFollow> entity_follow_;
    std::shared_ptr<VisualEntityMotion> motion_;
    std::vector<std::shared_ptr<Scene>> scenes_;
    PhaseReader phase_reader_;
    struct PhaseCache {std::shared_ptr<const VisualProgram> program;std::map<std::string,std::shared_ptr<const api::PhaseVisualConfigSources>> values;};
    std::map<const VisualProgram*,PhaseCache> phase_cache_;
    bool closing_=false;
    std::optional<api::WorldFrame> frame_;
};
}
