#pragma once
#include <hksr/framework/presentation/VisualProgramRuntime.hpp>
#include <filesystem>

namespace hksr::research {
// Optional development startup file. No engine reads, target creation or
// action submission. Instance IDs resolve later through SceneCharacters.
using VisualProgramInputs=std::map<std::string,std::shared_ptr<const framework::VisualProgramInput>>;
VisualProgramInputs ReadVisualProgramInputs(const std::filesystem::path& file);
}
