#pragma once
#include <hksr/framework/assets/CharacterPackage.hpp>
#include <hksr/model/ModelProfileInputs.hpp>
#include <hksr/animation/OfficialCatalog.hpp>
#include <hksr/model/CharacterSkin.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <variant>

namespace hksr::framework {
enum class SourceResolution { Ready, Missing, Ambiguous, Incomplete };
struct BattleSourceChoice {
    SourceResolution status = SourceResolution::Missing;
    std::vector<api::AvatarCatalogRow> candidates;
};
struct BattlePresentation {
    api::ProfileDescription model;
    api::TextInput initial_state;
};
struct PlayerKey {
    std::uint32_t id = 0, enhanced = 0, skin = 0;
    bool Matches(const api::PlayerCatalogRow& p) const {
        return id == p.id && enhanced == p.enhanced && skin == p.skin;
    }
};
// Published source values; preparation resources remain with their official-thread owner.
struct SourceSummary {
    std::optional<CharacterPackage> character;
    BattleSourceChoice battle;
    std::optional<BattlePresentation> presentation;
    bool finished = false;
    std::vector<std::string> errors;
};
struct PreparedSource {
    SourceSummary summary;
    std::shared_ptr<api::CharacterSkin> skin;
};
struct BrowseNpc {
    std::uint32_t id;
};
struct BrowsePlayer {
    PlayerKey key;
};
struct BrowseSkin {
    std::uint32_t skin_id = 0;
};
using BrowseSource = std::variant<BrowseNpc, BrowsePlayer, BrowseSkin, CharacterPackage>;
}
