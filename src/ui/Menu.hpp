#pragma once
#include <hksr/framework/application/ApplicationService.hpp>
#include <mutex>
#include <set>
#include "Localization.hpp"
#include "TaskInputDraft.hpp"

namespace hksr::framework::ui {
enum class MenuPage { Actions, Controls, Diagnostics };
class Menu {
public:
    explicit Menu(MenuPage page=MenuPage::Actions,Language language=DefaultLocale):page_(page),language_(language){}
    void SetLanguage(Language language,bool save=true) {language_=language;if(save)language_changed_=true;}
    Language GetLanguage() const {return language_;}
    std::optional<Language> TakeLanguageChange() {if(!language_changed_)return {};language_changed_=false;return language_;}
    void SettingsSaved(bool saved) {settings_error_=!saved;}
    void Draw(ApplicationService& host);
    // Value-only view also used by local layout previews. No game queries here.
    void Render(ApplicationService& host,const ApplicationSnapshot& snapshot);
    void OpenCreation(std::uint64_t source_request=0) {create_open_=true;source_request_=source_request;}
    void OpenLayouts() {layouts_open_=true;}
    void OpenBattleStates() {page_=MenuPage::Actions;action_kind_=1;}
    void OpenTaskInputs(CharacterTaskEntry entry) {task_inputs_.emplace(std::move(entry));}
private:
    struct PackageJob {
        std::mutex mutex;
        bool done=false;
        std::optional<CharacterPackage> package;
        std::shared_ptr<const ModelAssetInput> payload;
        std::string error;
    };
    void Source(ApplicationService& host,const ApplicationSnapshot& snapshot);
    void Actions(ApplicationService& host,const ApplicationSnapshot& snapshot,const SceneCharacterStatus& selected);
    void TaskInputs(ApplicationService& host,const ApplicationSnapshot& snapshot);
    void Diagnostics(ApplicationService& host,const ApplicationSnapshot& snapshot,const SceneCharacterStatus& selected);
    void Controls(ApplicationService& host,const ApplicationSnapshot& snapshot,const SceneCharacterStatus& selected);
    void SelectActor(ApplicationService& host,std::uint64_t id);
    void Layouts(ApplicationService& host,const ApplicationSnapshot& snapshot);
    void BeginSourceRequest(ApplicationService& host,CharacterCommand command,std::shared_ptr<const ModelAssetInput> payload={});
    MenuPage page_=MenuPage::Actions;
    Language language_=DefaultLocale;
    bool language_changed_=false,settings_error_=false;
    const char* T(UiText key) const {return Translate(key,language_);}
    const char* L(UiText key) const {return LocalizedLabel(key,language_);}
    bool create_open_=false,advanced_sources_=false;
    char source_filter_[256]{},actor_filter_[128]{},action_filter_[256]{},package_file_[2048]{};
    std::shared_ptr<const api::CharacterCatalog> listed_rows_;
    std::string listed_filter_;
    int listed_kind_=-1;
    bool listed_advanced_=false;
    std::vector<std::size_t> listed_indices_;
    int source_kind_=1,action_kind_=0,action_domain_=0;
    std::uint64_t action_actor_=0,action_generation_=0;
    float position_[3]{},yaw_=0;
    bool near_player_=true;
    float player_offset_[3]{2,0,0};
    std::uint64_t actor_=0,queried_creation_=0,source_serial_=0,source_request_=0,create_serial_=0;
    std::set<std::uint64_t> source_replies_;
    bool requested_=false;
    bool show_unavailable_tasks_=false;
    std::optional<TaskInputDraft> task_inputs_;
    std::shared_ptr<PackageJob> package_job_;
    std::shared_ptr<const ModelAssetInput> payload_;
    std::string message_;
    std::uint64_t edited_actor_=0,look_target_=0;
    bool pose_dirty_=false;
    float edit_position_[3]{},edit_yaw_=0,move_step_=0.1f,turn_step_=5.0f,look_point_[3]{};
    int look_mode_=0;
    bool look_limit_=false;float look_distance_=10;
    std::optional<LookIntent> look_pending_;
    std::uint64_t look_serial_=0;
    bool layouts_open_=false;
    char layout_name_[256]{};
    std::string layout_scene_,layout_selected_,layout_confirm_name_;
    LayoutOperation layout_confirm_=LayoutOperation::Load;
};
}
