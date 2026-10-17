#include "VisualProgramInputs.hpp"
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
        Fields(row,{"file","targets","choices"});
        const auto filename=row.at("file").get<std::string>();
        if(filename.empty() || filename.find('\0')!=std::string::npos)throw std::invalid_argument("visual program filename missing");
        auto input=std::make_shared<framework::VisualProgramInput>();
        input->program=std::make_shared<framework::VisualProgram>(framework::ParseVisualProgram(Read(file.parent_path()/std::filesystem::u8path(filename))));
        const auto targets=row.value("targets",Json::object());
        if(!targets.is_object())throw std::invalid_argument("visual targets must be an object");
        for(auto it=targets.begin();it!=targets.end();++it) {
            if(it.key().empty() || it.key()=="Caster" || it.key().find('\0')!=std::string::npos ||
                !it.value().is_number_unsigned() || it.value().get<std::uint64_t>()==0)
                throw std::invalid_argument("visual target requires an alias and positive instance ID");
            input->target_characters.emplace(it.key(),it.value().get<std::uint64_t>());
        }
        input->choices=row.value("choices",Json::object()).get<std::map<std::string,std::string>>();
        if(!result.emplace(input->program->source,input).second)throw std::invalid_argument("duplicate visual program source: "+input->program->source);
    }
    return result;
}
}
