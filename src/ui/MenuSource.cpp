#include "Menu.hpp"
#include "MenuLabels.hpp"
#include "Widgets.hpp"
#include <fstream>
#include <iterator>
#include <thread>
#include <cmath>

namespace hksr::framework::ui {
void Menu::Source(ApplicationService& host,const ApplicationSnapshot& snapshot) {
    const auto screen=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(960.0f,screen.x-32),std::min(700.0f,screen.y-32)},ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({screen.x*0.5f,screen.y*0.5f},ImGuiCond_FirstUseEver,{0.5f,0.5f});
    ImGui::SetNextWindowSizeConstraints({std::min(720.0f,screen.x-32),std::min(530.0f,screen.y-32)},{screen.x-32,screen.y-32});
    ImGui::SetNextWindowBgAlpha(1.0f);
    if(!ImGui::Begin(L(UiText::CreationTitle),&create_open_,ImGuiWindowFlags_NoCollapse)){ImGui::End();return;}
    ImGui::BeginDisabled(!snapshot.prepared || snapshot.closing);
    ImGui::RadioButton(L(UiText::OfficialNpc),&source_kind_,0);ImGui::SameLine();
    ImGui::RadioButton(L(UiText::OfficialPlayer),&source_kind_,1);ImGui::SameLine();
    ImGui::RadioButton(L(UiText::ExternalAdvanced),&source_kind_,2);
    const CatalogSnapshot* catalog=snapshot.catalog.get();
    const auto* request=catalog?catalog->Source(source_request_):nullptr;
    const auto* selected_npc=request?std::get_if<BrowseNpc>(&request->input):nullptr;
    const auto* selected_player=request?std::get_if<BrowsePlayer>(&request->input):nullptr;
    const bool fits=request && (source_kind_==0?bool(selected_npc):source_kind_==1?bool(selected_player):
        std::holds_alternative<CharacterPackage>(request->input));
    ImGui::BeginChild("source-content",{0,-190},ImGuiChildFlags_Borders);
    if(source_kind_==2) {
        ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##package-path",T(UiText::PackagePath),package_file_,sizeof(package_file_));
        ImGui::BeginDisabled(bool(package_job_));
        if(ImGui::Button(L(UiText::ReadPackage))) {
            auto job=std::make_shared<PackageJob>();package_job_=job;const std::string path=package_file_;
            std::thread([job,path] {
                try {
                    const auto file=std::filesystem::u8path(path);std::ifstream input(file,std::ios::binary);
                    if(!input)throw std::runtime_error("cannot open package description");
                    auto package=ParseCharacterPackage(std::string(std::istreambuf_iterator<char>(input),{}));
                    std::shared_ptr<const ModelAssetInput> payload;
                    if(package.source==SourceKind::External)payload=std::make_shared<const ModelAssetInput>(LoadModelPayload(file.parent_path(),package));
                    std::lock_guard<std::mutex> lock(job->mutex);job->package=std::move(package);job->payload=std::move(payload);job->done=true;
                } catch(const std::exception& e){std::lock_guard<std::mutex> lock(job->mutex);job->error=e.what();job->done=true;}
            }).detach();
        }
        ImGui::EndDisabled();if(package_job_)ImGui::TextUnformatted(T(UiText::ReadingFile));
    } else if(catalog && catalog->rows && ImGui::BeginTable("source-columns",2,ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn(T(UiText::SourceColumn),ImGuiTableColumnFlags_WidthStretch,0.65f);
        ImGui::TableSetupColumn(T(UiText::SelectionDetails),ImGuiTableColumnFlags_WidthStretch,0.35f);
        ImGui::TableNextRow();ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##source-search",T(UiText::SearchSources),source_filter_,sizeof(source_filter_));
        if(source_kind_==1)ImGui::Checkbox(L(UiText::ShowEnhanced),&advanced_sources_);
        if(ImGui::SmallButton(L(UiText::RefreshList)))host.Post(RequestCatalog{true});
        const auto& rows=*catalog->rows;
        if(listed_rows_!=catalog->rows || listed_kind_!=source_kind_ || listed_filter_!=source_filter_ || listed_advanced_!=advanced_sources_) {
            listed_rows_=catalog->rows;listed_kind_=source_kind_;listed_filter_=source_filter_;listed_advanced_=advanced_sources_;
            listed_indices_.clear();
            if(source_kind_==0) {
                for(auto i:SourceIndices(rows.npcs.rows,""))
                    if(SourceMatches(SearchNames(rows.npcs.rows[i])+" "+SourceLabel(rows.npcs.rows[i])+" "+rows.npcs.rows[i].model_config,source_filter_))listed_indices_.push_back(i);
            } else for(auto i:SourceIndices(rows.players.rows,"")) {
                const auto& p=rows.players.rows[i];
                if((advanced_sources_ || !p.enhanced) && SourceMatches(SearchNames(p)+" "+SourceLabel(p)+" "+p.prefab,source_filter_))listed_indices_.push_back(i);
            }
        }
        ImGui::SameLine();ImGui::TextDisabled("%s",FormatItemCount(language_,listed_indices_.size()).c_str());
        if(ImGui::BeginListBox("##source-list",{-1,-1})) {
            ImGuiListClipper clipper;clipper.Begin(static_cast<int>(listed_indices_.size()));
            while(clipper.Step())for(int visible=clipper.DisplayStart;visible<clipper.DisplayEnd;++visible) {
                const auto i=listed_indices_[visible];ImGui::PushID(static_cast<int>(i));
                if(source_kind_==0) {
                    const auto& p=rows.npcs.rows[i];
                    const auto label=FriendlyName(p,language_)+"###source";
                    if(ImGui::Selectable(label.c_str(),selected_npc && selected_npc->id==p.id))BeginSourceRequest(host,PrepareSource{BrowseNpc{p.id}});
                    if(ImGui::IsItemHovered()){BeginHelp();Text(p.character_config);Text(p.model_config);EndHelp();}
                } else {
                    const auto& p=rows.players.rows[i];
                    const auto label=FriendlyName(p,language_)+(p.enhanced?T(UiText::EnhancedSuffix)+std::to_string(p.enhanced):"")+(p.skin?T(UiText::SkinSuffix)+std::to_string(p.skin):"")+"###source";
                    if(ImGui::Selectable(label.c_str(),selected_player && selected_player->key.Matches(p)))
                        BeginSourceRequest(host,PrepareSource{BrowsePlayer{{p.id,p.enhanced,p.skin},{}}});
                    if(ImGui::IsItemHovered()){BeginHelp();Text(p.character_config);Text(p.prefab);EndHelp();}
                }
                ImGui::PopID();
            }
            ImGui::EndListBox();
        }
        ImGui::TableNextColumn();
        if(fits) {
            const auto& source=request->result;
            if(source.character)Text(ActorName(*source.character,&rows,language_));
            if(request->Pending())Text(T(UiText::PreparingSource));
            if(source.foundation.status==SourceResolution::Ambiguous && selected_player) {
                Text(T(UiText::ChooseFoundation));
                for(auto id:source.foundation.candidates)for(const auto& row:rows.npcs.rows)if(row.id==id) {
                    if(ImGui::Selectable((ResourceName(row.model_config)+"###foundation-"+std::to_string(id)).c_str()))
                        BeginSourceRequest(host,PrepareSource{BrowsePlayer{selected_player->key,id}});
                }
            }
            Text(request->error);
            for(const auto& error:source.errors)Text(error);
            for(const auto& error:source.foundation.errors)Text(error);
            if(ImGui::CollapsingHeader(L(UiText::TechnicalDetails))) {
                for(const auto& note:source.foundation.notes)Text(note);
                if(source.character)Text(source.character->display_name);
            }
            if(ImGui::SmallButton(L(UiText::RetryPreparation)))BeginSourceRequest(host,RetrySource{source_request_},payload_);
        } else Text(T(UiText::SelectSourceHint));
        ImGui::EndTable();
    } else Text(T(UiText::ReadingCatalog));
    ImGui::EndChild();
    if(!message_.empty())Text(message_);
    Text(catalog?catalog->table_error:std::string{});
    ImGui::Checkbox(L(UiText::NearPlayer),&near_player_);
    ImGui::SetNextItemWidth(280);
    if(near_player_)ImGui::InputFloat3(L(UiText::WorldOffset),player_offset_);
    else {
        ImGui::InputFloat3(L(UiText::InitialPosition),position_);
        ImGui::SetNextItemWidth(160);ImGui::InputFloat(L(UiText::YawDegrees),&yaw_);
    }
    const bool ready=fits && request->id==source_request_ && request->phase==SourceRequestPhase::Ready && snapshot.scene;
    ImGui::BeginDisabled(!ready);
    if(ImGui::Button(L(UiText::CreateInstance),{140,0})) {
        const auto half=yaw_*0.00872664626f;
        CharacterPlacement placement{{position_[0],position_[1],position_[2]},{0,std::sin(half),0,std::cos(half)}};
        if(request->result.character->source==SourceKind::External && !payload_)message_=T(UiText::ReloadExternal);
        else create_serial_=host.Post(AddPreparedCharacter{source_request_,"",{},placement,payload_,{},near_player_,{player_offset_[0],player_offset_[1],player_offset_[2]}});
    }
    ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button(L(UiText::Cancel)))create_open_=false;
    if(!snapshot.scene)ImGui::TextWrapped("%s",T(UiText::WaitingScene));
    ImGui::EndDisabled();ImGui::End();
}
}
