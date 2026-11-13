#include <hksr/framework/tasks/TaskControlTypes.hpp>
#include <cmath>
#include <stdexcept>

namespace hksr::framework {
void ValidateTaskSelection(const TaskSelection& value) {
    auto name = [](const auto& text) { return !text.empty() && text.find('\0') == std::string::npos; };
    if (!name(value.task.catalog) || !name(value.task.name))
        throw std::invalid_argument("invalid task identity");
    for (const auto& [key, input] : value.query.inputs) {
        if (!name(key))
            throw std::invalid_argument("invalid task input name");
        if (const auto* number = std::get_if<double>(&input); number && !std::isfinite(*number))
            throw std::invalid_argument("nonfinite task input");
    }
    ValidateVisualParameters(value.visual);
}
bool SameTaskSelection(const TaskSelection& a, const TaskSelection& b) {
    return a.task == b.task && a.inputs_captured == b.inputs_captured && a.query.inputs == b.query.inputs &&
           SameVisualParameters(a.visual, b.visual);
}
void RemapTaskTargets(TaskSelection& value, const std::map<std::uint64_t, std::uint64_t>& keys) {
    RemapVisualParameters(value.visual, keys);
}
}
