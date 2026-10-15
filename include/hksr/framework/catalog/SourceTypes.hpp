#pragma once
#include "../assets/CharacterPackage.hpp"
#include <hksr/model/ModelProfileInputs.hpp>
#include <hksr/animation/OfficialCatalog.hpp>
#include <variant>

namespace hksr::framework {
enum class SourceResolution { Ready, Pending, Missing, Ambiguous, Incomplete };
struct FoundationChoice {
    SourceResolution status=SourceResolution::Missing;
    std::vector<std::uint32_t> candidates;
    std::vector<std::string> needed_configs,errors;
    std::optional<api::NpcCatalogRow> selected;
    bool explicit_choice=false; // caller supplied foundation, not an inferred identity match
    std::string preference; // nonofficial default policy, empty for automatic/explicit
    std::vector<std::string> notes; // advisory; never a readiness gate
};
struct BattleSourceChoice {
    SourceResolution status=SourceResolution::Missing;
    std::vector<api::AvatarCatalogRow> candidates;
};
struct BattlePresentation {
    api::ProfileDescription model;
    api::TextInput initial_state;
};
struct PlayerKey {
    std::uint32_t id=0,enhanced=0,skin=0;
    bool Matches(const api::PlayerCatalogRow& p) const { return id==p.id && enhanced==p.enhanced && skin==p.skin; }
};
struct PreparedSource {
    std::optional<CharacterPackage> character;
    FoundationChoice foundation;
    BattleSourceChoice battle;
    std::optional<BattlePresentation> presentation;
    bool finished=false;
    std::vector<std::string> errors;
};
struct BrowseNpc { std::uint32_t id; };
struct BrowsePlayer { PlayerKey key; std::optional<std::uint32_t> npc; };
using BrowseSource=std::variant<BrowseNpc,BrowsePlayer,CharacterPackage>;
}
