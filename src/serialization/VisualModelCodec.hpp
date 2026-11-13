#pragma once
#include <hksr/framework/presentation/model/VisualModelTypes.hpp>
#include <nlohmann/json_fwd.hpp>
#include <optional>

namespace hksr::framework::visual_model_codec {
std::optional<VisualModelOperation> Parse(const nlohmann::json&);
}
