#include "AnimationParameterCodec.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace hksr::framework::animation_parameter_codec {
namespace {
using Json = nlohmann::json;
void Need(bool valid, const char* detail) {
    if (!valid)
        throw std::invalid_argument(detail);
}
void Fields(const Json& value, std::initializer_list<const char*> names) {
    Need(value.is_object(), "animation parameter object expected");
    for (auto it = value.begin(); it != value.end(); ++it) {
        bool known = false;
        for (const auto* name : names)
            known = known || it.key() == name;
        if (!known)
            throw std::invalid_argument("unknown animation parameter field: " + it.key());
    }
}
std::string Name(const Json& value) {
    Need(value.is_string(), "animation parameter name must be text");
    auto text = value.get<std::string>();
    Need(!text.empty() && text.find('\0') == std::string::npos, "invalid animation parameter name");
    return text;
}
}
TaskAnimationParameterCommand Parse(const Json& row) {
    Fields(row, {"op", "as", "wait", "participant", "parameter", "propagate", "source"});
    Need(row.at("op") == "animation_parameter", "animation_parameter operation expected");
    TaskAnimationParameterCommand command;
    command.participant.name = Name(row.at("participant"));
    const auto& parameter = row.at("parameter");
    Fields(parameter, {"name", "type", "value"});
    command.parameter.name = Name(parameter.at("name"));
    const auto type = Name(parameter.at("type"));
    if (type == "float") {
        const auto& value = parameter.at("value");
        Need(value.is_number(), "float animation parameter requires a number");
        const auto number = value.get<double>();
        Need(std::isfinite(number) && std::abs(number) <= std::numeric_limits<float>::max(),
             "animation parameter float out of range");
        command.parameter.value = static_cast<float>(number);
    } else if (type == "int") {
        const auto& value = parameter.at("value");
        Need(value.is_number_integer(), "int animation parameter requires an integer");
        if (value.is_number_unsigned()) {
            const auto number = value.get<std::uint64_t>();
            Need(number <= static_cast<std::uint64_t>(std::numeric_limits<int>::max()),
                 "animation parameter int out of range");
            command.parameter.value = static_cast<int>(number);
        } else {
            const auto number = value.get<std::int64_t>();
            Need(number >= std::numeric_limits<int>::min() && number <= std::numeric_limits<int>::max(),
                 "animation parameter int out of range");
            command.parameter.value = static_cast<int>(number);
        }
    } else if (type == "bool") {
        Need(parameter.at("value").is_boolean(), "bool animation parameter requires a boolean");
        command.parameter.value = parameter.at("value").get<bool>();
    } else if (type == "trigger" || type == "reset_trigger") {
        if (parameter.contains("value"))
            Need(parameter.at("value").is_boolean() && parameter.at("value").get<bool>(),
                 "trigger value, when supplied, must be true");
        command.parameter.value = api::AnimationTrigger{type == "reset_trigger"};
    } else
        throw std::invalid_argument("unknown animation parameter type: " + type);
    if (row.contains("propagate")) {
        Need(row.at("propagate").is_boolean(), "animation parameter propagate must be boolean");
        if (row.at("propagate").get<bool>())
            command.propagation = api::AnimationParameterPropagation::SyncPropState;
    }
    return command;
}
}
