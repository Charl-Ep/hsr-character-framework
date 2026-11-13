#pragma once
#include <hksr/framework/presentation/scene/VisualProgram.hpp>
#include <nlohmann/json_fwd.hpp>

namespace hksr::framework::visual_codec {
// Shared by action-clock programs and task catalogs; no synthetic body action.
VisualProgram Resources(const nlohmann::json&, std::string id, std::string source_path, std::string event_scope);
std::vector<VisualInstruction> Instructions(const nlohmann::json&, const VisualProgram&,
                                          const std::string& path = "instructions", bool request_fields = false);
VisualProgramContext Context(const nlohmann::json&, const VisualProgram&, const std::string& path);
VisualProgramAction CuedAction(const nlohmann::json&, const VisualProgram&, const std::string& path);
}
