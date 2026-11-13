#include "Menu.hpp"
#include "MenuLabels.hpp"
#include "Widgets.hpp"
#include <fstream>
#include <iterator>
#include <thread>
#include <cmath>

namespace hksr::framework::ui {
void Menu::Source(ApplicationService& host, const ApplicationSnapshot& snapshot) {
    const auto screen = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(960.0f, screen.x - 32), std::min(700.0f, screen.y - 32)},
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({screen.x * 0.5f, screen.y * 0.5f}, ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    ImGui::SetNextWindowSizeConstraints({std::min(720.0f, screen.x - 32), std::min(530.0f, screen.y - 32)},
                                        {screen.x - 32, screen.y - 32});
    ImGui::SetNextWindowBgAlpha(1.0f);
    if (!ImGui::Begin(L(UiText::CreationTitle), &create_open_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    ImGui::BeginDisabled(!snapshot.prepared || snapshot.closing);
    ImGui::RadioButton(L(UiText::OfficialNpc), &source_kind_, 0);
    ImGui::SameLine();
    ImGui::RadioButton(L(UiText::OfficialPlayer), &source_kind_, 1);
    ImGui::SameLine();
    ImGui::RadioButton(L(UiText::ExternalAdvanced), &source_kind_, 2);
    const CatalogSnapshot* catalog = snapshot.catalog.get();
    const auto* request = catalog ? catalog->Source(source_request_) : nullptr;
    const auto* selected_npc = request ? std::get_if<BrowseNpc>(&request->input) : nullptr;
    const auto* selected_player = request ? std::get_if<BrowsePlayer>(&request->input) : nullptr;
    const auto* selected_skin = request ? std::get_if<BrowseSkin>(&request->input) : nullptr;
    const bool selection_present =
        (!selected_player && !selected_skin) ||
        (catalog && catalog->rows &&
         ((selected_player && std::any_of(catalog->rows->players.rows.begin(), catalog->rows->players.rows.end(),
                                          [&](const auto& row) { return selected_player->key.Matches(row); })) ||
          (selected_skin && catalog->skins &&
           std::any_of(catalog->skins->rows.begin(), catalog->skins->rows.end(),
                       [&](const auto& row) { return row.skin_id == selected_skin->skin_id; }))));
    const bool fits = request && (source_kind_ == 0   ? bool(selected_npc)
                                  : source_kind_ == 1 ? bool(selected_player || selected_skin)
                                                      : std::holds_alternative<CharacterPackage>(request->input));
    const auto groups = catalog && catalog->rows ? GroupPlayerSources(*catalog->rows, catalog->skins.get())
                                                 : std::vector<PlayerSourceGroup>{};
    if (catalog && (listed_rows_ != catalog->rows || listed_skins_ != catalog->skins))
        source_options_request_ = 0;
    auto choose = [&](PlayerAppearanceChoice choice) {
        source_appearance_ = choice;
        if (choice.skin) {
            const auto& skin = catalog->skins->rows[choice.index];
            source_avatar_ = skin.avatar_id;
            source_enhanced_ = 0;
            BeginSourceRequest(host, PrepareSource{BrowseSkin{skin.skin_id}});
        } else {
            const auto& player = catalog->rows->players.rows[choice.index];
            source_avatar_ = player.avatar;
            source_enhanced_ = player.enhanced;
            BeginSourceRequest(host, PrepareSource{BrowsePlayer{{player.id, player.enhanced, player.skin}}});
        }
    };
    if (request && request->id != source_options_request_ && catalog && catalog->rows &&
        (selected_player || selected_skin)) {
        source_options_request_ = request->id;
        source_role_ = 0;
        source_avatar_ = 0;
        source_enhanced_ = 0;
        for (const auto& group : groups) {
            bool found = false;
            for (auto i : group.players)
                if (selected_player && selected_player->key.Matches(catalog->rows->players.rows[i])) {
                    const auto& player = catalog->rows->players.rows[i];
                    source_role_ = group.key;
                    source_avatar_ = player.avatar;
                    source_enhanced_ = player.enhanced;
                    source_appearance_ = {false, i};
                    found = true;
                    break;
                }
            if (!found && selected_skin && catalog->skins)
                for (auto i : group.skins)
                    if (catalog->skins->rows[i].skin_id == selected_skin->skin_id) {
                        source_role_ = group.key;
                        source_avatar_ = catalog->skins->rows[i].avatar_id;
                        source_enhanced_ = 0;
                        source_appearance_ = {true, i};
                        found = true;
                        break;
                    }
            if (found)
                break;
        }
    }
    ImGui::BeginChild("source-content", {0, source_kind_ == 1 ? -310.0f : -190.0f}, ImGuiChildFlags_Borders);
    if (source_kind_ == 2) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##package-path", T(UiText::PackagePath), package_file_, sizeof(package_file_));
        ImGui::BeginDisabled(bool(package_job_));
        if (ImGui::Button(L(UiText::ReadPackage))) {
            auto job = std::make_shared<PackageJob>();
            package_job_ = job;
            const std::string path = package_file_;
            std::thread([job, path] {
                try {
                    const auto file = std::filesystem::u8path(path);
                    std::ifstream input(file, std::ios::binary);
                    if (!input)
                        throw std::runtime_error("cannot open package description");
                    auto package = ParseCharacterPackage(std::string(std::istreambuf_iterator<char>(input), {}));
                    std::shared_ptr<const ModelAssetInput> payload;
                    if (package.source == SourceKind::External)
                        payload =
                            std::make_shared<const ModelAssetInput>(LoadModelPayload(file.parent_path(), package));
                    std::lock_guard<std::mutex> lock(job->mutex);
                    job->package = std::move(package);
                    job->payload = std::move(payload);
                    job->done = true;
                } catch (const std::exception& e) {
                    std::lock_guard<std::mutex> lock(job->mutex);
                    job->error = e.what();
                    job->done = true;
                }
            }).detach();
        }
        ImGui::EndDisabled();
        if (package_job_)
            ImGui::TextUnformatted(T(UiText::ReadingFile));
    } else if (catalog && catalog->rows && ImGui::BeginTable("source-columns", 2, ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn(T(UiText::SourceColumn), ImGuiTableColumnFlags_WidthStretch, 0.65f);
        ImGui::TableSetupColumn(T(UiText::SelectionDetails), ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##source-search", T(UiText::SearchSources), source_filter_, sizeof(source_filter_));
        if (ImGui::SmallButton(L(UiText::RefreshList)))
            host.Post(RequestCatalog{true});
        const auto& rows = *catalog->rows;
        if (listed_rows_ != catalog->rows || listed_skins_ != catalog->skins || listed_kind_ != source_kind_ ||
            listed_filter_ != source_filter_) {
            listed_rows_ = catalog->rows;
            listed_skins_ = catalog->skins;
            listed_kind_ = source_kind_;
            listed_filter_ = source_filter_;
            listed_indices_.clear();
            if (source_kind_ == 0) {
                for (auto i : SourceIndices(rows.npcs.rows, ""))
                    if (SourceMatches(SearchNames(rows.npcs.rows[i]) + " " + SourceLabel(rows.npcs.rows[i]) + " " +
                                          rows.npcs.rows[i].model_config,
                                      source_filter_))
                        listed_indices_.push_back(i);
            } else
                for (std::size_t i = 0; i < groups.size(); ++i) {
                    std::string names;
                    for (auto p : groups[i].players)
                        names += " " + SearchNames(rows.players.rows[p]) + " " +
                                 OfficialNameText(rows.players.rows[p].path_name) + " " +
                                 rows.players.rows[p].character_config;
                    if (catalog->skins)
                        for (auto s : groups[i].skins)
                            names += " " + OfficialNameText(catalog->skins->rows[s].name);
                    if (SourceMatches(names, source_filter_))
                        listed_indices_.push_back(i);
                }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", FormatItemCount(language_, listed_indices_.size()).c_str());
        if (ImGui::BeginListBox("##source-list", {-1, -1})) {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(listed_indices_.size()));
            while (clipper.Step())
                for (int visible = clipper.DisplayStart; visible < clipper.DisplayEnd; ++visible) {
                    const auto i = listed_indices_[visible];
                    ImGui::PushID(static_cast<int>(i));
                    if (source_kind_ == 0) {
                        const auto& p = rows.npcs.rows[i];
                        const auto label = FriendlyName(p, language_) + "###source";
                        if (ImGui::Selectable(label.c_str(), selected_npc && selected_npc->id == p.id))
                            BeginSourceRequest(host, PrepareSource{BrowseNpc{p.id}});
                        if (ImGui::IsItemHovered()) {
                            BeginHelp();
                            Text(p.character_config);
                            Text(p.model_config);
                            EndHelp();
                        }
                    } else {
                        const auto& group = groups[i];
                        const auto& player = rows.players.rows[group.players.front()];
                        const auto label = FriendlyName(player, language_) + "###role-source";
                        if (ImGui::Selectable(label.c_str(), source_role_ == group.key)) {
                            source_role_ = group.key;
                            const auto forms = PlayerForms(group, rows);
                            source_avatar_ = forms.front();
                            const auto enhancements = PlayerEnhancements(group, rows, source_avatar_);
                            source_enhanced_ = enhancements.front();
                            choose(
                                PlayerAppearances(group, rows, catalog->skins.get(), source_avatar_, source_enhanced_)
                                    .front());
                        }
                    }
                    ImGui::PopID();
                }
            ImGui::EndListBox();
        }
        ImGui::TableNextColumn();
        if (fits && selection_present) {
            const auto& source = request->result;
            if (source.character)
                Text(ActorName(*source.character, &rows, language_, selected_skin ? selected_skin->skin_id : 0,
                               catalog->skins.get()));
            if (request->Pending())
                Text(T(UiText::PreparingSource));
            Text(request->error);
            for (const auto& error : source.errors)
                Text(error);
#if HKSR_DEVELOPMENT_UI
            if (ImGui::CollapsingHeader(L(UiText::TechnicalDetails))) {
                if (source.character)
                    Text(source.character->display_name);
            }
#endif
            if (ImGui::SmallButton(L(UiText::RetryPreparation)))
                BeginSourceRequest(host, RetrySource{source_request_}, payload_);
        } else
            Text(T(UiText::SelectSourceHint));
        ImGui::EndTable();
    } else
        Text(T(UiText::ReadingCatalog));
    ImGui::EndChild();
    if (source_kind_ == 1 && catalog && catalog->rows) {
        const auto group =
            std::find_if(groups.begin(), groups.end(), [&](const auto& row) { return row.key == source_role_; });
        if (group != groups.end()) {
            const auto& rows = *catalog->rows;
            auto representative = [&](std::uint32_t avatar) -> const api::PlayerCatalogRow& {
                for (auto i : group->players)
                    if (rows.players.rows[i].avatar == avatar)
                        return rows.players.rows[i];
                return rows.players.rows[group->players.front()];
            };
            const auto forms = PlayerForms(*group, rows);
            auto form_name = [&](std::uint32_t avatar) {
                const auto& row = representative(avatar);
                return PlayerFormName(row, language_, PlayerGenderDistinct(row, rows));
            };
            ImGui::SetNextItemWidth(340);
            if (forms.size() > 1 && BeginChoices(L(UiText::CharacterForm), form_name(source_avatar_).c_str())) {
                for (auto avatar : forms) {
                    ImGui::PushID(static_cast<int>(avatar));
                    if (ImGui::Selectable(form_name(avatar).c_str(), source_avatar_ == avatar)) {
                        source_avatar_ = avatar;
                        source_enhanced_ = PlayerEnhancements(*group, rows, avatar).front();
                        choose(PlayerAppearances(*group, rows, catalog->skins.get(), source_avatar_, source_enhanced_)
                                   .front());
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            const auto enhancements = PlayerEnhancements(*group, rows, source_avatar_);
            if (enhancements.size() > 1) {
                ImGui::SetNextItemWidth(340);
                if (BeginChoices(L(UiText::Enhancement),
                                 T(source_enhanced_ ? UiText::EnhancedVariant : UiText::StandardVariant))) {
                    for (auto enhanced : enhancements) {
                        ImGui::PushID(static_cast<int>(enhanced));
                        if (ImGui::Selectable(T(enhanced ? UiText::EnhancedVariant : UiText::StandardVariant),
                                              source_enhanced_ == enhanced)) {
                            source_enhanced_ = enhanced;
                            choose(
                                PlayerAppearances(*group, rows, catalog->skins.get(), source_avatar_, source_enhanced_)
                                    .front());
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
            }
            const auto appearances =
                PlayerAppearances(*group, rows, catalog->skins.get(), source_avatar_, source_enhanced_);
            auto appearance_name = [&](PlayerAppearanceChoice choice) {
                if (choice.skin) {
                    const auto name = OfficialNameText(catalog->skins->rows[choice.index].name);
                    return name.empty() ? std::string(T(UiText::SkinAppearance)) : name;
                }
                const auto name = OfficialNameText(rows.players.rows[choice.index].default_skin_name);
                return name.empty() ? std::string(T(UiText::DefaultAppearance)) : name;
            };
            if (!appearances.empty()) {
                // Refresh may reorder rows; select a valid local default rather
                // than retain an out-of-range or incompatible appearance.
                if (std::find(appearances.begin(), appearances.end(), source_appearance_) == appearances.end())
                    choose(appearances.front());
                ImGui::SetNextItemWidth(340);
                if (appearances.size() > 1 &&
                    BeginChoices(L(UiText::SkinAppearance), appearance_name(source_appearance_).c_str())) {
                    for (std::size_t i = 0; i < appearances.size(); ++i) {
                        ImGui::PushID(static_cast<int>(i));
                        if (ImGui::Selectable(appearance_name(appearances[i]).c_str(),
                                              source_appearance_ == appearances[i]))
                            choose(appearances[i]);
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
            }
        }
    }
    if (!message_.empty())
        Text(message_);
    Text(catalog ? catalog->table_error : std::string{});
    if (source_kind_ == 1 && catalog && catalog->skins)
        for (const auto& error : catalog->skins->errors)
            Text(error);
    ImGui::Checkbox(L(UiText::NearPlayer), &near_player_);
    ImGui::SetNextItemWidth(280);
    if (near_player_)
        ImGui::InputFloat3(L(UiText::WorldOffset), player_offset_);
    else {
        ImGui::InputFloat3(L(UiText::InitialPosition), position_);
        ImGui::SetNextItemWidth(160);
        ImGui::InputFloat(L(UiText::YawDegrees), &yaw_);
    }
    const bool ready = fits && selection_present && request->id == source_request_ &&
                       request->phase == SourceRequestPhase::Ready && snapshot.scene;
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button(L(UiText::CreateInstance), {140, 0})) {
        const auto half = yaw_ * 0.00872664626f;
        CharacterPlacement placement{{position_[0], position_[1], position_[2]},
                                     {0, std::sin(half), 0, std::cos(half)}};
        if (request->result.character->source == SourceKind::External && !payload_)
            message_ = T(UiText::ReloadExternal);
        else
            create_serial_ = host.Post(AddPreparedCharacter{source_request_,
                                                            "",
                                                            {},
                                                            placement,
                                                            payload_,
                                                            {},
                                                            near_player_,
                                                            {player_offset_[0], player_offset_[1], player_offset_[2]}});
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(L(UiText::Cancel)))
        create_open_ = false;
    if (!snapshot.scene)
        ImGui::TextWrapped("%s", T(UiText::WaitingScene));
    ImGui::EndDisabled();
    ImGui::End();
}
}
