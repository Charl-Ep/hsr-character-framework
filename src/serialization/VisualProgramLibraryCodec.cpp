#include <hksr/framework/presentation/scene/VisualProgramLibrary.hpp>
#include <hksr/framework/tasks/TaskCatalog.hpp>
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace hksr::framework {
namespace {
std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("cannot read script: " + path.u8string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
bool IsScript(const std::filesystem::path& path) {
    const auto name = path.filename().u8string();
    const std::string suffix = ".script.json";
    return name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}
ScriptImportReader ImportReader(const std::filesystem::path& directory) {
    return [directory, cache = std::map<std::string, std::string>{}](const std::string& name) mutable {
        auto [found, inserted] = cache.try_emplace(name);
        if (inserted) found->second = Read(directory / std::filesystem::u8path(name));
        return found->second;
    };
}
}

std::shared_ptr<const VisualProgramInput> ReadScriptFile(const std::filesystem::path& file,
                                                       std::filesystem::path directory) {
    if (directory.empty()) {
        directory = file.parent_path();
        for (auto parent = directory; !parent.empty(); parent = parent.parent_path()) {
            if (parent.filename() == "Scripts") { directory = parent; break; }
            if (parent == parent.parent_path()) break;
        }
    }
    return ParseScript(Read(file), ImportReader(directory));
}

TaskCatalogInputs SelectScriptDefaults(const TaskCatalogInputs& catalogs) {
    std::map<std::string, std::vector<std::shared_ptr<const VisualProgramInput>>> sources;
    for (const auto& [id, input] : catalogs) {
        if (!input || !input->tasks || id != input->tasks->id || input->program != input->tasks->visual)
            throw std::invalid_argument("invalid script publication: " + id);
        sources[input->program->source].push_back(input);
    }
    TaskCatalogInputs result;
    for (const auto& [source, candidates] : sources) {
        std::shared_ptr<const VisualProgramInput> selected;
        for (const auto& input : candidates)
            if (input->default_selection == true) {
                if (selected)
                    throw std::invalid_argument("multiple default scripts for source: " + source);
                selected = input;
            }
        if (candidates.size() == 1) {
            if (candidates.front()->default_selection != false)
                selected = candidates.front();
        } else if (!selected && std::any_of(candidates.begin(), candidates.end(), [](const auto& input) {
                       return !input->default_selection.has_value();
                   }))
            throw std::invalid_argument("multiple scripts require one explicit default for source: " + source);
        if (selected)
            result.emplace(source, std::move(selected));
    }
    return result;
}

VisualProgramLibrary ReadScriptLibrary(const std::filesystem::path& directory) {
    if (!std::filesystem::exists(directory))
        return {};
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
        if (entry.is_regular_file() && IsScript(entry.path()))
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    VisualProgramLibrary result;
    auto imports = ImportReader(directory);
    for (const auto& file : files)
        try {
            auto input = ParseScript(Read(file), std::ref(imports));
            if (!result.catalogs.emplace(input->tasks->id, input).second)
                throw std::invalid_argument("duplicate script id: " + input->tasks->id);
        } catch (const std::exception& error) {
            throw std::runtime_error(file.u8string() + ": " + error.what());
        }
    result.sources = SelectScriptDefaults(result.catalogs);
    return result;
}
}
