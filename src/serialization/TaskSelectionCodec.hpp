#pragma once
#include <hksr/framework/tasks/TaskControlTypes.hpp>
#include <nlohmann/json.hpp>

namespace hksr::framework::task_codec {
TaskSelection ParseSelection(const nlohmann::json&);
nlohmann::json Selection(const TaskSelection&);
}
