#include "VisualProgramInputs.hpp"
#include <stdexcept>

namespace hksr::research {
VisualProgramInputs ReadVisualProgramInputs(const std::filesystem::path& directory) {
    return framework::ReadScriptLibrary(directory);
}
void VisualProgramOverrides::Set(std::uint64_t character, framework::VisualProgramParameters value) {
    if (!character)
        throw std::invalid_argument("visual input requires an instance ID");
    std::lock_guard<std::mutex> lock(mutex_);
    if (value.targets.empty() && value.choices.empty() && value.anchors.empty() && value.position_targets.empty() &&
        value.target_aliases.empty() && value.target_collections.empty())
        values_.erase(character);
    else
        values_[character] = std::move(value);
}
framework::VisualProgramParameters VisualProgramOverrides::Read(std::uint64_t character) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = values_.find(character);
    return found == values_.end() ? framework::VisualProgramParameters{} : found->second;
}
framework::VisualProgramProviders CreateVisualProgramProviders(std::shared_ptr<VisualProgramOverrides> overrides) {
    if (!overrides)
        throw std::invalid_argument("visual input store absent");
    return {[overrides](std::uint64_t character) { return overrides->Read(character); },
            [overrides](std::uint64_t character) { return overrides->Take(character); }};
}
framework::TaskCatalogInputs ReadTaskCatalogInputs(const std::filesystem::path& directory) {
    if (!std::filesystem::is_directory(directory))
        throw std::runtime_error("script directory missing: " + directory.u8string());
    return ReadVisualProgramInputs(directory).catalogs;
}
void VisualProgramOverrides::Push(std::uint64_t character, framework::VisualProgramSignal signal) {
    if (!character || !signal.generation || !signal.intent || signal.name.empty() ||
        signal.name.find('\0') != std::string::npos)
        throw std::invalid_argument("visual signal requires instance, generation, intent and name");
    std::lock_guard<std::mutex> lock(mutex_);
    signals_[character].push_back(std::move(signal));
}
std::vector<framework::VisualProgramSignal> VisualProgramOverrides::Take(std::uint64_t character) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = signals_.find(character);
    if (found == signals_.end())
        return {};
    auto result = std::move(found->second);
    signals_.erase(found);
    return result;
}
}
