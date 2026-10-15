#include "Menu.hpp"
#include "MenuLabels.hpp"
#include "Widgets.hpp"

namespace hksr::framework::ui {
void Menu::Layouts(ApplicationService& host,const ApplicationSnapshot& snapshot) {
    ImGui::SetNextWindowSize({600,480},ImGuiCond_FirstUseEver);
    if(ImGui::Begin(L(UiText::MapLayouts),&layouts_open_)) {
        if(!snapshot.scene || !snapshot.layouts.available)Text(T(UiText::LayoutUnavailable));
        else {
            const auto& scene=snapshot.scene->key;
            if(layout_scene_!=scene){layout_scene_=scene;layout_selected_.clear();layout_confirm_name_.clear();}
            const auto found=snapshot.layouts.maps.find(scene);
            if(found!=snapshot.layouts.maps.end()) {
                const auto& state=found->second;
                if(!state.busy && std::find(state.names.begin(),state.names.end(),layout_selected_)==state.names.end())layout_selected_.clear();
                UiText phase=UiText::LayoutReady;
                if(state.phase=="reading" || state.phase=="io")phase=UiText::LayoutReading;
                else if(state.phase=="preparing")phase=UiText::LayoutPreparing;
                else if(state.phase=="removing")phase=UiText::LayoutRemoving;
                else if(state.phase=="applying")phase=UiText::LayoutApplying;
                else if(state.phase=="failed")phase=UiText::LayoutFailed;
                else if(state.phase=="cancelled")phase=UiText::LayoutCancelled;
                if(state.busy || !state.error.empty())Text(T(phase));
                if(!state.error.empty())Text(state.error);
                if(!state.storage_error.empty())Text(state.storage_error);
                if(state.initialized)Text(T(state.revision==state.saved_revision && state.storage_error.empty()?UiText::LayoutSaved:UiText::LayoutUnsaved));
                auto post=[&](LayoutOperation op,const std::string& name={},bool overwrite=false){host.Post(LayoutCommand{op,name,overwrite,scene});};
                auto confirm=[&](LayoutOperation op,const std::string& name){layout_confirm_=op;layout_confirm_name_=name;ImGui::OpenPopup(L(UiText::LayoutConfirm));};
                ImGui::BeginDisabled(state.busy || snapshot.closing);
                if(!state.initialized || !state.storage_error.empty()) {
                    if(ImGui::Button(L(UiText::LayoutRetry)))post(LayoutOperation::RetryCurrent);
                    if(!state.initialized){ImGui::SameLine();if(ImGui::Button(L(UiText::LayoutKeep)))confirm(LayoutOperation::KeepCurrent,scene);}
                }
                ImGui::Separator();ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##layout-name",T(UiText::LayoutName),layout_name_,sizeof(layout_name_));
                ImGui::BeginDisabled(!layout_name_[0]);
                if(ImGui::Button(L(UiText::LayoutSave))) {
                    if(std::find(state.names.begin(),state.names.end(),layout_name_)!=state.names.end())confirm(LayoutOperation::Save,layout_name_);
                    else post(LayoutOperation::Save,layout_name_);
                }
                ImGui::EndDisabled();ImGui::SameLine();
                if(ImGui::Button(L(UiText::LayoutRefresh)))post(LayoutOperation::Refresh);
                if(ImGui::BeginListBox("##snapshots",{-1,140})) {
                    for(const auto& name:state.names)if(ImGui::Selectable(name.c_str(),name==layout_selected_))layout_selected_=name;
                    ImGui::EndListBox();
                }
                ImGui::BeginDisabled(layout_selected_.empty());
                if(ImGui::Button(L(UiText::LayoutLoad)))confirm(LayoutOperation::Load,layout_selected_);
                ImGui::SameLine();if(ImGui::Button(L(UiText::LayoutDelete)))post(LayoutOperation::Delete,layout_selected_);
                ImGui::EndDisabled();ImGui::EndDisabled();
                ImGui::SetNextWindowSize({500,0},ImGuiCond_Always);
                if(ImGui::BeginPopupModal(L(UiText::LayoutConfirm),nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
                    Text(layout_confirm_name_);
                    const auto help=layout_confirm_==LayoutOperation::Load?UiText::LayoutLoadHelp:
                        layout_confirm_==LayoutOperation::Save?UiText::LayoutOverwriteHelp:
                        layout_confirm_==LayoutOperation::Delete?UiText::LayoutDeleteHelp:UiText::LayoutKeepHelp;
                    ImGui::TextWrapped("%s",T(help));
                    ImGui::BeginDisabled(state.busy || layout_confirm_name_.empty() || snapshot.closing);
                    if(ImGui::Button(L(UiText::LayoutProceed))){post(layout_confirm_,layout_confirm_name_,layout_confirm_==LayoutOperation::Save);ImGui::CloseCurrentPopup();}
                    ImGui::EndDisabled();ImGui::SameLine();
                    if(ImGui::Button(L(UiText::Cancel)))ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
            }
        }
    }
    ImGui::End();
}
}
