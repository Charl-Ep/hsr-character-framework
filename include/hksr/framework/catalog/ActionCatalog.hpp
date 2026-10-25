#pragma once
#include "ActionCatalogTypes.hpp"

#include <hksr/animation/OfficialCatalog.hpp>
#include <functional>
#include <map>
#include <set>
#include <tuple>

namespace hksr::framework {
// Domain-local value cache; official-thread reads only on Advance. Event-file
// reference traversal is automatic, cycle-safe and shared across characters.
// No number-of-skills cap, default four-state list, JSON report or game pointers.
class ActionCatalog {
public:
    using Read=std::function<api::ActionConfigInput(api::ActionConfigKind,const std::string&)>;
    using ReadAbility=std::function<api::AbilitySources(const std::vector<std::string>&)>;
    using BeginAbility=std::function<std::unique_ptr<api::AbilitySourceRead>(const std::vector<std::string>&)>;
    using ReadVisual=std::function<api::VisualConfigSources(const std::string&)>;
    using ReadReadyDefaults=std::function<api::ReadyAnimationDefaults()>;
    ActionCatalog(api::OfficialCatalog& api,api::TypeRelationships& types);
    explicit ActionCatalog(Read read,ReadAbility abilities={},ReadVisual visuals={},ReadReadyDefaults ready={},BeginAbility begin={}):read_(std::move(read)),read_ability_(std::move(abilities)),read_visual_(std::move(visuals)),read_ready_(std::move(ready)),begin_ability_(std::move(begin)){}
    void Request(std::vector<ActionSourceRoot> roots);
    bool RequestVisuals(const std::string& character_config); // opt-in, shared by source
    void Advance(); // one document or one resumable shared-cache ability slice
    bool CancelPending(); // engine-thread drain; true when no managed reader remains
    bool ReadingAbilities() const {return bool(ability_read_);}
    void Refresh(const ActionSourceKey& source); // explicit retry, no per-frame failure loop
    void RefreshAbility(const std::string& ability);
    const ActionCatalogStatus& Status() const;
    bool Finished() const { return finished_; }
    std::uint64_t Revision() const {return revision_;}
    // Pure projection of the shared cache. Does not change queued discovery,
    // read the engine, or merge another character's actions into this view.
    ActionCatalogStatus View(const std::vector<ActionSourceRoot>& roots) const;
    std::shared_ptr<const ActionCatalogStatus> Snapshot(const std::vector<ActionSourceRoot>& roots) const;
    const api::ActionConfigInput* Document(const ActionSourceKey& source) const;
    // Source preparation shares this exact document read with discovery.
    const api::ActionConfigInput& Load(const ActionSourceKey& source);
    const api::ReadyAnimationDefaults& LoadReadyDefaults();
    const api::VisualConfigSources& LoadVisuals(const std::string& source);
private:
    struct Projection {
        ActionCatalogStatus status;
        std::vector<ActionSourceKey> documents;
        std::vector<std::string> abilities,visuals;
        bool ready=false;
        std::set<ActionSourceKey> used_documents;
        std::set<std::string> used_abilities,used_visuals;
        bool uses_ready=false;
    };
    using ViewKey=std::vector<std::tuple<ActionDomain,api::ActionConfigKind,std::string>>;
    struct CachedView {
        std::shared_ptr<const ActionCatalogStatus> value;
        std::set<ActionSourceKey> documents;
        std::set<std::string> abilities,visuals;
        bool ready=false;
    };
    // One dependency walk for discovery and projection. Discovery skips value
    // assembly; projections additionally retain their invalidation dependencies.
    Projection Project(const std::vector<ActionSourceRoot>& roots,bool values=true) const;
    void InvalidateDocument(const ActionSourceKey& source);
    void InvalidateAbility(const std::string& ability);
    void StoreAbilities(const std::vector<std::string>& names,api::AbilitySources result);
    void Rebuild();
    Read read_;
    ReadAbility read_ability_;
    ReadVisual read_visual_;
    ReadReadyDefaults read_ready_;
    BeginAbility begin_ability_;
    std::unique_ptr<api::AbilitySourceRead> ability_read_;
    std::vector<std::string> reading_abilities_;
    bool discard_read_=false;
    std::optional<api::ReadyAnimationDefaults> ready_defaults_;
    bool pending_ready_=false;
    std::map<std::string,api::VisualConfigSources> visual_sources_;
    std::set<std::string> visual_requests_;
    std::vector<std::string> pending_visuals_;
    std::vector<ActionSourceRoot> requested_;
    std::map<ActionSourceKey,api::ActionConfigInput> documents_;
    std::vector<ActionSourceKey> pending_;
    std::map<std::string,api::AbilitySources> ability_sources_;
    std::map<std::string,std::vector<ActionRetirement>> retirements_;
    std::vector<std::string> pending_abilities_;
    mutable ActionCatalogStatus status_;
    mutable std::uint64_t status_revision_=0;
    mutable std::map<ViewKey,CachedView> views_;
    std::map<std::tuple<ActionDomain,api::ActionConfigKind,std::string>,Projection> discovery_;
    bool finished_=false;
    bool advancing_=false;
    std::uint64_t revision_=0;
};
}
