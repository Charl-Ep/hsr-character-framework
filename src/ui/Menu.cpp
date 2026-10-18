#include "Menu.hpp"
#include "MenuLabels.hpp"
#include "Widgets.hpp"
#include "CharacterStatus.hpp"

namespace hksr::framework::ui {
void Menu::SelectActor(ApplicationService& host,std::uint64_t id) {
    actor_=id;queried_creation_=0;host.Post(ObserveCharacter{id});host.Post(BrowseCharacterActions{id});
}
void Menu::BeginSourceRequest(ApplicationService& host,CharacterCommand command,std::shared_ptr<const ModelAssetInput> payload) {
    source_serial_=host.Post(std::move(command));source_replies_.insert(source_serial_);
    if(source_request_)host.Post(ReleaseSource{source_request_});
    source_request_=0;payload_=std::move(payload);
}
void Menu::Draw(ApplicationService& host) {
    for(const auto& reply:host.TakeReplies()) {
        if(source_replies_.erase(reply.serial)) {
            if(reply.serial==source_serial_) {
                if(reply.accepted)source_request_=reply.source_request;else message_=reply.error;
            } else if(reply.accepted)host.Post(ReleaseSource{reply.source_request});
        }
        else if(!reply.accepted)message_=reply.error;
        else if(reply.serial==create_serial_) {SelectActor(host,reply.character);create_open_=false;}
        if(reply.serial==look_serial_ && !reply.accepted)look_pending_.reset();
    }
    if(package_job_) {
        auto job=package_job_;std::lock_guard<std::mutex> lock(job->mutex);
        if(job->done) {
            package_job_.reset();
            if(job->package)BeginSourceRequest(host,PrepareSource{*job->package},job->payload);
            else message_=job->error;
        }
    }
    Render(host,host.Snapshot());
}
void Menu::Render(ApplicationService& host,const ApplicationSnapshot& snapshot) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{12,12});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{8,5});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{8,6});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,5.0f);
    const auto display=ImGui::GetIO().DisplaySize;
    const ImVec2 max_size{std::max(480.0f,display.x-32),std::max(360.0f,display.y-32)};
    ImGui::SetNextWindowPos({16,16},ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({std::min(1080.0f,max_size.x),std::min(760.0f,max_size.y)},ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({std::min(740.0f,max_size.x),std::min(500.0f,max_size.y)},max_size);
    if(ImGui::Begin(L(UiText::WindowTitle))) {
        ImGui::TextUnformatted(T(UiText::Title));ImGui::SameLine();ImGui::TextDisabled("%s",T(UiText::ToggleHint));
        ImGui::SameLine();ImGui::SetNextItemWidth(std::clamp(ImGui::CalcTextSize(DescribeLanguage(language_).name).x+40.0f,112.0f,240.0f));
        if(BeginChoices(L(UiText::Language),DescribeLanguage(language_).name)) {
            for(const auto& language:AvailableLanguages()) {
                ImGui::PushID(language.code);
                if(ImGui::Selectable(language.name,language_==language.value))SetLanguage(language.value);
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if(settings_error_)Text(T(UiText::SettingsNotSaved));
        if(ImGui::Button(L(UiText::MapLayouts)))layouts_open_=true;
        if(!snapshot.prepared)ImGui::TextUnformatted(T(UiText::Connecting));
        if(snapshot.closing)ImGui::TextUnformatted(T(UiText::Closing));
        if(!message_.empty()) {Text(message_);if(ImGui::SmallButton(L(UiText::ClearError)))message_.clear();}
        if(!snapshot.map_error.empty())Text(snapshot.map_error);
        ImGui::BeginDisabled(!snapshot.prepared || snapshot.closing);
        if(snapshot.prepared && !snapshot.closing && !requested_){host.Post(RequestCatalog{});requested_=true;}
        if(actor_ && std::none_of(snapshot.characters.begin(),snapshot.characters.end(),
            [&](const auto& row){return row.id==actor_ && !row.deleting;}))actor_=0;
        if(snapshot.prepared && !actor_)for(const auto& row:snapshot.characters)if(!row.deleting){SelectActor(host,row.id);break;}
        const float sidebar=std::min(260.0f,ImGui::GetContentRegionAvail().x*0.30f);
        ImGui::BeginChild("instances",{sidebar,0},ImGuiChildFlags_Borders);
        if(ImGui::Button(L(UiText::NewCharacter),{-1,0}))create_open_=true;
        ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##instance-filter",T(UiText::SearchInstances),actor_filter_,sizeof(actor_filter_));
        ImGui::Separator();
        const auto* rows=snapshot.catalog && snapshot.catalog->rows?snapshot.catalog->rows.get():nullptr;
        for(const auto& row:snapshot.characters) {
            if(row.deleting)continue;
            const auto name=ActorName(row.package,rows,language_);
            std::string searchable=std::to_string(row.id);
            for(const auto& language:AvailableLanguages())searchable+=" "+ActorName(row.package,rows,language.value)+" "+SourceMode(row.package,language.value);
            if(!SourceMatches(searchable,actor_filter_))continue;
            ImGui::PushID(static_cast<int>(row.id));
            const auto label=name+"\n"+SourceMode(row.package,language_)+"###instance";
            const float width=std::max(1.0f,ImGui::GetContentRegionAvail().x-30-ImGui::GetStyle().ItemSpacing.x);
            if(ImGui::Selectable(label.c_str(),actor_==row.id,0,{width,ImGui::GetTextLineHeight()*2+10}))SelectActor(host,row.id);
            if(ImGui::IsItemHovered()){BeginHelp();Text(name);Text(SourceMode(row.package,language_));EndHelp();}
            ImGui::SameLine();if(ImGui::SmallButton("x##delete"))host.Post(DeleteCharacter{row.id});
            ImGui::PopID();
        }
        if(snapshot.characters.empty())ImGui::TextWrapped("%s",T(UiText::NoInstances));
        ImGui::EndChild();ImGui::SameLine();
        ImGui::BeginChild("workspace",{0,0});
        const auto found=std::find_if(snapshot.characters.begin(),snapshot.characters.end(),[&](const auto& row){return row.id==actor_;});
        if(found==snapshot.characters.end())ImGui::TextWrapped("%s",T(UiText::SelectInstanceHint));
        else {
            const auto& selected=*found;
            Text(ActorName(selected.package,rows,language_));
            const auto status=CharacterStatus(snapshot,selected,language_);
            if(!status.text.empty()) {
            const float status_width=std::max(1.0f,ImGui::GetContentRegionAvail().x-2*ImGui::GetStyle().WindowPadding.x);
            const float status_height=ImGui::CalcTextSize(status.text.c_str(),nullptr,false,status_width).y+2*ImGui::GetStyle().WindowPadding.y;
            ImGui::BeginChild("character-status",{0,status_height},ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar);
            Text(status.text);ImGui::EndChild();
            }
            Text(status.error);
            if(selected.session) {
                if(selected.active && selected.session->phase==SessionPhase::Active && queried_creation_!=selected.creations) {
                    queried_creation_=selected.creations;
                    if(PureNpc(selected.package))host.Post(ReadCharacterNpcActions{actor_});
                    host.Post(BrowseCharacterActions{actor_});
                }
            }
            ImGui::Separator();
            const std::pair<const char*,MenuPage> tabs[]={{L(UiText::Actions),MenuPage::Actions},{L(UiText::Controls),MenuPage::Controls}
#if HKSR_DEVELOPMENT_UI
                ,{L(UiText::Diagnostics),MenuPage::Diagnostics}
#endif
            };
#if !HKSR_DEVELOPMENT_UI
            if(page_==MenuPage::Diagnostics)page_=MenuPage::Actions;
#endif
            for(const auto& item:tabs) {
                if(item.second!=MenuPage::Actions)ImGui::SameLine();
                const bool selected_page=page_==item.second;
                if(selected_page)ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if(ImGui::Button(item.first))page_=item.second;
                if(selected_page)ImGui::PopStyleColor();
            }
            ImGui::Separator();
            ImGui::BeginChild("page",{0,0});
            if(page_==MenuPage::Actions)Actions(host,snapshot,selected);
            else if(page_==MenuPage::Controls)Controls(host,snapshot,selected);
#if HKSR_DEVELOPMENT_UI
            else Diagnostics(host,snapshot,selected);
#endif
            ImGui::EndChild();
        }
        ImGui::EndChild();ImGui::EndDisabled();
    }
    ImGui::End();
    if(create_open_)Source(host,snapshot);
    if(layouts_open_)Layouts(host,snapshot);
    ImGui::PopStyleVar(4);
}
void Menu::Diagnostics(ApplicationService& host,const ApplicationSnapshot& snapshot,const SceneCharacterStatus& selected) {
    Text(T(UiText::RawDiagnostics));
    Text(selected.look_support);
    Text(FormatCreationCount(language_,selected.creations,selected.retiring));
    for(const auto& error:snapshot.binding_errors)Text(error);
    Text(snapshot.controller_catalog_error);
    if(selected.session)for(const auto& error:selected.session->errors)Text(error);
    if(selected.handoff){Text(selected.handoff->error);Text(selected.handoff->initialization_note);Text(selected.handoff->event_value_note);}
    if(selected.playback) {
        const auto& p=*selected.playback;
        Text(p.completion_note);Text(p.visual_note);Text(p.blink_note);Text(p.ready_note);Text(p.effect_note);
        if(p.last)Text(p.last->detail);
    }
    const auto* actions=snapshot.catalog?snapshot.catalog->Actions(actor_):nullptr;
    if(actions) {
        for(const auto& doc:actions->actions.documents) {
            ImGui::PushID(doc.input.source.path.c_str());Text(doc.input.source.path);
            for(const auto& issue:doc.issues)Text(issue.operation+": "+issue.error);
            if(ImGui::SmallButton(L(UiText::ReloadDocument)))host.Post(RefreshActionDocument{doc.input.source});ImGui::PopID();
        }
        for(const auto& ability:actions->actions.abilities) {
            ImGui::PushID(ability.ability.c_str());Text(ability.ability);
            for(const auto& issue:ability.issues)Text(issue.error);
            if(ImGui::SmallButton(L(UiText::ReloadAbility)))host.Post(RefreshAbilitySource{ability.ability});ImGui::PopID();
        }
    }
}
}
