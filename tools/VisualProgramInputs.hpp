#pragma once
#include <hksr/framework/presentation/scene/VisualProgramLibrary.hpp>
#include <hksr/framework/presentation/scene/VisualProgramBindings.hpp>
#include <filesystem>
#include <mutex>

namespace hksr::research {
// Shared script directory reader. No engine reads or action submission.
using VisualProgramInputs = framework::VisualProgramLibrary;
VisualProgramInputs ReadVisualProgramInputs(const std::filesystem::path& directory);
// Worker updates, engine-thread snapshots. Each live instance retains its
// published definitions; value inputs can change while its model is running.
class VisualProgramOverrides {
  public:
    void Set(std::uint64_t character, framework::VisualProgramParameters value);
    framework::VisualProgramParameters Read(std::uint64_t character) const;
    void Push(std::uint64_t character, framework::VisualProgramSignal);
    std::vector<framework::VisualProgramSignal> Take(std::uint64_t character);

  private:
    mutable std::mutex mutex_;
    std::map<std::uint64_t, framework::VisualProgramParameters> values_;
    std::map<std::uint64_t, std::vector<framework::VisualProgramSignal>> signals_;
};
framework::VisualProgramProviders CreateVisualProgramProviders(std::shared_ptr<VisualProgramOverrides>);
framework::TaskCatalogInputs ReadTaskCatalogInputs(const std::filesystem::path&);
}
