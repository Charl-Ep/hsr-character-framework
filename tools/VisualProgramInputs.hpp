#pragma once
#include <hksr/framework/presentation/VisualProgramRuntime.hpp>
#include <filesystem>
#include <mutex>

namespace hksr::research {
// Optional development input file. No engine reads, target creation or
// action submission. Instance IDs resolve later through SceneCharacters.
using VisualProgramInputs=framework::VisualProgramLibrary;
VisualProgramInputs ReadVisualProgramInputs(const std::filesystem::path& file);
// Worker updates, engine-thread snapshots. Each live instance retains its
// published definitions; value inputs can change while its model is running.
class VisualProgramOverrides {
public:
    void Set(std::uint64_t character,framework::VisualProgramParameters value);
    framework::VisualProgramParameters Read(std::uint64_t character) const;
    void Push(std::uint64_t character,framework::VisualProgramSignal);
    std::vector<framework::VisualProgramSignal> Take(std::uint64_t character);
private:
    mutable std::mutex mutex_;
    std::map<std::uint64_t,framework::VisualProgramParameters> values_;
    std::map<std::uint64_t,std::vector<framework::VisualProgramSignal>> signals_;
};
VisualProgramInputs BindVisualProgramOverrides(VisualProgramInputs,
    std::shared_ptr<VisualProgramOverrides>);
framework::TaskCatalogInputs ReadBoundTaskCatalogs(const std::filesystem::path&,
    std::shared_ptr<VisualProgramOverrides>);
}
