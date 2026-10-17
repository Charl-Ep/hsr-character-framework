#pragma once
#include <hksr/framework/presentation/VisualProgramRuntime.hpp>
#include <filesystem>
#include <mutex>

namespace hksr::research {
// Optional development startup file. No engine reads, target creation or
// action submission. Instance IDs resolve later through SceneCharacters.
using VisualProgramInputs=std::map<std::string,std::shared_ptr<const framework::VisualProgramInput>>;
VisualProgramInputs ReadVisualProgramInputs(const std::filesystem::path& file);
// Worker updates, engine-thread snapshots. Definitions stay fixed for the host
// lifetime; only value inputs can change while a model is running.
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
}
