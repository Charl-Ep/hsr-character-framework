#pragma once
#include <hksr/framework/catalog/SourceList.hpp>
#include <hksr/model/CharacterSkin.hpp>
#include <cstdint>

namespace hksr::framework {
struct PlayerSourceGroup {
    std::uint64_t key = 0;
    std::vector<std::size_t> players, skins;
};
struct PlayerAppearanceChoice {
    bool skin = false;
    std::size_t index = 0;
    bool operator==(const PlayerAppearanceChoice& other) const {
        return skin == other.skin && index == other.index;
    }
};
inline std::uint64_t PlayerRoleKey(const api::PlayerCatalogRow& row) {
    // A missing official RoleID must not merge unrelated sources by name.
    return row.role ? row.role : (std::uint64_t{1} << 32) | row.avatar;
}
inline std::vector<PlayerSourceGroup> GroupPlayerSources(const api::CharacterCatalog& catalog,
                                                         const api::CharacterSkinCatalog* skins = nullptr) {
    std::vector<PlayerSourceGroup> groups;
    for (auto index : SourceIndices(catalog.players.rows, "")) {
        const auto key = PlayerRoleKey(catalog.players.rows[index]);
        auto group = std::find_if(groups.begin(), groups.end(), [&](const auto& row) { return row.key == key; });
        if (group == groups.end()) {
            groups.push_back({key, {index}, {}});
        } else
            group->players.push_back(index);
    }
    if (skins)
        for (std::size_t i = 0; i < skins->rows.size(); ++i) {
            const auto owner = std::find_if(catalog.players.rows.begin(), catalog.players.rows.end(),
                                            [&](const auto& row) { return row.avatar == skins->rows[i].avatar_id; });
            if (owner == catalog.players.rows.end())
                continue;
            for (auto& group : groups)
                if (group.key == PlayerRoleKey(*owner)) {
                    group.skins.push_back(i);
                    break;
                }
        }
    return groups;
}
inline std::vector<std::uint32_t> PlayerForms(const PlayerSourceGroup& group, const api::CharacterCatalog& catalog) {
    std::vector<std::uint32_t> result;
    for (auto index : group.players) {
        const auto avatar = catalog.players.rows[index].avatar;
        if (std::find(result.begin(), result.end(), avatar) == result.end())
            result.push_back(avatar);
    }
    return result;
}
inline std::vector<std::uint32_t> PlayerEnhancements(const PlayerSourceGroup& group,
                                                     const api::CharacterCatalog& catalog, std::uint32_t avatar) {
    std::vector<std::uint32_t> result;
    for (auto index : group.players)
        if (catalog.players.rows[index].avatar == avatar)
            result.push_back(catalog.players.rows[index].enhanced);
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
inline std::vector<PlayerAppearanceChoice> PlayerAppearances(const PlayerSourceGroup& group,
                                                             const api::CharacterCatalog& catalog,
                                                             const api::CharacterSkinCatalog* skins,
                                                             std::uint32_t avatar, std::uint32_t enhanced) {
    std::vector<PlayerAppearanceChoice> result;
    for (auto index : group.players)
        if (catalog.players.rows[index].avatar == avatar && catalog.players.rows[index].enhanced == enhanced)
            result.push_back({false, index});
    // BrowseSkin currently prepares an independent, normal Avatar source; it
    // does not accept EnhancedID. Do not invent enhanced/skin combinations.
    if (!enhanced && skins)
        for (auto index : group.skins)
            if (skins->rows[index].avatar_id == avatar)
                result.push_back({true, index});
    return result;
}
}
