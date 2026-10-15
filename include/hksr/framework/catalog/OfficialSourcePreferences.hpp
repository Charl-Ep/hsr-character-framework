#pragma once
#include <string>
#include <vector>

namespace hksr::framework {
// Framework policy, NOT an official identity/compatibility table. Resource
// references select live catalog rows; numeric NPC/Player IDs are not persisted.
// An entry chooses the default carrier for a Player appearance. It does not
// merge variants, enumerate actions or promise cross-variant animation support.
struct OfficialSourcePreference {
    std::string label;
    std::string player_config,npc_config,npc_model;
    std::string evidence;
};
const std::vector<OfficialSourcePreference>& OfficialSourcePreferences();
}
