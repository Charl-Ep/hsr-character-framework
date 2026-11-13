#pragma once
#include <hksr/framework/presentation/scene/VisualProgram.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <filesystem>
#include <functional>

namespace hksr::framework {
struct TaskCatalog;
// Immutable definition selection and authored defaults, shared by actor instances.
struct VisualProgramInput {
    std::shared_ptr<const VisualProgram> program;
    std::shared_ptr<const TaskCatalog> tasks;
    std::map<std::string, std::uint64_t> target_characters;
    std::map<std::string, std::string> choices;
    VisualSpatialAnchors anchors;
    std::optional<bool> default_selection;
    std::uint64_t catalog_revision = 0;
};
using TaskCatalogInputs = std::map<std::string, std::shared_ptr<const VisualProgramInput>>;
// Each source selects one complete script. Other scripts remain selectable.
struct VisualProgramLibrary {
    std::map<std::string, std::shared_ptr<const VisualProgramInput>> sources;
    TaskCatalogInputs catalogs;
    bool empty() const {
        return sources.empty() && catalogs.empty();
    }
};
using ScriptImportReader = std::function<std::string(const std::string&)>;
std::shared_ptr<const VisualProgramInput> ParseScript(const std::string& text, ScriptImportReader imports = {});
std::shared_ptr<const VisualProgramInput> ReadScriptFile(const std::filesystem::path& file,
                                                       std::filesystem::path directory = {});
TaskCatalogInputs SelectScriptDefaults(const TaskCatalogInputs& catalogs);
VisualProgramLibrary ReadScriptLibrary(const std::filesystem::path& directory);
}
