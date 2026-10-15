#pragma once
#include "CatalogSnapshot.hpp"
#include "SourceCatalog.hpp"
#include "ActionCatalog.hpp"
#include "../character/CharacterPlan.hpp"
#include "../presentation/ActionSelection.hpp"
#include <deque>

namespace hksr::framework {
struct CatalogReaders {
    std::function<api::CharacterCatalog()> characters;
    std::function<api::TextInput(const std::string&)> identity;
    ActionCatalog::Read document;
    ActionCatalog::ReadAbility abilities;
    std::function<api::NpcMotionSources(void*)> npc_motions;
    ActionCatalog::ReadVisual visuals;
    ActionCatalog::ReadReadyDefaults ready_defaults;
    ActionCatalog::BeginAbility begin_abilities;
};
// Official-thread coordinator. Construction and selection do not call the
// engine. UI receives immutable values, never game objects or query callbacks.
class CatalogBrowser {
public:
    explicit CatalogBrowser(CatalogReaders readers);
    void Request(bool refresh=false);
    std::uint64_t Prepare(BrowseSource source);
    // Retry creates a new request; existing results/plans never change under
    // another controller. Release cancels queued work and forgets its result.
    std::uint64_t RetrySource(std::uint64_t request);
    void ReleaseSource(std::uint64_t request);
    void Advance(std::uint32_t budget_microseconds=2000);
    void BatchSources(const std::function<void()>& update);
    bool CancelPending() { return actions_.CancelPending(); }
    void RefreshDocument(const ActionSourceKey& source);
    void RefreshAbility(const std::string& ability);
    void InspectActions(std::uint64_t character,std::vector<ActionSourceRoot> roots);
    void ForgetCharacter(std::uint64_t character);
    PlacedAction ResolveAction(std::uint64_t character,const CatalogActionSelection& selection) const;
    CharacterPlan Plan(std::uint64_t request,std::shared_ptr<const ModelAssetInput> payload={},std::string look={}) const;
    std::shared_ptr<const CatalogSnapshot> Snapshot() const { return snapshot_; }
    api::NpcMotionSources NpcMotions(void* owned_component) const;
private:
    void BeginSource(const BrowseSource& input);
    void AdvanceSource();
    bool AdvanceOne();
    void Publish();
    void RequestActions();
    struct CharacterSources {std::uint64_t revision;std::vector<ActionSourceRoot> roots;};
    std::map<std::uint64_t,CharacterSources> character_sources_;
    std::map<std::uint64_t,std::vector<ActionSourceRoot>> source_roots_;
    std::deque<std::uint64_t> pending_sources_;
    std::uint64_t revision_=0,next_source_=0;
    CatalogReaders readers_;
    ActionCatalog actions_;
    std::unique_ptr<SourcePreparation> preparation_;
    CatalogSnapshot state_;
    std::shared_ptr<const CatalogSnapshot> snapshot_;
    bool table_pending_=false,source_turn_=true,actions_started_=false;
    bool publication_dirty_=false;
    unsigned source_batch_=0;
    bool sources_dirty_=false;
};
}
