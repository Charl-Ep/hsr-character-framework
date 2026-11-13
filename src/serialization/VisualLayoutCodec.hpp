#pragma once
#include <hksr/framework/presentation/effects/VisualLayoutTypes.hpp>
#include <nlohmann/json_fwd.hpp>
#include <optional>
namespace hksr::framework::visual_layout_codec {
std::optional<VisualEffectLayout> Parse(const nlohmann::json&);
}
