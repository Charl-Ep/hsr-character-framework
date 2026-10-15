#pragma once
#include <hksr/animation/OfficialCatalog.hpp>

namespace hksr::framework {
struct NpcVariantReference {
    std::string config,model,note;
};
struct CharacterFamily {
    std::string key,label;
    std::vector<std::string> player_configs;
    std::vector<NpcVariantReference> npcs;
};
// Display names and researched relationships only; NOT a merged runtime catalog.
const std::vector<CharacterFamily>& OfficialCharacterFamilies();
}
