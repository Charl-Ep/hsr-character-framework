#pragma once
#include <hksr/framework/presentation/reactions/HitEffectPolicy.hpp>
#include <nlohmann/json_fwd.hpp>

namespace hksr::framework::hit_effects_codec {
std::vector<HitEffectOverride> Parse(const nlohmann::json&);
nlohmann::json Serialize(const std::vector<HitEffectOverride>&);
}
