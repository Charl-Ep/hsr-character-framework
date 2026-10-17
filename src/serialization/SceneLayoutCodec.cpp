#include <hksr/framework/layout/SceneLayout.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace hksr::framework {
namespace {
using Json=nlohmann::json;
void Need(bool value,const char* message){if(!value)throw std::invalid_argument(message);}
int Variant(const Json& value) {
    Need(value.is_number_integer(),"saved variant must be an integer");
    if(value.is_number_unsigned()) {
        const auto number=value.get<std::uint64_t>();
        Need(number<=static_cast<std::uint64_t>((std::numeric_limits<int>::max)()),"saved variant exceeds int32");
        return static_cast<int>(number);
    }
    const auto number=value.get<std::int64_t>();
    Need(number>=-1 && number<=(std::numeric_limits<int>::max)(),"saved variant outside supported range");
    return static_cast<int>(number);
}
Json Position(api::Position3 p){return {{"x",p.x},{"y",p.y},{"z",p.z}};}
api::Position3 Position(const Json& j){return {j.at("x").get<float>(),j.at("y").get<float>(),j.at("z").get<float>()};}
std::uint64_t Id(const Json& j) {
    const auto text=j.get<std::string>();std::size_t end=0;
    Need(!text.empty() && std::all_of(text.begin(),text.end(),[](char c){return c>='0' && c<='9';}),"invalid layout key");
    const auto id=std::stoull(text,&end);Need(end==text.size(),"invalid layout key");return id;
}
const char* Domain(ActionDomain d){return d==ActionDomain::Alternate?"battle":"exploration";}
ActionDomain Domain(const std::string& d){Need(d=="battle"||d=="exploration","invalid action domain");return d=="battle"?ActionDomain::Alternate:ActionDomain::Exploration;}
const char* Look(LookMode mode) {
    switch(mode){case LookMode::Automatic:return "automatic";case LookMode::Disabled:return "disabled";
    case LookMode::Player:return "player";case LookMode::Instance:return "instance";case LookMode::Point:return "point";}
    throw std::invalid_argument("invalid gaze mode");
}
LookMode Look(const std::string& text) {
    for(auto mode:{LookMode::Automatic,LookMode::Disabled,LookMode::Player,LookMode::Instance,LookMode::Point})
        if(text==Look(mode))return mode;
    throw std::invalid_argument("invalid gaze mode");
}
}
std::string EncodeSceneLayout(const SceneLayout& layout) {
    ValidateSceneLayout(layout);
    Json out={{"format","hksr.scene-layout"},{"version",1},{"scene",layout.scene},{"characters",Json::array()}};
    for(const auto& row:layout.characters) {
        Json source;
        if(const auto* npc=std::get_if<BrowseNpc>(&row.source))source={{"kind","npc"},{"npc_id",npc->id}};
        else {
            const auto& player=std::get<BrowsePlayer>(row.source);
            source={{"kind","player"},{"player_id",player.key.id},{"enhanced",player.key.enhanced},{"skin",player.key.skin}};
            if(player.npc)source["npc_id"]=*player.npc;
        }
        const auto q=row.placement.rotation;
        Json value={{"key",std::to_string(row.key)},{"source",source},{"enabled",row.enabled},{"presentation_phase",row.presentation_phase},
            {"position",Position(row.placement.position)},{"rotation",{{"x",q.x},{"y",q.y},{"z",q.z},{"w",q.w}}},
            {"look",{{"mode",Look(row.look.mode)},{"target",std::to_string(row.look.target)},{"point",Position(row.look.point)},{"max_distance",row.look.max_distance}}}};
        if(row.placement.snap_to_ground)value["snap_to_ground"]=*row.placement.snap_to_ground;
        if(row.action)value["action"]=std::visit([](const auto& action)->Json {
            using T=std::decay_t<decltype(action)>;
            if constexpr(std::is_same_v<T,NpcMotion>)return {{"kind","npc"},{"id",action.id}};
            else if constexpr(std::is_same_v<T,SavedConfiguredAction>)
                return {{"kind","configured"},{"domain",Domain(action.domain)},{"state",action.state},{"controller_observed",action.controller_observed}};
            else if constexpr(std::is_same_v<T,ProgramAction>)return {{"kind","program"},{"program",action.program},{"action",action.action},{"choices",action.choices}};
            else return {{"kind","ready"},{"source",action.source},{"state",action.state},{"skill",action.skill},{"variant",action.variant}};
        },*row.action);
        out["characters"].push_back(std::move(value));
    }
    return out.dump(2);
}
SceneLayout DecodeSceneLayout(const std::string& text) {
    const auto data=Json::parse(text);Need(data.at("format")=="hksr.scene-layout" && data.at("version")==1,"unsupported layout format/version");
    SceneLayout out;out.scene=data.at("scene").get<std::string>();
    Need(data.at("characters").is_array(),"layout characters must be an array");
    for(const auto& item:data.at("characters")) {
        SavedCharacter row;row.key=Id(item.at("key"));row.enabled=item.at("enabled").get<bool>();
        row.presentation_phase=item.value("presentation_phase",std::string{});
        const auto& source=item.at("source");
        auto number=[](const Json& n) {Need(n.is_number_unsigned() && n.get<std::uint64_t>()<=UINT32_MAX,"invalid source/action ID");return n.get<std::uint32_t>();};
        if(source.at("kind")=="npc")row.source=BrowseNpc{number(source.at("npc_id"))};
        else {
            Need(source.at("kind")=="player","unsupported layout source");
            BrowsePlayer player{{number(source.at("player_id")),number(source.at("enhanced")),number(source.at("skin"))},{}};
            if(source.contains("npc_id"))player.npc=number(source.at("npc_id"));row.source=player;
        }
        row.placement.position=Position(item.at("position"));const auto& q=item.at("rotation");
        row.placement.rotation={q.at("x").get<float>(),q.at("y").get<float>(),q.at("z").get<float>(),q.at("w").get<float>()};
        if(item.contains("snap_to_ground"))row.placement.snap_to_ground=item.at("snap_to_ground").get<bool>();
        const auto& look=item.at("look");row.look={Look(look.at("mode").get<std::string>()),Id(look.at("target")),Position(look.at("point"))};
        row.look.max_distance=look.value("max_distance",0.0f);
        if(item.contains("action")) {
            const auto& a=item.at("action");
            if(a.at("kind")=="npc")row.action=NpcMotion{number(a.at("id"))};
            else if(a.at("kind")=="program")row.action=ProgramAction{a.at("program").get<std::string>(),a.at("action").get<std::string>(),
                a.value("choices",std::map<std::string,std::string>{})};
            else if(a.at("kind")=="configured")row.action=SavedConfiguredAction{Domain(a.at("domain").get<std::string>()),a.at("state").get<std::string>(),a.value("controller_observed",false)};
            else {
                Need(a.at("kind")=="ready","unsupported saved action");
                row.action=SavedReadyAction{a.at("source").get<std::string>(),a.at("state").get<std::string>(),a.at("skill").get<std::string>(),Variant(a.at("variant"))};
            }
        }
        out.characters.push_back(std::move(row));
    }
    ValidateSceneLayout(out);return out;
}
}
