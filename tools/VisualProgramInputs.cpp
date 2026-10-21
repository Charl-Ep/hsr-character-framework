#include "VisualProgramInputs.hpp"
#include <stdexcept>

namespace hksr::research {
VisualProgramInputs ReadVisualProgramInputs(const std::filesystem::path& file) {
    return framework::ReadVisualProgramLibrary(file);
}
void VisualProgramOverrides::Set(std::uint64_t character,framework::VisualProgramParameters value) {
    if(!character)throw std::invalid_argument("visual input requires an instance ID");
    std::lock_guard<std::mutex> lock(mutex_);
    if(value.targets.empty() && value.choices.empty() && value.anchors.empty())values_.erase(character);
    else values_[character]=std::move(value);
}
framework::VisualProgramParameters VisualProgramOverrides::Read(std::uint64_t character) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found=values_.find(character);
    return found==values_.end()?framework::VisualProgramParameters{}:found->second;
}
VisualProgramInputs BindVisualProgramOverrides(VisualProgramInputs inputs,std::shared_ptr<VisualProgramOverrides> overrides) {
    if(!overrides)throw std::invalid_argument("visual input store absent");
    auto bind=[&](auto& entries){for(auto& [source,original]:entries) {
        (void)source;auto input=std::make_shared<framework::VisualProgramInput>(*original);
        input->read_parameters=[overrides](std::uint64_t character){return overrides->Read(character);};
        input->take_signals=[overrides](std::uint64_t character){return overrides->Take(character);};
        original=std::move(input);
    }};
    bind(inputs.sources);bind(inputs.catalogs);
    return inputs;
}
framework::TaskCatalogInputs ReadBoundTaskCatalogs(const std::filesystem::path& file,std::shared_ptr<VisualProgramOverrides> overrides) {
    // A missing/partially saved candidate is an error, not a request to erase
    // the registry. An explicit {"programs":[]} publishes an empty registry.
    if(!std::filesystem::exists(file))throw std::runtime_error("task manifest missing: "+file.u8string());
    return BindVisualProgramOverrides(ReadVisualProgramInputs(file),std::move(overrides)).catalogs;
}
void VisualProgramOverrides::Push(std::uint64_t character,framework::VisualProgramSignal signal) {
    if(!character || !signal.generation || !signal.intent || signal.name.empty() || signal.name.find('\0')!=std::string::npos)
        throw std::invalid_argument("visual signal requires instance, generation, intent and name");
    std::lock_guard<std::mutex> lock(mutex_);signals_[character].push_back(std::move(signal));
}
std::vector<framework::VisualProgramSignal> VisualProgramOverrides::Take(std::uint64_t character) {
    std::lock_guard<std::mutex> lock(mutex_);const auto found=signals_.find(character);
    if(found==signals_.end())return {};
    auto result=std::move(found->second);signals_.erase(found);return result;
}
}
