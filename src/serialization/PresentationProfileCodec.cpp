#include <hksr/framework/presentation/PresentationProfileFiles.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>

namespace hksr::framework {
namespace {
using Json=nlohmann::json;
void Need(bool ok,const std::string& reason){if(!ok)throw std::invalid_argument(reason);}
void Fields(const Json& row,std::initializer_list<const char*> names) {
    Need(row.is_object(),"profile entry must be an object");
    for(auto it=row.begin();it!=row.end();++it) {
        const bool known=std::any_of(names.begin(),names.end(),[&](auto name){return it.key()==name;});
        Need(known,"unknown profile field: "+it.key());
    }
}
std::string Name(const Json& value) {
    const auto text=value.get<std::string>();
    Need(!text.empty() && text.find('\0')==std::string::npos,"empty or invalid profile name");return text;
}
float Time(const Json& value) {
    Need(value.is_number(),"normalized time must be numeric");const auto out=value.get<float>();
    Need(std::isfinite(out) && out>=0,"normalized time must be finite and nonnegative");return out;
}
}
PresentationProfile ParsePresentationProfile(const std::string& text) {
    const auto json=Json::parse(text);Fields(json,{"format","id","source","initial_phase","reset_on_domain_exit","phases","actions"});
    Need(json.at("format")=="hksr.presentation-profile","unsupported presentation profile format");
    PresentationProfile out;out.id=Name(json.at("id"));out.source=Name(json.at("source"));
    out.initial_phase=Name(json.at("initial_phase"));out.reset_on_domain_exit=json.value("reset_on_domain_exit",true);
    Need(json.at("phases").is_object(),"phases must be an object");
    for(auto it=json.at("phases").begin();it!=json.at("phases").end();++it) {
        Fields(it.value(),{"idle"});out.phases.emplace(Name(it.key()),PresentationPhase{Name(it.value().at("idle"))});
    }
    auto phase=[&](const Json& row,const char* key) {
        if(!row.contains(key))return std::string{};
        const auto id=Name(row.at(key));Need(out.phases.count(id)!=0,"unknown phase: "+id);return id;
    };
    Need(out.phases.count(out.initial_phase)!=0,"initial phase is absent");
    Need(json.at("actions").is_object(),"actions must be an object");
    for(auto it=json.at("actions").begin();it!=json.at("actions").end();++it) {
        const auto& row=it.value();Fields(row,{"on_enter","on_complete","on_interrupt","cues","complete_at","return"});
        PresentationAction action;action.enter_phase=phase(row,"on_enter");action.complete_phase=phase(row,"on_complete");
        action.interrupt_phase=phase(row,"on_interrupt");action.complete_at=Time(row.value("complete_at",Json(1)));
        const auto finish=row.value("return",std::string("phase_idle"));
        Need(finish=="phase_idle" || finish=="hold","unknown action return policy");
        action.finish=finish=="hold"?PresentationReturn::Hold:PresentationReturn::PhaseIdle;
        if(row.contains("cues")) {
            Need(row.at("cues").is_array(),"cues must be an array");
            for(const auto& cue:row.at("cues")) {
                Fields(cue,{"at","phase"});const auto at=Time(cue.at("at"));
                Need(action.cues.empty() || at>action.cues.back().normalized_time,"cues must have strictly increasing progress");
                Need(at<=action.complete_at,"cue is later than configured completion");
                action.cues.push_back({at,phase(cue,"phase")});Need(!action.cues.back().phase.empty(),"cue needs a phase");
            }
        }
        out.actions.emplace(Name(it.key()),std::move(action));
    }
    return out;
}
PresentationProfiles ReadPresentationProfiles(const std::filesystem::path& directory) {
    PresentationProfiles out;std::error_code error;
    if(!std::filesystem::exists(directory,error)) {if(error)out.errors.push_back(error.message());return out;}
    std::vector<std::filesystem::path> paths;
    for(std::filesystem::directory_iterator it(directory,error),end;!error && it!=end;it.increment(error)) {
        std::error_code entry_error;const auto regular=it->is_regular_file(entry_error);
        if(entry_error)out.errors.push_back(it->path().u8string()+": "+entry_error.message());
        else if(regular && it->path().extension()==".json")paths.push_back(it->path());
    }
    if(error)out.errors.push_back(directory.u8string()+": "+error.message());
    std::sort(paths.begin(),paths.end());std::set<std::string> ambiguous;
    for(const auto& path:paths)try {
        std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error("cannot read profile");
        std::string text{std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
        auto profile=std::make_shared<const PresentationProfile>(ParsePresentationProfile(text));
        if(ambiguous.count(profile->source))continue;
        if(out.sources.count(profile->source)) {
            out.sources.erase(profile->source);ambiguous.insert(profile->source);
            throw std::runtime_error("multiple profiles bind the same source: "+profile->source);
        }
        out.sources.emplace(profile->source,std::move(profile));
    }catch(const std::exception& e){out.errors.push_back(path.filename().u8string()+": "+e.what());}
    return out;
}
}
