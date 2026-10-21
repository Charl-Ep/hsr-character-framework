#pragma once
#include <hksr/framework/application/ApplicationService.hpp>
#include "TaskSelectionCodec.hpp"

namespace hksr::framework::task_codec {
ControlCharacterTask ParseTaskCommand(const nlohmann::json&);
nlohmann::json TaskResult(const TaskControlResult&);
nlohmann::json TaskCommand(const ControlCharacterTask&);
}
