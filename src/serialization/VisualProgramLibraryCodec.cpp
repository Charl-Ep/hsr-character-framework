#include <hksr/framework/presentation/VisualProgramRuntime.hpp>
#include <hksr/framework/tasks/TaskCatalog.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

namespace hksr::framework {
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
VisualProgramLibrary ReadVisualProgramLibrary(const std::filesystem::path& file) {
    if(!std::filesystem::exists(file))return {};
    const auto json=Json::parse(Read(file));Fields(json,{"programs"});
    if(!json.at("programs").is_array())throw std::invalid_argument("programs must be an array");
    VisualProgramLibrary result;
    for(const auto& row:json.at("programs")) {
        Fields(row,{"file","targets","choices","anchors"});
        const auto filename=row.at("file").get<std::string>();
        if(filename.empty() || filename.find('\0')!=std::string::npos)throw std::invalid_argument("visual program filename missing");
        auto input=std::make_shared<VisualProgramInput>();
        const auto text=Read(file.parent_path()/std::filesystem::u8path(filename));
        const auto format=Json::parse(text).value("format",std::string{});
        if(format=="hksr.task-catalog") {
            input->tasks=std::make_shared<const TaskCatalog>(ParseTaskCatalog(text));
            input->program=input->tasks->visual;
        } else input->program=std::make_shared<VisualProgram>(ParseVisualProgram(text));
        const auto targets=row.value("targets",Json::object());
        if(!targets.is_object())throw std::invalid_argument("visual targets must be an object");
        for(auto it=targets.begin();it!=targets.end();++it) {
            if(it.key().empty() || it.key()=="Caster" || it.key().find('\0')!=std::string::npos ||
                !it.value().is_number_unsigned() || it.value().get<std::uint64_t>()==0)
                throw std::invalid_argument("visual target requires an alias and positive instance ID");
            input->target_characters.emplace(it.key(),it.value().get<std::uint64_t>());
        }
        input->choices=row.value("choices",Json::object()).get<std::map<std::string,std::string>>();
        input->anchors=ParseVisualSpatialAnchors(row.value("anchors",Json::object()).dump());
        if(input->tasks) {
            if(!result.catalogs.emplace(input->tasks->id,input).second)
                throw std::invalid_argument("duplicate task catalog: "+input->tasks->id);
        } else if(!result.sources.emplace(input->program->source,input).second)
            throw std::invalid_argument("duplicate visual program source: "+input->program->source);
    }
    return result;
}
}
