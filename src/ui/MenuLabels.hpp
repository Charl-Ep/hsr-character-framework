#pragma once
#include <hksr/framework/character/CharacterDirectory.hpp>
#include <hksr/framework/assets/CharacterPackage.hpp>
#include <hksr/framework/catalog/SourceList.hpp>
#include <hksr/framework/catalog/PlayerSourceGroups.hpp>
#include <imgui.h>
#include "Localization.hpp"
#include "OfficialNameText.hpp"

namespace hksr::framework::ui {
inline void Text(const std::string& value) {
    if (!value.empty())
        ImGui::TextWrapped("%s", value.c_str());
}
inline std::string ResourceName(const std::string& path) {
    auto name = SourceFile(path);
    for (const auto* prefix : {"LocalPlayer_", "NPC_Avatar_", "NPC_Normal_", "NPC_Special_"})
        if (name.rfind(prefix, 0) == 0) {
            name.erase(0, std::char_traits<char>::length(prefix));
            break;
        }
    const std::string suffix = "_Config.json";
    if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
        name.resize(name.size() - suffix.size());
    return name;
}
inline std::string FriendlyName(const api::PlayerCatalogRow& p, Language language = DefaultLocale) {
    if (const auto name = OfficialNameText(p.name); !name.empty())
        return name;
    for (const auto& family : OfficialCharacterFamilies())
        if (std::find(family.player_configs.begin(), family.player_configs.end(), p.character_config) !=
            family.player_configs.end()) {
            const auto* name = LocalizedSourceName(family.key, language);
            if (*name)
                return name;
        }
    return Translate(UiText::UnnamedCharacter, language);
}
inline std::string FriendlyName(const api::NpcCatalogRow& p, Language language = DefaultLocale) {
    if (const auto name = OfficialNameText(p.name); !name.empty())
        return name;
    for (const auto& family : OfficialCharacterFamilies())
        for (const auto& n : family.npcs)
            if (n.config == p.character_config && n.model == p.model_config) {
                const auto* name = LocalizedSourceName(family.key, language);
                if (*name)
                    return name;
            }
    return Translate(UiText::OfficialNpc, language);
}
inline bool PlayerGenderDistinct(const api::PlayerCatalogRow& row, const api::CharacterCatalog& catalog) {
    return row.gender != api::CharacterGender::Unspecified &&
           std::any_of(catalog.players.rows.begin(), catalog.players.rows.end(), [&](const auto& other) {
               return other.avatar != row.avatar && PlayerRoleKey(other) == PlayerRoleKey(row) &&
                      OfficialNameText(other.path_name) == OfficialNameText(row.path_name) &&
                      other.gender != api::CharacterGender::Unspecified && other.gender != row.gender;
           });
}
inline std::string PlayerFormName(const api::PlayerCatalogRow& row, Language language = DefaultLocale,
                                  bool gender = false) {
    auto name = FriendlyName(row, language);
    if (const auto path = OfficialNameText(row.path_name); !path.empty())
        name += " · " + path;
    if (gender && row.gender != api::CharacterGender::Unspecified)
        name += " · " + std::string(Translate(row.gender == api::CharacterGender::Male ? UiText::Male : UiText::Female,
                                              language));
    return name;
}
inline std::string PlayerInstanceName(const api::PlayerCatalogRow& player, const api::CharacterCatalog& rows,
                                      Language language = DefaultLocale) {
    auto name = FriendlyName(player, language);
    for (const auto& other : rows.players.rows)
        if (PlayerRoleKey(other) == PlayerRoleKey(player) && other.avatar != player.avatar) {
            name = PlayerFormName(player, language, PlayerGenderDistinct(player, rows));
            break;
        }
    if (player.enhanced)
        name += " · " + std::string(Translate(UiText::EnhancedVariant, language));
    return name;
}
inline std::string SkinName(const api::CharacterSkinRow& skin, const api::CharacterCatalog& rows,
                            Language language = DefaultLocale) {
    std::string name;
    for (const auto& player : rows.players.rows)
        if (player.avatar == skin.avatar_id && !player.enhanced) {
            name = PlayerInstanceName(player, rows, language);
            break;
        }
    if (name.empty())
        name = Translate(UiText::UnnamedCharacter, language);
    const auto appearance = OfficialNameText(skin.name);
    return name + " · " + (appearance.empty() ? Translate(UiText::SkinAppearance, language) : appearance);
}
inline bool PureNpc(const CharacterPackage& p) {
    return p.source == SourceKind::Official && !p.appearance;
}
template <class Row> std::string SearchNames(const Row& row) {
    std::string names;
    for (const auto& language : AvailableLanguages())
        names += " " + FriendlyName(row, language.value);
    return names;
}
inline const char* SourceMode(const CharacterPackage& p, Language language = DefaultLocale) {
    return Translate(p.source == SourceKind::External ? UiText::ExternalPackage
                     : PureNpc(p)                     ? UiText::OfficialNpc
                                                      : UiText::PlayerMode,
                     language);
}
inline std::string ActorName(const CharacterPackage& p, const api::CharacterCatalog* rows,
                             Language language = DefaultLocale, std::uint32_t skin_id = 0,
                             const api::CharacterSkinCatalog* skins = nullptr) {
    if (skin_id && rows && skins)
        for (const auto& skin : skins->rows)
            if (skin.skin_id == skin_id)
                return SkinName(skin, *rows, language);
    if (p.source == SourceKind::External)
        return p.display_name.empty() ? p.id : p.display_name;
    if (rows) {
        if (p.appearance)
            for (const auto& row : rows->players.rows)
                if (p.id == "official-player:" + std::to_string(row.id) + ":" + std::to_string(row.enhanced) + ":" +
                                std::to_string(row.skin))
                    return PlayerInstanceName(row, *rows, language);
        if (p.appearance)
            for (const auto& row : rows->players.rows)
                if (row.character_config == p.appearance->character_config &&
                    row.prefab == p.appearance->template_prefab)
                    return PlayerInstanceName(row, *rows, language);
        if (!p.appearance)
            for (const auto& row : rows->npcs.rows)
                if (row.id == p.npc_id)
                    return FriendlyName(row, language);
    }
    return Translate(p.appearance ? UiText::UnnamedCharacter : UiText::OfficialNpc, language);
}
}
