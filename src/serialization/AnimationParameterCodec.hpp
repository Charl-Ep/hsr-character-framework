#pragma once
#include <hksr/framework/tasks/TaskAnimationParameter.hpp>
#include <nlohmann/json_fwd.hpp>

namespace hksr::framework::animation_parameter_codec {
TaskAnimationParameterCommand Parse(const nlohmann::json&);
}
