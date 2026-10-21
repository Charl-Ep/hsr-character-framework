#include "TaskCommands.hpp"
#include "../src/serialization/TaskControlCodec.hpp"

namespace hksr::research {
framework::ControlCharacterTask ParseTaskCommand(const nlohmann::json& row) {
    return framework::task_codec::ParseTaskCommand(row);
}
nlohmann::json TaskResult(const framework::TaskControlResult& result) {
    return framework::task_codec::TaskResult(result);
}
}
