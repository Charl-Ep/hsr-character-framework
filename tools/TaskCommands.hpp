#pragma once
#include <hksr/framework/application/ApplicationService.hpp>
#include <nlohmann/json_fwd.hpp>

namespace hksr::research {
framework::ControlCharacterTask ParseTaskCommand(const nlohmann::json&);
nlohmann::json TaskResult(const framework::TaskControlResult&);
}
