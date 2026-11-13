#pragma once
#include <hksr/framework/presentation/audio/VisualBgmTypes.hpp>
#include <nlohmann/json_fwd.hpp>
#include <optional>
namespace hksr::framework::visual_bgm_codec {
std::optional<VisualBgmSync> Parse(const nlohmann::json&);
}
