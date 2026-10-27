#include <hksr/framework/layout/SceneLayout.hpp>
#include "TaskSelectionCodec.hpp"
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
Json Context(const VisualContextSnapshot& value) {
    Json targets=Json::object(),anchors=Json::object(),positions=Json::object();
    for(const auto& [name,id]:value.inputs.targets)targets[name]=id?Json(std::to_string(id)):Json(nullptr);
    for(const auto& [name,pose]:value.inputs.anchors) {
        const auto q=pose.rotation;
        anchors[name]={{"position",Position(pose.position)},{"rotation",{q.x,q.y,q.z,q.w}}};
    }
    for(const auto& [name,pose]:value.inputs.position_targets) {
        const auto q=pose.rotation;
        positions[name]={{"position",Position(pose.position)},{"rotation",{q.x,q.y,q.z,q.w}}};
    }
    return {{"name",value.name},{"choices",value.inputs.choices},{"variables",value.variables},
        {"targets",targets},{"anchors",anchors},{"position_targets",positions}};
}
VisualContextSnapshot Context(const Json& value) {
    VisualContextSnapshot out;out.name=value.at("name").get<std::string>();
    out.inputs.choices=value.at("choices").get<std::map<std::string,std::string>>();
    out.variables=value.at("variables").get<std::map<std::string,float>>();
    Need(value.at("targets").is_object() && value.at("anchors").is_object(),"invalid visual inputs");
    for(auto it=value.at("targets").begin();it!=value.at("targets").end();++it)out.inputs.targets[it.key()]=it.value().is_null()?0:Id(it.value());
    for(const auto* field:{"anchors","position_targets"}) {
    if(!value.contains(field))continue;
    Need(value.at(field).is_object(),"invalid visual poses");
    auto& poses=std::string(field)=="anchors"?out.inputs.anchors:out.inputs.position_targets;
    for(auto it=value.at(field).begin();it!=value.at(field).end();++it) {
        const auto& row=it.value();const auto& q=row.at("rotation");
        Need(q.is_array() && q.size()==4,"invalid visual anchor rotation");
        poses[it.key()]={Position(row.at("position")),{q[0].get<float>(),q[1].get<float>(),q[2].get<float>(),q[3].get<float>()}};
    }
    }
    return out;
}
Json ProgramSnapshot(const VisualProgramSnapshot& state) {
    Json out={{"program",state.program},{"contexts",Json::array()}};
    if(state.scene)out["scene"]=Context(*state.scene);
    for(const auto& value:state.contexts)out["contexts"].push_back(Context(value));
    return out;
}
VisualProgramSnapshot ProgramSnapshot(const Json& json) {
    VisualProgramSnapshot out;out.program=json.at("program").get<std::string>();
    Need(json.at("contexts").is_array(),"visual contexts must retain their entry order");
    if(json.contains("scene"))out.scene=Context(json.at("scene"));
    for(const auto& value:json.at("contexts"))out.contexts.push_back(Context(value));
    return out;
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
        if(row.visual_state)value["visual_state"]=ProgramSnapshot(*row.visual_state);
        if(!row.task_catalog.empty())value["task_catalog"]=row.task_catalog;
        if(row.task){value["task"]=task_codec::Selection(*row.task);value["task"]["captured"]=row.task->inputs_captured;}
        if(row.action)value["action"]=std::visit([](const auto& action)->Json {
            using T=std::decay_t<decltype(action)>;
            if constexpr(std::is_same_v<T,NpcMotion>)return {{"kind","npc"},{"id",action.id}};
            else if constexpr(std::is_same_v<T,BattleIdle>)return {{"kind","battle_idle"}};
            else if constexpr(std::is_same_v<T,VisualActionKey>)return {{"kind","visual_behavior"},{"source",action.character_config},{"flag",action.flag},{"recover",action.recover}};
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
        if(item.contains("visual_state"))row.visual_state=ProgramSnapshot(item.at("visual_state"));
        row.task_catalog=item.value("task_catalog",std::string{});
        if(item.contains("task")){
            row.task=task_codec::ParseSelection(item.at("task"));row.task->inputs_captured=item.at("task").value("captured",false);
        }
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
            else if(a.at("kind")=="battle_idle")row.action=BattleIdle{};
            else if(a.at("kind")=="visual_behavior") {
                const auto& flag=a.at("flag");
                Need(flag.is_number_integer() && (flag.is_number_unsigned()?flag.get<std::uint64_t>()<=INT32_MAX:
                    flag.get<std::int64_t>()>=INT32_MIN && flag.get<std::int64_t>()<=INT32_MAX),"invalid visual behavior flag");
                row.action=VisualActionKey{a.at("source").get<std::string>(),flag.get<std::int32_t>(),a.at("recover").get<bool>()};
            }
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
