#pragma once
#include <hksr/framework/presentation/scene/VisualProgramLibrary.hpp>
#include <nlohmann/json_fwd.hpp>

namespace hksr::framework::script_codec {
nlohmann::json Compose(nlohmann::json, const ScriptImportReader&);
}
