#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hksr::framework {
enum class SourceKind { Official, External };
enum class ShapeData { None, MatchingTemplate };
enum class ConstructionKind { MapNpc, StoryActor };
struct Appearance {
    std::string template_prefab;
    std::string asset_directory;
    std::string character_config;
    std::string weapon_config;
    ShapeData shapes = ShapeData::None;
};
struct CharacterPackage {
    std::string id, display_name;
    std::uint32_t npc_id=0;
    SourceKind source=SourceKind::Official;
    std::optional<Appearance> appearance;
    // Selected official Avatar identity, not the NPC carrier ID. Zero for
    // pure NPC/authored packages which have not selected an official Avatar.
    std::uint32_t avatar_id=0;
    ConstructionKind construction=ConstructionKind::MapNpc;
};
// File-format parsing only. No game calls, pointers, hook or diagnostic state.
CharacterPackage ParseCharacterPackage(std::string_view json);
}
