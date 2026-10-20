#pragma once
#include <hksr/framework/presentation/VisualProgram.hpp>
#include <nlohmann/json_fwd.hpp>

namespace hksr::framework::visual_codec {
// Shared by action-clock programs and task catalogs; no synthetic body action.
VisualProgram Resources(const nlohmann::json&);
std::vector<VisualInstruction> Instructions(const nlohmann::json&,const VisualProgram&);
}
