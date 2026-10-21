#include "VisualProgramInputs.hpp"
#include <hksr/framework/tasks/TaskCatalog.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

namespace hksr::research {
namespace {
using Json=nlohmann::json;
std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("cannot read visual program input: "+path.u8string());
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
void Fields(const Json& row,std::initializer_list<const char*> names) {
    if(!row.is_object())throw std::invalid_argument("visual program input must be an object");
    for(auto it=row.begin();it!=row.end();++it) {
        bool known=false;for(const auto* name:names)known=known || it.key()==name;
        if(!known)throw std::invalid_argument("unknown visual program input field: "+it.key());
    }
}
}
VisualProgramInputs ReadVisualProgramInputs(const std::filesystem::path& file) {
    if(!std::filesystem::exists(file))return {};
    const auto json=Json::parse(Read(file));Fields(json,{"programs"});
    if(!json.at("programs").is_array())throw std::invalid_argument("programs must be an array");
    VisualProgramInputs result;
    for(const auto& row:json.at("programs")) {
        Fields(row,{"file","targets","choices","anchors"});
        const auto filename=row.at("file").get<std::string>();
        if(filename.empty() || filename.find('\0')!=std::string::npos)throw std::invalid_argument("visual program filename missing");
        auto input=std::make_shared<framework::VisualProgramInput>();
        const auto text=Read(file.parent_path()/std::filesystem::u8path(filename));
        const auto format=Json::parse(text).value("format",std::string{});
        if(format=="hksr.task-catalog") {
            input->tasks=std::make_shared<const framework::TaskCatalog>(framework::ParseTaskCatalog(text));
            input->program=input->tasks->visual;
        } else input->program=std::make_shared<framework::VisualProgram>(framework::ParseVisualProgram(text));
        const auto targets=row.value("targets",Json::object());
        if(!targets.is_object())throw std::invalid_argument("visual targets must be an object");
        for(auto it=targets.begin();it!=targets.end();++it) {
            if(it.key().empty() || it.key()=="Caster" || it.key().find('\0')!=std::string::npos ||
                !it.value().is_number_unsigned() || it.value().get<std::uint64_t>()==0)
                throw std::invalid_argument("visual target requires an alias and positive instance ID");
            input->target_characters.emplace(it.key(),it.value().get<std::uint64_t>());
        }
        input->choices=row.value("choices",Json::object()).get<std::map<std::string,std::string>>();
        input->anchors=framework::ParseVisualSpatialAnchors(row.value("anchors",Json::object()).dump());
        if(input->tasks) {
            if(!result.catalogs.emplace(input->tasks->id,input).second)
                throw std::invalid_argument("duplicate task catalog: "+input->tasks->id);
        } else if(!result.sources.emplace(input->program->source,input).second)
            throw std::invalid_argument("duplicate visual program source: "+input->program->source);
    }
    return result;
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
