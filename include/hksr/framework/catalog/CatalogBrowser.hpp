#pragma once
#include <hksr/framework/catalog/CatalogSnapshot.hpp>
#include <hksr/framework/catalog/SourceCatalog.hpp>
#include <hksr/framework/catalog/ActionCatalog.hpp>
#include <hksr/framework/character/CharacterPlan.hpp>
#include <hksr/framework/catalog/ActionSelection.hpp>
#include <hksr/framework/presentation/playback/PoseRequests.hpp>
#include <hksr/framework/presentation/reactions/ReactionPolicy.hpp>
#include <deque>
#include <chrono>

namespace hksr::framework {
struct CatalogWork {
    std::uint64_t slices = 0, total_us = 0, max_us = 0, source_steps = 0, action_steps = 0, elapsed_us = 0;
    std::uint64_t name_steps = 0;
};
struct CatalogReaders {
    std::function<api::CharacterCatalog()> characters;
    ActionCatalog::Read document;
    ActionCatalog::ReadAbility abilities;
    std::function<api::NpcMotionSources(void*)> npc_motions;
    ActionCatalog::ReadVisual visuals;
    ActionCatalog::ReadReadyDefaults ready_defaults;
    ActionCatalog::BeginAbility begin_abilities;
    std::function<api::CharacterHitInput(const std::string&)> hit_character;
    std::function<api::HitAnimationDefaults()> hit_weights;
    std::function<api::VisualReactionInput()> hit_names;
    std::shared_ptr<ActionSourceStore> source_store;
    std::function<api::CharacterSkinCatalog()> skins;
    std::function<std::shared_ptr<api::CharacterSkin>(std::uint32_t)> prepare_skin;
    std::function<api::TextInput()> text_language;
    std::function<api::CharacterDisplayName(api::CharacterNameKind, std::uint32_t)> display_name;
};
// Official-thread coordinator. Construction and selection do not call the
// engine. UI receives immutable values, never game objects or query callbacks.
class CatalogBrowser {
  public:
    explicit CatalogBrowser(CatalogReaders readers, std::function<void(const CatalogWork&)> report = {});
    void Request(bool refresh = false);
    std::uint64_t Prepare(BrowseSource source);
    // Retry creates a new request; existing results/plans never change under
    // another controller. Release cancels queued work and forgets its result.
    std::uint64_t RetrySource(std::uint64_t request);
    void ReleaseSource(std::uint64_t request);
    void Advance(std::uint32_t budget_microseconds = 2000,
                 std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    void BatchSources(const std::function<void()>& update);
    bool CancelPending() {
        return actions_.CancelPending();
    }
    void RefreshDocument(const ActionSourceKey& source);
    void RefreshAbility(const std::string& ability);
    void InspectActions(std::uint64_t character, std::vector<ActionSourceRoot> roots);
    bool InspectVisuals(const std::vector<ActionSourceRoot>& roots);
    void ForgetCharacter(std::uint64_t character);
    PlacedAction ResolveAction(std::uint64_t character, const CatalogActionSelection& selection) const;
    std::optional<PoseCommand> ResolvePose(const std::vector<ActionSourceRoot>&, const PoseSelection&);
    std::optional<HitCatalogInputs> ResolveHitInputs(const std::string& source, bool visuals);
    std::optional<api::VisualReactionInput> ResolveDeathNames(const std::string& source);
    CharacterPlan Plan(std::uint64_t request, std::shared_ptr<const ModelAssetInput> payload = {},
                       std::string look = {}) const;
    std::shared_ptr<const CatalogSnapshot> Snapshot() const {
        return snapshot_;
    }
    api::NpcMotionSources NpcMotions(void* owned_component) const;

  private:
    void BeginSource(const BrowseSource& input);
    void AdvanceSource();
    bool AdvanceOne();
    void PollLanguage(std::chrono::steady_clock::time_point now);
    void BeginNameRefresh(const std::string& language);
    bool AdvanceNameRefresh();
    void PublishLocalizationError();
    struct NameRefresh {
        std::string language;
        bool waiting_for_language = false;
        std::shared_ptr<api::CharacterCatalog> rows;
        std::shared_ptr<api::CharacterSkinCatalog> skins;
        std::deque<std::pair<api::CharacterNameKind, std::uint32_t>> pending;
        std::vector<std::string> issues;
    };
    std::optional<NameRefresh> name_refresh_;
    std::string observed_text_language_, language_error_, names_error_;
    std::chrono::steady_clock::time_point next_language_poll_{};
    void Publish();
    void RequestActions();
    bool AdvanceHitInputs();
    bool AdvanceDeathNames();
    std::set<std::string> death_pending_;
    std::uint64_t death_revision_ = 0;
    struct CharacterSources {
        std::uint64_t revision;
        std::vector<ActionSourceRoot> roots;
    };
    std::map<std::uint64_t, CharacterSources> character_sources_;
    std::map<std::uint64_t, std::vector<ActionSourceRoot>> source_roots_;
    std::set<std::string> pose_sources_;
    std::map<std::string, HitCatalogInputs> hit_sources_;
    std::map<std::string, std::string> hit_errors_;
    std::map<std::string, bool> hit_pending_;
    std::shared_ptr<const api::HitAnimationDefaults> hit_weights_;
    std::shared_ptr<const api::VisualReactionInput> hit_names_;
    std::shared_ptr<const api::ReadyAnimationDefaults> hit_animations_;
    std::map<std::uint64_t, std::shared_ptr<const ActionCatalogStatus>> source_views_, character_views_;
    std::deque<std::uint64_t> pending_sources_;
    std::uint64_t revision_ = 0, next_source_ = 0;
    CatalogReaders readers_;
    ActionCatalog actions_;
    std::unique_ptr<SourcePreparation> preparation_;
    // Runtime resources never enter the published CatalogSnapshot.
    std::map<std::uint64_t, PreparedSource> prepared_sources_;
    CatalogSnapshot state_;
    std::shared_ptr<const CatalogSnapshot> snapshot_;
    bool table_pending_ = false, source_turn_ = true, name_turn_ = true, actions_started_ = false;
    bool publication_dirty_ = false;
    unsigned source_batch_ = 0;
    bool sources_dirty_ = false;
    CatalogWork work_;
    std::optional<std::chrono::steady_clock::time_point> work_since_;
    std::function<void(const CatalogWork&)> report_;
};
}
