#include "HttpProtocol.hpp"
#include "../serialization/TaskControlCodec.hpp"
#include <hksr/framework/application/CharacterTasks.hpp>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>

namespace hksr::framework::http {
namespace {
std::string Text(const Json& value) {
    if(!value.is_string())throw std::invalid_argument("expected string");
    auto out=value.get<std::string>();
    if(out.empty() || out.find('\0')!=std::string::npos)throw std::invalid_argument("expected nonempty string without NUL");
    return out;
}
void Fields(const Json& value,std::initializer_list<const char*> names) {
    if(!value.is_object())throw std::invalid_argument("expected object");
    for(auto it=value.begin();it!=value.end();++it) {
        bool known=false;for(const auto* name:names)known=known || it.key()==name;
        if(!known)throw std::invalid_argument("unknown field: "+it.key());
    }
}
float Number(const Json& value) {
    if(!value.is_number())throw std::invalid_argument("expected finite number");
    const auto out=value.get<float>();if(!std::isfinite(out))throw std::invalid_argument("expected finite number");return out;
}
unsigned U32(const Json& value) {
    const auto id=Id(value);if(id>std::numeric_limits<unsigned>::max())throw std::invalid_argument("ID exceeds uint32");
    return static_cast<unsigned>(id);
}
api::Position3 Position(const Json& value) {
    Fields(value,{"x","y","z"});return {Number(value.at("x")),Number(value.at("y")),Number(value.at("z"))};
}
CharacterPlacement Placement(const Json& value) {
    Fields(value,{"position","rotation","snap_to_ground"});CharacterPlacement out;
    if(value.contains("snap_to_ground"))out.snap_to_ground=value.at("snap_to_ground").get<bool>();
    out.position=Position(value.at("position"));
    const auto& q=value.at("rotation");Fields(q,{"x","y","z","w"});
    out.rotation={Number(q.at("x")),Number(q.at("y")),Number(q.at("z")),Number(q.at("w"))};
    const double norm=double(out.rotation.x)*out.rotation.x+double(out.rotation.y)*out.rotation.y+
        double(out.rotation.z)*out.rotation.z+double(out.rotation.w)*out.rotation.w;
    if(std::abs(norm-1)>0.001)throw std::invalid_argument("rotation must be a unit quaternion");
    return out;
}
ActionDomain Domain(const Json& value) {
    const auto text=Text(value);
    if(text=="exploration")return ActionDomain::Exploration;
    if(text=="battle")return ActionDomain::Alternate;
    throw std::invalid_argument("domain must be exploration or battle");
}
const char* Domain(ActionDomain value) {return value==ActionDomain::Alternate?"battle":"exploration";}
Json Vector(api::Position3 value) {return {{"x",value.x},{"y",value.y},{"z",value.z}};}
Json Rotation(api::Rotation4 value) {return {{"x",value.x},{"y",value.y},{"z",value.z},{"w",value.w}};}
Json Pose(CharacterPlacement value) {
    Json out={{"position",Vector(value.position)},{"rotation",Rotation(value.rotation)}};
    if(value.snap_to_ground)out["snap_to_ground"]=*value.snap_to_ground;
    return out;
}
Json Actor(const SceneCharacterStatus& c) {
    static const char* looks[]={"automatic","disabled","player","instance","point"};
    Json out={{"id",std::to_string(c.id)},{"name",c.package.display_name},{"package",c.package.id},
        {"npc_id",std::to_string(c.package.npc_id)},{"avatar_id",std::to_string(c.package.avatar_id)},
        {"creation",std::to_string(c.creations)},{"revision",std::to_string(c.revision)},
        {"active_revision",std::to_string(c.active_revision)},{"enabled",c.enabled},{"active",c.active},
        {"creation_ready",c.creation_ready},{"creation_error",c.creation_error},{"error",c.error},{"deleting",c.deleting},
        {"retiring",c.retiring},{"pending_inputs",c.pending_inputs},{"scene",c.scene},{"placement",Pose(c.placement)},
        {"look",{{"mode",looks[static_cast<int>(c.look.mode)]},{"target",std::to_string(c.look.target)},{"point",Vector(c.look.point)},{"max_distance",c.look.max_distance}}},
        {"look_support",c.look_support},{"battle_idle_state",c.battle_idle_state},{"presentation_phase",c.presentation_phase}};
    out["branch_state"]={{"phase",c.branch_state.phase},{"values",Json::array()}};
    for(const auto& value:c.branch_state.values) {
        Json item={{"scope",value.scope},{"name",value.name}};
        std::visit([&](const auto& v){item["value"]=v;},value.value);
        out["branch_state"]["values"].push_back(std::move(item));
    }
    if(c.controls)out["controls"]={{"position",Vector(c.controls->live.position)},{"rotation",Rotation(c.controls->live.rotation)},
        {"ready",c.controls->live.ready},{"error",c.controls->live.error},{"placement_pending",c.controls->placement_pending},
        {"placement_phase",PlacementPhaseName(c.controls->placement_phase)},{"gaze_phase",GazePhaseName(c.controls->gaze_phase)},
        {"placement_error",c.controls->placement_error},{"gaze_error",c.controls->gaze_error}};
    if(c.handoff)out["domain"]=c.handoff->ready_domain?Json(Domain(*c.handoff->ready_domain)):Json(nullptr);
    if(c.action) {
        static const char* phases[]={"waiting","dispatching","returned","failed","cancelled"};
        out["action"]={{"id",std::to_string(c.action->id)},{"creation",std::to_string(c.action->creation)},
            {"phase",phases[static_cast<int>(c.action->phase)]},{"detail",c.action->detail}};
    }
    if(c.playback)out["playback"]={{"pending",c.playback->pending},{"completion_pending",c.playback->completion_pending},
        {"completion_note",c.playback->completion_note},{"effect_note",c.playback->effect_note},
        {"ready_note",c.playback->ready_note},{"visual_note",c.playback->visual_note}};
    if(c.tasks)out["tasks"]={{"catalog",c.task_catalog},{"revision",std::to_string(c.task_catalog_revision)},
        {"domain",Domain(c.tasks->domain)},{"control",task_codec::TaskResult(c.tasks->control)}};
    return out;
}
Json Operation(const OperationStatus& value) {
    static const char* kinds[]={"prepare_source","create","rebuild","replace_source","enable","disable","action","placement","gaze","delete"};
    static const char* phases[]={"waiting","running","returned","failed","cancelled","superseded"};
    return {{"id",std::to_string(value.id)},{"kind",kinds[static_cast<int>(value.kind)]},
        {"phase",phases[static_cast<int>(value.phase)]},{"character",std::to_string(value.character)},
        {"source_request",std::to_string(value.source_request)},{"revision",std::to_string(value.revision)},
        {"creation",std::to_string(value.creation)},{"action_intent",std::to_string(value.action_intent)},
        {"terminal",value.Terminal()},{"detail",value.detail}};
}
Json Runtime(const ApplicationSnapshot& s) {
    Json out={{"prepared",s.prepared},{"closing",s.closing},{"closed",s.closed},{"camera_presentations",s.camera_presentations},
        {"map_error",s.map_error},{"binding_errors",s.binding_errors},{"scene",nullptr}};
    if(s.scene)out["scene"]={{"key",s.scene->key},{"incarnation",std::to_string(s.scene->incarnation)}};
    out["layouts"]={{"available",s.layouts.available},{"sequence",std::to_string(s.layouts.sequence)},{"maps",Json::object()}};
    for(const auto& entry:s.layouts.maps) {
        const auto& m=entry.second;
        out["layouts"]["maps"][entry.first]={{"initialized",m.initialized},{"busy",m.busy},{"phase",m.phase},
            {"error",m.error},{"storage_error",m.storage_error},{"operation",m.operation},{"name",m.name},
            {"command",std::to_string(m.command)},{"revision",std::to_string(m.revision)},
            {"saved_revision",std::to_string(m.saved_revision)},{"snapshots",m.names}};
    }
    return out;
}
Json Sources(const ApplicationSnapshot& s) {
    Json out=Json::object();if(!s.catalog)return out;
    static const char* phases[]={"queued","preparing","ready","unresolved","failed"};
    for(const auto& entry:s.catalog->sources) {
        const auto& r=*entry.second;
        Json row={{"phase",phases[static_cast<int>(r.phase)]},{"error",r.error},{"errors",r.result.errors},
            {"finished",r.result.finished},{"npc_candidates",r.result.foundation.candidates}};
        if(const auto* npc=std::get_if<BrowseNpc>(&r.input))row["input"]={{"kind","npc"},{"npc_id",std::to_string(npc->id)}};
        else if(const auto* player=std::get_if<BrowsePlayer>(&r.input)) {
            row["input"]={{"kind","player"},{"player_id",std::to_string(player->key.id)},
                {"enhanced",player->key.enhanced},{"skin",player->key.skin}};
            if(player->npc)row["input"]["npc_id"]=std::to_string(*player->npc);
        }else row["input"]={{"kind","package"},{"package_id",std::get<CharacterPackage>(r.input).id}};
        if(r.result.character)row["package"]={{"id",r.result.character->id},{"name",r.result.character->display_name}};
        out[std::to_string(entry.first)]=std::move(row);
    }
    return out;
}
Json Configured(ActionDomain domain,const std::string& state) {return {{"kind","configured"},{"domain",Domain(domain)},{"state",state}};}
}
std::uint64_t Id(const Json& value) {
    // IDs are strings on the wire. Also accept exact, safe JSON integers for
    // convenience, never a floating-point conversion or silently wrapped sign.
    if(value.is_number_unsigned()) {
        const auto n=value.get<std::uint64_t>();if(n<=9007199254740991ULL)return n;
    } else if(value.is_number_integer()) {
        const auto n=value.get<std::int64_t>();if(n>=0 && n<=9007199254740991LL)return static_cast<std::uint64_t>(n);
    } else if(value.is_string()) {
        const auto text=value.get<std::string>();std::uint64_t out=0;
        const auto r=std::from_chars(text.data(),text.data()+text.size(),out);
        if(r.ec==std::errc() && r.ptr==text.data()+text.size())return out;
    }
    throw std::invalid_argument("expected decimal ID string or safe nonnegative integer");
}
CharacterCommand Command(const Json& v) {
    const auto op=Text(v.at("op"));
    if(op=="task_query" || op=="task_select" || op=="task_finish" || op=="task_cancel") {
        (void)Id(v.at("character"));(void)Id(v.at("creation"));
        if(v.contains("targets"))for(const auto& target:v.at("targets").items())if(!target.value().is_null())(void)Id(target.value());
        return task_codec::ParseTaskCommand(v);
    }
    if(op=="layout") {
        Fields(v,{"op","operation","name","overwrite","scene"});const auto action=Text(v.at("operation"));
        LayoutOperation operation;
        if(action=="refresh")operation=LayoutOperation::Refresh;
        else if(action=="save")operation=LayoutOperation::Save;
        else if(action=="load")operation=LayoutOperation::Load;
        else if(action=="delete")operation=LayoutOperation::Delete;
        else if(action=="retry_current")operation=LayoutOperation::RetryCurrent;
        else if(action=="keep_current")operation=LayoutOperation::KeepCurrent;
        else throw std::invalid_argument("unknown layout operation");
        return LayoutCommand{operation,v.value("name",std::string{}),v.value("overwrite",false),v.value("scene",std::string{})};
    }
    if(op=="catalog") {Fields(v,{"op","refresh"});return RequestCatalog{v.value("refresh",false)};}
    if(op=="prepare_source") {
        Fields(v,{"op","source"});const auto& s=v.at("source");const auto kind=Text(s.at("kind"));
        if(kind=="npc") {Fields(s,{"kind","npc_id"});return PrepareSource{BrowseNpc{U32(s.at("npc_id"))}};}
        if(kind=="player") {
            Fields(s,{"kind","player_id","enhanced","skin","npc_id"});BrowsePlayer source;
            source.key={U32(s.at("player_id")),s.contains("enhanced")?U32(s.at("enhanced")):0,s.contains("skin")?U32(s.at("skin")):0};
            if(s.contains("npc_id"))source.npc=U32(s.at("npc_id"));return PrepareSource{source};
        }
        throw std::invalid_argument("source kind must be npc or player; external asset upload is not exposed");
    }
    if(op=="retry_source" || op=="release_source") {
        Fields(v,{"op","source_request"});const auto id=Id(v.at("source_request"));
        if(op=="retry_source")return RetrySource{id};return ReleaseSource{id};
    }
    if(op=="create") {
        Fields(v,{"op","source_request","scene","placement","near_player","player_world_offset","task_catalog","transient"});
        AddPreparedCharacter out;out.source_request=Id(v.at("source_request"));out.scene=v.value("scene",std::string());
        out.task_catalog=v.value("task_catalog",std::string());out.transient=v.value("transient",false);
        if(v.contains("task_catalog") && (out.task_catalog.empty() || out.task_catalog.find('\0')!=std::string::npos))
            throw std::invalid_argument("task_catalog must be a nonempty catalog ID");
        if(v.contains("placement"))out.placement=Placement(v.at("placement"));out.near_player=v.value("near_player",false);
        if(v.contains("player_world_offset"))out.player_world_offset=Position(v.at("player_world_offset"));return out;
    }
    if(op=="replace_source") {
        Fields(v,{"op","character","source_request"});return ReplacePreparedCharacter{Id(v.at("character")),Id(v.at("source_request")),{}, {}};
    }
    if(op=="enable") {Fields(v,{"op","character","enabled"});return EnableCharacter{Id(v.at("character")),v.at("enabled").get<bool>()};}
    if(op=="rebuild") {Fields(v,{"op","character"});return RebuildCharacter{Id(v.at("character"))};}
    if(op=="delete") {Fields(v,{"op","character"});return DeleteCharacter{Id(v.at("character"))};}
    if(op=="idle") {Fields(v,{"op","character"});return IdleCharacter{Id(v.at("character"))};}
    if(op=="browse_actions") {Fields(v,{"op","character"});return BrowseCharacterActions{Id(v.at("character"))};}
    if(op=="browse_npc_actions") {Fields(v,{"op","character"});return ReadCharacterNpcActions{Id(v.at("character"))};}
    if(op=="place") {
        Fields(v,{"op","character","placement","relative"});return PlaceCharacter{Id(v.at("character")),Placement(v.at("placement")),v.value("relative",false)};
    }
    if(op=="look") {
        Fields(v,{"op","character","mode","target","point","max_distance"});LookIntent look;const auto mode=Text(v.at("mode"));
        if(v.contains("max_distance")){look.max_distance=Number(v.at("max_distance"));if(look.max_distance<0)throw std::invalid_argument("gaze distance must be nonnegative");}
        if(mode=="automatic")look.mode=LookMode::Automatic;
        else if(mode=="disabled")look.mode=LookMode::Disabled;
        else if(mode=="player")look.mode=LookMode::Player;
        else if(mode=="instance"){look.mode=LookMode::Instance;look.target=Id(v.at("target"));}
        else if(mode=="point"){look.mode=LookMode::Point;look.point=Position(v.at("point"));}
        else throw std::invalid_argument("unknown gaze mode");
        return LookCharacter{Id(v.at("character")),look};
    }
    if(op=="action") {
        Fields(v,{"op","character","revision","action"});const auto id=Id(v.at("character"));
        const auto& a=v.at("action");const auto kind=Text(a.at("kind"));
        if(kind=="npc") {Fields(a,{"kind","id"});return SelectCharacterAction{id,{ActionDomain::Exploration,NpcMotion{U32(a.at("id"))}}};}
        if(kind=="program") {
            Fields(a,{"kind","program","action","choices"});
            ProgramAction selection{Text(a.at("program")),Text(a.at("action")),a.value("choices",std::map<std::string,std::string>{})};
            for(const auto& [key,value]:selection.choices){Text(key);Text(value);}
            return SelectCharacterAction{id,{ActionDomain::Alternate,std::move(selection)}};
        }
        const auto revision=Id(v.at("revision"));
        if(kind=="configured") {
            Fields(a,{"kind","domain","state"});return SelectCatalogAction{id,{revision,ConfiguredActionKey{Domain(a.at("domain")),Text(a.at("state"))}}};
        }
        if(kind=="ready") {
            Fields(a,{"kind","source","pose"});return SelectCatalogAction{id,{revision,ReadyActionKey{Text(a.at("source")),static_cast<std::size_t>(Id(a.at("pose")))}}};
        }
        if(kind=="visual_behavior") {
            Fields(a,{"kind","source","flag","recover"});const auto& flag=a.at("flag");
            if(!flag.is_number_integer() || (flag.is_number_unsigned()?flag.get<std::uint64_t>()>INT32_MAX:
                flag.get<std::int64_t>()<INT32_MIN || flag.get<std::int64_t>()>INT32_MAX))
                throw std::invalid_argument("visual behavior flag must be int32");
            return SelectCatalogAction{id,{revision,VisualActionKey{Text(a.at("source")),flag.get<std::int32_t>(),a.at("recover").get<bool>()}}};
        }
        throw std::invalid_argument("unknown action kind");
    }
    throw std::invalid_argument("unknown command op");
}
Json Reply(const CommandReply& r) {
    Json out={{"serial",std::to_string(r.serial)},{"accepted",r.accepted},{"error",r.error},
        {"character",std::to_string(r.character)},{"source_request",std::to_string(r.source_request)},
        {"action_intent",std::to_string(r.action_intent)},{"operation",std::to_string(r.operation)}};
    if(r.task)out["task"]=task_codec::TaskResult(*r.task);
    return out;
}
Json Snapshot(const ApplicationSnapshot& s) {
    Json out={{"runtime",Runtime(s)},{"characters",Json::object()},{"operations",Json::object()},
        {"sources",Sources(s)},{"catalog_url","/v1/catalog"},{"actions_url_template","/v1/characters/{id}/actions"},
        {"tasks_url_template","/v1/characters/{id}/tasks"}};
    for(const auto& c:s.characters)out["characters"][std::to_string(c.id)]=Actor(c);
    for(const auto& o:s.operations.entries)out["operations"][std::to_string(o.first)]=Operation(o.second);
    return out;
}
Json Catalog(const ApplicationSnapshot& s) {
    Json out={{"available",bool(s.catalog)},{"sources",Sources(s)},{"npcs",Json::array()},{"players",Json::array()}};
    if(!s.catalog)return out;
    const auto& c=*s.catalog;out["requested"]=c.requested;out["attempted"]=c.attempted;out["error"]=c.error;out["table_error"]=c.table_error;
    if(c.rows) {
        for(const auto& r:c.rows->npcs.rows)out["npcs"].push_back({{"npc_id",std::to_string(r.id)},
            {"character_config",r.character_config},{"model_config",r.model_config}});
        for(const auto& r:c.rows->players.rows)out["players"].push_back({{"player_id",std::to_string(r.id)},
            {"avatar",std::to_string(r.avatar)},{"enhanced",r.enhanced},{"skin",r.skin},
            {"character_config",r.character_config},{"prefab",r.prefab}});
    }
    return out;
}
Json Actions(const ApplicationSnapshot& s,std::uint64_t character) {
    const SceneCharacterStatus* actor=nullptr;for(const auto& c:s.characters)if(c.id==character){actor=&c;break;}
    if(!actor)throw std::out_of_range("character not found");
    Json out={{"character",std::to_string(character)},{"available",!actor->program_actions.empty()},{"revision","0"},{"finished",false},
        {"configured",Json::array()},{"ready",Json::array()},{"npc",Json::array()},{"program",Json::array()},{"reactions",Json::array()}};
    for(const auto& [key,state]:actor->program_actions)out["program"].push_back({
        {"action",{{"kind","program"},{"program",actor->visual_program},{"action",key}}},{"state",state}});
    if(const auto* c=s.catalog?s.catalog->Actions(character):nullptr) {
        out["available"]=true;out["revision"]=std::to_string(c->revision);out["finished"]=c->actions.finished;
        out["domain_ready"]={{"exploration",c->actions.Ready(ActionDomain::Exploration)},
            {"battle",c->actions.Ready(ActionDomain::Alternate)}};
        for(const auto& p:c->actions.proposals)out["configured"].push_back(Configured(p.domain,p.state));
        for(const auto& row:c->actions.visual_states)if(row.value.flag.available)for(bool recover:{false,true}) {
            const auto query=QueryVisualAction(row,recover);
            out["reactions"].push_back({{"action",{{"kind","visual_behavior"},{"source",row.character_config},
                {"flag",row.value.flag.value},{"recover",recover}}},{"available",query.available},{"issues",query.issues},
                {"body_source",row.source},{"expression_source",row.expression_source},
                {"occur",row.value.occur.resolved_state.value},{"loop",row.value.loop.resolved_state.value},
                {"recovery",row.value.recover.resolved_state.value},{"expression",row.expression.value},
                {"scope",{"body","expression","blink"}}});
        }
        for(const auto& set:c->actions.ready_actions)for(std::size_t i=0;i<set->poses.size();++i) {
            const auto& pose=set->poses[i];Json attacks=Json::array();
            for(const auto* a:ReadyAttacks(c->actions,*set,pose))attacks.push_back(Configured(a->domain,a->state));
            out["ready"].push_back({{"action",{{"kind","ready"},{"source",set->source},{"pose",std::to_string(i)}}},
                {"label",pose.label},{"state",pose.state},{"attacks",std::move(attacks)}});
        }
    }
    const auto npc=s.npc_actions.find(character);
    if(npc!=s.npc_actions.end() && npc->second->creation==actor->creations)for(const auto& m:npc->second->source.motions)
        out["npc"].push_back({{"action",{{"kind","npc"},{"id",std::to_string(m.id)}}},
            {"name",m.name.available?m.name.value:m.indexed_name}});
    return out;
}
Json Tasks(const ApplicationSnapshot& snapshot,std::uint64_t id) {
    Json entries=Json::array();
    for(const auto& entry:CharacterTasks(snapshot,id)) {
        static const char* kinds[]={"npc","configured_clip","ready","reaction","program","sequence","idle"};
        Json command=std::visit([](const auto& value)->Json {
            using T=std::decay_t<decltype(value)>;
            if constexpr(std::is_same_v<T,ControlCharacterTask>)return task_codec::TaskCommand(value);
            else if constexpr(std::is_same_v<T,IdleCharacter>)return {{"op","idle"},{"character",std::to_string(value.id)}};
            else if constexpr(std::is_same_v<T,SelectCharacterAction>) {
                Json action;
                if(const auto* npc=std::get_if<NpcMotion>(&value.action.request))action={{"kind","npc"},{"id",std::to_string(npc->id)}};
                else if(const auto* program=std::get_if<ProgramAction>(&value.action.request))
                    action={{"kind","program"},{"program",program->program},{"action",program->action},{"choices",program->choices}};
                else throw std::logic_error("unsupported task directory action backend");
                return {{"op","action"},{"character",std::to_string(value.id)},{"action",std::move(action)}};
            } else if constexpr(std::is_same_v<T,SelectCatalogAction>) {
                auto action=std::visit([](const auto& key)->Json {
                    using K=std::decay_t<decltype(key)>;
                    if constexpr(std::is_same_v<K,ConfiguredActionKey>)return Configured(key.domain,key.state);
                    else if constexpr(std::is_same_v<K,ReadyActionKey>)return {{"kind","ready"},{"source",key.source},{"pose",std::to_string(key.pose)}};
                    else return {{"kind","visual_behavior"},{"source",key.character_config},{"flag",key.flag},{"recover",key.recover}};
                },value.action.key);
                return {{"op","action"},{"character",std::to_string(value.id)},{"revision",std::to_string(value.action.revision)},{"action",std::move(action)}};
            } else throw std::logic_error("unsupported task directory command");
        },entry.command);
        const char* available=entry.query.availability==TaskAvailability::Available?"available":
            entry.query.availability==TaskAvailability::Unavailable?"unavailable":"unknown";
        Json reasons=Json::array(),sources=Json::array(),inputs=Json::object();
        for(const auto& reason:entry.query.reasons)reasons.push_back({{"source",reason.source},{"detail",reason.detail}});
        for(const auto& source:entry.sources)sources.push_back({{"document",source.document},{"field",source.field}});
        for(const auto& input:entry.inputs) {
            static const char* types[]={"bool","integer","number","text"};
            Json description={{"type",types[static_cast<int>(input.type)]}};
            if(input.default_value)std::visit([&](const auto& value){description["default"]=value;},*input.default_value);
            inputs[input.name]=std::move(description);
        }
        entries.push_back({{"backend",kinds[static_cast<int>(entry.kind)]},{"domain",Domain(entry.domain)},
            {"label",entry.label},{"detail",entry.detail},{"recovery",entry.recovery},{"availability",available},
            {"reasons",std::move(reasons)},{"sources",std::move(sources)},{"inputs",std::move(inputs)},{"command",std::move(command)}});
    }
    return {{"character",std::to_string(id)},{"entries",std::move(entries)}};
}
std::string Cursor(const ApplicationCursor& c) {return c.stream+":"+std::to_string(c.sequence);}
ApplicationCursor ParseCursor(const std::string& text) {
    const auto split=text.find(':');
    if(split!=32 || text.size()<=33)throw std::invalid_argument("invalid event cursor");
    for(std::size_t i=0;i<split;++i)if(!((text[i]>='0' && text[i]<='9') || (text[i]>='a' && text[i]<='f')))
        throw std::invalid_argument("invalid event cursor");
    return {text.substr(0,split),Id(text.substr(split+1))};
}
Json Updates(const ApplicationUpdates& u) {
    Json out={{"cursor",Cursor(u.changes.cursor)},{"reset",u.changes.reset}};
    if(u.changes.reset) {out["snapshot"]=Snapshot(u.snapshot);return out;}
    std::set<std::uint64_t> actors,operations;bool runtime=false,catalog=false,inspection=false;
    for(const auto& c:u.changes.events) {
        actors.insert(c.characters.begin(),c.characters.end());operations.insert(c.operations.begin(),c.operations.end());
        runtime=runtime || c.runtime;catalog=catalog || c.catalog;inspection=inspection || c.inspection;
    }
    out["characters"]=Json::object();out["operations"]=Json::object();
    for(const auto id:actors) {
        Json value=nullptr;for(const auto& c:u.snapshot.characters)if(c.id==id){value=Actor(c);break;}
        out["characters"][std::to_string(id)]=std::move(value);
    }
    for(const auto id:operations) {
        const auto found=u.snapshot.operations.entries.find(id);
        out["operations"][std::to_string(id)]=found==u.snapshot.operations.entries.end()?Json(nullptr):Operation(found->second);
    }
    if(runtime)out["runtime"]=Runtime(u.snapshot);
    if(catalog)out["sources"]=Sources(u.snapshot);
    out["catalog_changed"]=catalog;out["actions_changed"]=catalog || inspection;
    return out;
}
std::string Event(const ApplicationUpdates& u) {
    return "id: "+Cursor(u.changes.cursor)+"\nevent: "+(u.changes.reset?"reset":"update")+
        "\ndata: "+Updates(u).dump(-1,' ',false,Json::error_handler_t::replace)+"\n\n";
}
}
