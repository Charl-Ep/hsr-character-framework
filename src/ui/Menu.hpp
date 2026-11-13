#pragma once
#include <hksr/framework/application/ApplicationService.hpp>
#include <mutex>
#include <set>
#include "Localization.hpp"
#include "TaskInputDraft.hpp"
#include <hksr/framework/catalog/PlayerSourceGroups.hpp>
#include "ActionMenuState.hpp"

namespace hksr::framework::ui {
enum class MenuPage { Actions, Controls, Diagnostics };
class Menu {
  public:
    explicit Menu(MenuPage page = MenuPage::Actions, Language language = Language::English)
        : page_(page), language_(language) {}
    void SetLanguage(Language language) {
        language_ = language;
    }
    Language GetLanguage() const {
        return language_;
    }
    void FollowGameLanguage(std::string_view language) {
        language_ = GameLanguage(language);
    }
    void SettingsSaved(bool saved) {
        settings_error_ = !saved;
    }
    void Draw(ApplicationService& host);
    void Draw(ApplicationService& host, const ApplicationSnapshot& snapshot);
    // Value-only view also used by local layout previews. No game queries here.
    void Render(ApplicationService& host, const ApplicationSnapshot& snapshot);
    void OpenCreation(std::uint64_t source_request = 0) {
        create_open_ = true;
        source_request_ = source_request;
    }
    void OpenLayouts() {
        layouts_open_ = true;
    }
    void OpenBattleStates() {
        page_ = MenuPage::Actions;
#if HKSR_DEVELOPMENT_UI
        action_view_ = ActionView::Preview;
#else
        action_view_ = ActionView::Battle;
#endif
    }
    void OpenTaskInputs(CharacterTaskEntry entry) {
        if (HasTaskVariants(entry)) {
            task_inputs_.reset();
            variant_inputs_ = VariantInputsState{actor_, action_generation_, ActionRowIdentity(entry)};
        } else {
            variant_inputs_.reset();
            task_inputs_.emplace(task_settings_.Apply(std::move(entry)));
        }
    }

  private:
    struct PackageJob {
        std::mutex mutex;
        bool done = false;
        std::optional<CharacterPackage> package;
        std::shared_ptr<const ModelAssetInput> payload;
        std::string error;
    };
    void Source(ApplicationService& host, const ApplicationSnapshot& snapshot);
    void Actions(ApplicationService& host, const ApplicationSnapshot& snapshot, const SceneCharacterStatus& selected);
    void TaskInputs(ApplicationService& host, const ApplicationSnapshot& snapshot);
    void VariantInputs(const ApplicationSnapshot& snapshot);
    void Diagnostics(ApplicationService& host, const ApplicationSnapshot& snapshot,
                     const SceneCharacterStatus& selected);
    void Controls(ApplicationService& host, const ApplicationSnapshot& snapshot, const SceneCharacterStatus& selected);
    void SelectActor(ApplicationService& host, std::uint64_t id);
    void Layouts(ApplicationService& host, const ApplicationSnapshot& snapshot);
    void BeginSourceRequest(ApplicationService& host, CharacterCommand command,
                            std::shared_ptr<const ModelAssetInput> payload = {});
    MenuPage page_ = MenuPage::Actions;
    Language language_ = Language::English;
    bool settings_error_ = false;
    const char* T(UiText key) const {
        return Translate(key, language_);
    }
    const char* L(UiText key) const {
        return LocalizedLabel(key, language_);
    }
    bool create_open_ = false;
    char source_filter_[256]{}, actor_filter_[128]{}, action_filter_[256]{}, package_file_[2048]{};
    std::shared_ptr<const api::CharacterCatalog> listed_rows_;
    std::shared_ptr<const api::CharacterSkinCatalog> listed_skins_;
    std::string listed_filter_;
    int listed_kind_ = -1;
    std::vector<std::size_t> listed_indices_;
    std::uint64_t source_role_ = 0;
    std::uint64_t source_options_request_ = 0;
    std::uint32_t source_avatar_ = 0, source_enhanced_ = 0;
    PlayerAppearanceChoice source_appearance_;
    int source_kind_ = 1;
    ActionView action_view_ = ActionView::Exploration;
    std::uint64_t action_actor_ = 0, action_generation_ = 0;
    float position_[3]{}, yaw_ = 0;
    bool near_player_ = true;
    float player_offset_[3]{2, 0, 0};
    std::uint64_t actor_ = 0, queried_creation_ = 0, source_serial_ = 0, source_request_ = 0, create_serial_ = 0;
    std::set<std::uint64_t> source_replies_;
    bool requested_ = false;
    bool show_unavailable_tasks_ = false;
    bool show_technical_inputs_ = false;
    std::optional<TaskInputDraft> task_inputs_;
    TaskInputSettings task_settings_;
    struct VariantInputsState {
        std::uint64_t actor, creation;
        std::string row;
    };
    std::optional<VariantInputsState> variant_inputs_;
    ActionVariantSettings variant_settings_;
    std::shared_ptr<PackageJob> package_job_;
    std::shared_ptr<const ModelAssetInput> payload_;
    std::string message_;
    std::uint64_t edited_actor_ = 0, look_target_ = 0;
    bool pose_dirty_ = false;
    float edit_position_[3]{}, edit_yaw_ = 0, move_step_ = 0.1f, turn_step_ = 5.0f, look_point_[3]{};
    int look_mode_ = 0;
    bool look_limit_ = false;
    float look_distance_ = 10;
    std::optional<LookIntent> look_pending_;
    std::uint64_t look_serial_ = 0;
    bool layouts_open_ = false;
    char layout_name_[256]{};
    std::string layout_scene_, layout_selected_, layout_confirm_name_;
    LayoutOperation layout_confirm_ = LayoutOperation::Load;
};
}
