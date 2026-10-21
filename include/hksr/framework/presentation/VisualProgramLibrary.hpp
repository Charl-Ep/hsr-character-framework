#pragma once
#include <map>
#include <memory>
#include <string>
#include <filesystem>

namespace hksr::framework {
struct VisualProgramInput;
using TaskCatalogInputs=std::map<std::string,std::shared_ptr<const VisualProgramInput>>;
// Source defaults and explicit catalogs have separate identities. Publishing
// a task candidate never replaces an existing source's visual scene.
struct VisualProgramLibrary {
    std::map<std::string,std::shared_ptr<const VisualProgramInput>> sources;
    TaskCatalogInputs catalogs;
    bool empty() const {return sources.empty() && catalogs.empty();}
};
VisualProgramLibrary ReadVisualProgramLibrary(const std::filesystem::path& manifest);
}
