#include "DevelopmentControl.hpp"
#include "DevelopmentProtocol.hpp"
#include "TaskCommands.hpp"
#include <cmath>
#include <limits>

namespace hksr::research {
using Json=nlohmann::json;
using namespace framework;
namespace {
std::uint64_t Unsigned(const Json& value) {
    if(!value.is_number_integer() || (value.is_number_integer() && !value.is_number_unsigned() && value.get<std::int64_t>()<0))
        throw std::invalid_argument("expected nonnegative integer");
    return value.get<std::uint64_t>();
}
unsigned U32(const Json& value) {
    const auto n=Unsigned(value);if(n>std::numeric_limits<unsigned>::max())throw std::invalid_argument("integer exceeds uint32");
    return static_cast<unsigned>(n);
}
float Number(const Json& value) {
    if(!value.is_number())throw std::invalid_argument("expected numeric value");
    const auto n=value.get<float>();if(!std::isfinite(n))throw std::invalid_argument("expected finite value");return n;
}
api::Position3 Position(const Json& row) {return {Number(row.at("x")),Number(row.at("y")),Number(row.at("z"))};}
template<class T> Json Input(const T& value) {return {{"available",value.available},{"value",value.value},{"error",value.error}};}
Json Actions(const ActionCatalogStatus& source) {
    Json out={{"finished",source.finished},{"proposals",Json::array()},{"ready",Json::array()}};
    Json cleanup=Json::array();
    for(const auto& ability:source.abilities)if(ability.removal_sources || !ability.issues.empty()) {
        Json errors=Json::array();for(const auto& error:ability.issues)errors.push_back(error.error);
        cleanup.push_back({{"ability",ability.ability},{"removals",ability.removal_sources},{"linked",ability.linked_retirements},{"issues",errors}});
    }
    for(const auto& proposal:source.proposals) {
        Json row={{"state",proposal.state},{"domain",proposal.domain==ActionDomain::Alternate?"battle":"exploration"},{"references",Json::array()},{"retirements",proposal.retirements.size()}};
        for(const auto& ref:proposal.references) {
            Json source_ref={{"source",ref.source.path},{"field",ref.field},{"root_ability",ref.root_ability}};
            if(ref.task) {
                source_ref["ability"]=ref.task->ability;source_ref["task_path"]=Json::array();
                source_ref["enabled"]=Input(ref.task->enabled);source_ref["target"]=Input(ref.task->target_alias);
                for(const auto& step:ref.task->path)source_ref["task_path"].push_back({{"type",step.declaring_type},{"field",step.field},
                    {"selection",static_cast<int>(step.selection)},{"index",step.index},{"key",step.key}});
            }
            row["references"].push_back(std::move(source_ref));
        }
        out["proposals"].push_back(std::move(row));
    }
    for(const auto& set:source.ready_actions) {
        Json group={{"source",set->source},{"poses",Json::array()},{"transitions",Json::array()}};
        for(std::size_t index=0;index<set->poses.size();++index) {
            const auto& pose=set->poses[index];
            group["poses"].push_back({{"index",index},{"skill_index",pose.skill_index},{"variant_index",pose.variant_index},
                {"state",pose.state},{"label",pose.label},{"entry_ability",Input(pose.entry_ability)},
                {"preparation_source",pose.preparation_source},
                {"prepare_ability",Input(pose.prepare_ability)},{"use_transition",Input(pose.use_transition)},
                {"has_condition",Input(pose.has_condition)},{"effect",Input(pose.effect_path)},{"wait",Input(pose.wait)},
                {"child_skills",{{"available",pose.child_skills.available},{"values",pose.child_skills.values},{"error",pose.child_skills.error}}}});
        }
        for(const auto& edge:set->transitions.rows)group["transitions"].push_back({{"from",Input(edge.from)},{"to",Input(edge.to)},
            {"state",Input(edge.state)},{"blend",Input(edge.transition_in)},{"reverse",Input(edge.reverse_outro)}});
        out["ready"].push_back(std::move(group));
    }
    out["cleanup"]=std::move(cleanup);return out;
}
const char* Phase(SourceRequestPhase phase) {
    switch(phase) {
    case SourceRequestPhase::Queued:return "queued";
    case SourceRequestPhase::Preparing:return "preparing";
    case SourceRequestPhase::Ready:return "ready";
    case SourceRequestPhase::Unresolved:return "unresolved";
    case SourceRequestPhase::Failed:return "failed";
    }
    return "unknown";
}
const char* Phase(ActionIntentPhase phase) {
    switch(phase) {
    case ActionIntentPhase::Waiting:return "waiting";
    case ActionIntentPhase::Dispatching:return "dispatching";
    case ActionIntentPhase::Returned:return "returned";
    case ActionIntentPhase::Failed:return "failed";
    case ActionIntentPhase::Cancelled:return "cancelled";
    }
    return "unknown";
}
Json Snapshot(const ApplicationSnapshot& source,bool include_catalog,bool include_actions) {
    Json out={{"prepared",source.prepared},{"closing",source.closing},{"closed",source.closed},
        {"map_error",source.map_error},{"binding_errors",source.binding_errors},{"characters",Json::array()}};
    if(source.scene)out["scene"]={{"key",source.scene->key},{"incarnation",source.scene->incarnation}};
    out["layouts"]={{"available",source.layouts.available},{"sequence",source.layouts.sequence},{"maps",Json::object()}};
    for(const auto& entry:source.layouts.maps) {
        const auto& m=entry.second;
        out["layouts"]["maps"][entry.first]={{"initialized",m.initialized},{"busy",m.busy},{"phase",m.phase},
            {"error",m.error},{"storage_error",m.storage_error},{"operation",m.operation},{"command",m.command},
            {"revision",m.revision},{"saved_revision",m.saved_revision},{"snapshots",m.names}};
    }
    out["operations"]={{"sequence",source.operations.sequence},{"entries",Json::object()}};
    for(const auto& item:source.operations.entries) {
        const auto& value=item.second;
        static const char* kinds[]={"prepare_source","create","rebuild","replace_source","enable","disable","action","placement","gaze","delete"};
        static const char* phases[]={"waiting","running","returned","failed","cancelled","superseded"};
        out["operations"]["entries"][std::to_string(item.first)]={{"id",value.id},{"kind",kinds[static_cast<int>(value.kind)]},
            {"phase",phases[static_cast<int>(value.phase)]},{"character",value.character},{"source_request",value.source_request},
            {"revision",value.revision},{"action_intent",value.action_intent},{"creation",value.creation},
            {"terminal",value.Terminal()},{"detail",value.detail}};
    }
    if(source.catalog) {
        const auto& c=*source.catalog;
        out["catalog"]={{"error",c.error},{"table_error",c.table_error},{"sources",Json::object()}};
        for(const auto& entry:c.sources) {
            const auto& request=*entry.second;
            Json row={{"phase",Phase(request.phase)},{"error",request.error},
                {"source_finished",request.result.finished},{"source_errors",request.result.errors},
                {"npc_candidates",request.result.foundation.candidates}};
            if(request.result.character)row["package_id"]=request.result.character->id;
            if(const auto* npc=std::get_if<BrowseNpc>(&request.input))row["input"]={{"npc_id",npc->id}};
            else if(const auto* player=std::get_if<BrowsePlayer>(&request.input)) {
                row["input"]={{"player_id",player->key.id},{"enhanced",player->key.enhanced},{"skin",player->key.skin}};
                if(player->npc)row["input"]["npc_id"]=*player->npc;
            } else row["input"]={{"package_id",std::get<CharacterPackage>(request.input).id}};
            if(include_actions)row["actions"]=Actions(request.actions);
            out["catalog"]["sources"][std::to_string(entry.first)]=std::move(row);
        }
        if(include_actions) {
            auto& actors=out["catalog"]["character_actions"];actors=Json::object();
            for(const auto& entry:c.characters) {
                auto row=Actions(entry.second->actions);row["revision"]=entry.second->revision;
                actors[std::to_string(entry.first)]=std::move(row);
            }
        }
        if(include_catalog && c.rows) {
            auto& rows=out["catalog"]["rows"];
            rows={{"npcs",Json::array()},{"players",Json::array()},{"avatars",Json::array()}};
            for(const auto& row:c.rows->npcs.rows)rows["npcs"].push_back({{"id",row.id},{"character_config",row.character_config},{"model_config",row.model_config}});
            for(const auto& row:c.rows->players.rows)rows["players"].push_back({{"id",row.id},{"avatar",row.avatar},{"enhanced",row.enhanced},{"skin",row.skin},{"character_config",row.character_config},{"prefab",row.prefab}});
            for(const auto& row:c.rows->avatars.rows)rows["avatars"].push_back({{"avatar",row.avatar},{"character_config",row.character_config},{"prefab",row.prefab}});
        }
    }
    for(const auto& c:source.characters) {
        Json row={{"id",c.id},{"creations",c.creations},{"enabled",c.enabled},{"active",c.active},
            {"presentation_phase",c.presentation_phase},{"battle_idle_state",c.battle_idle_state},
            {"revision",c.revision},{"active_revision",c.active_revision},{"creation_ready",c.creation_ready},{"creation_error",c.creation_error},
            {"retiring",c.retiring},{"pending_inputs",c.pending_inputs},{"error",c.error},{"npc_id",c.package.npc_id},{"scene",c.scene},
            {"position",{{"x",c.placement.position.x},{"y",c.placement.position.y},{"z",c.placement.position.z}}},
            {"rotation",{{"x",c.placement.rotation.x},{"y",c.placement.rotation.y},{"z",c.placement.rotation.z},{"w",c.placement.rotation.w}}}};
        row["branch_state"]={{"phase",c.branch_state.phase},{"values",Json::array()}};
        for(const auto& value:c.branch_state.values) {
            Json item={{"scope",value.scope},{"name",value.name}};
            std::visit([&](const auto& v){item["value"]=v;},value.value);
            row["branch_state"]["values"].push_back(std::move(item));
        }
        if(c.placement.snap_to_ground)row["snap_to_ground"]=*c.placement.snap_to_ground;
        if(c.handoff) {
            row["handoff"]={{"phase",static_cast<int>(c.handoff->phase)},{"error",c.handoff->error}};
            if(c.handoff->ready_domain)row["handoff"]["domain"]=*c.handoff->ready_domain==ActionDomain::Alternate?"battle":"exploration";
        }
        if(c.playback && c.playback->last)row["action"]={{"serial",c.playback->last->serial},
            {"outcome",static_cast<int>(c.playback->last->outcome)},{"detail",c.playback->last->detail}};
        if(c.action) {
            row["action_intent"]={{"id",c.action->id},{"creation",c.action->creation},
                {"phase",Phase(c.action->phase)},{"detail",c.action->detail}};
            if(c.action->outcome)row["action_intent"]["outcome"]=static_cast<int>(*c.action->outcome);
        }
        if(c.outfit)row["outfit"]={{"avatar",c.outfit->avatar},{"applicable",c.outfit->applicable},
            {"gender",c.outfit->gender},{"base_type",c.outfit->base_type},{"applied",c.outfit->applied},
            {"config_refresh_required",c.outfit->config_refresh_required},{"config_refreshed",c.outfit->refresh.returned},
            {"restore_attempted",c.outfit->restore_attempted},{"closed",c.outfit->closed},{"errors",c.outfit->errors}};
        out["characters"].push_back(std::move(row));
    }
    return out;
}
}
CharacterCommand ParseDevelopmentCommand(const Json& input) {
    const auto op=input.at("op").get<std::string>();
    if(op=="layout") {
        const auto action=input.at("operation").get<std::string>();LayoutOperation operation;
        if(action=="refresh")operation=LayoutOperation::Refresh;
        else if(action=="save")operation=LayoutOperation::Save;
        else if(action=="load")operation=LayoutOperation::Load;
        else if(action=="delete")operation=LayoutOperation::Delete;
        else if(action=="retry_current")operation=LayoutOperation::RetryCurrent;
        else if(action=="keep_current")operation=LayoutOperation::KeepCurrent;
        else throw std::invalid_argument("unknown layout operation");
        return LayoutCommand{operation,input.value("name",std::string{}),input.value("overwrite",false),input.value("scene",std::string{})};
    }
    if(op=="catalog")return RequestCatalog{input.value("refresh",false)};
    if(op=="source_npc")return PrepareSource{BrowseNpc{U32(input.at("npc_id"))}};
    if(op=="source_player") {
        BrowsePlayer value{{U32(input.at("player_id")),U32(input.value("enhanced",Json(0))),U32(input.value("skin",Json(0)))},{}};
        if(input.contains("npc_id"))value.npc=U32(input.at("npc_id"));return PrepareSource{value};
    }
    if(op=="retry_source")return RetrySource{Unsigned(input.at("source_request"))};
    if(op=="release_source")return ReleaseSource{Unsigned(input.at("source_request"))};
    if(op=="create_prepared") {
        AddPreparedCharacter value;value.source_request=Unsigned(input.at("source_request"));value.near_player=input.value("near_player",true);
        if(input.contains("position"))value.placement.position=Position(input.at("position"));
        if(input.contains("rotation")) {
            const auto& rotation=input.at("rotation");
            value.placement.rotation={Number(rotation.at("x")),Number(rotation.at("y")),Number(rotation.at("z")),Number(rotation.at("w"))};
        }
        if(input.contains("offset"))value.player_world_offset=Position(input.at("offset"));return value;
    }
    const auto id=Unsigned(input.at("character"));
    if(op.compare(0,5,"task_")==0)return ParseTaskCommand(input);
    if(op=="observe")return ObserveCharacter{id};
    if(!id)throw std::invalid_argument("character must identify a framework-owned entry");
    if(op=="select")return BrowseCharacterActions{id};
    if(op=="controllers")return ReadCharacterControllerActions{id};
    if(op=="replace_prepared")return ReplacePreparedCharacter{id,Unsigned(input.at("source_request")),{}, {}};
    if(op=="enable")return EnableCharacter{id,input.at("enabled").get<bool>()};
    if(op=="delete")return DeleteCharacter{id};
    if(op=="idle")return IdleCharacter{id};
    if(op=="rebuild")return RebuildCharacter{id};
    if(op=="npc_motion")return SelectCharacterAction{id,{ActionDomain::Exploration,NpcMotion{U32(input.at("motion_id"))}}};
    if(op=="program_action")return SelectCharacterAction{id,{ActionDomain::Alternate,ProgramAction{
        input.at("program").get<std::string>(),input.at("action").get<std::string>(),input.value("choices",std::map<std::string,std::string>{})}}};
    if(op=="configured_state" || op=="ready_pose") {
        CatalogActionSelection selection;selection.revision=Unsigned(input.at("revision"));
        if(op=="ready_pose")selection.key=ReadyActionKey{input.at("source").get<std::string>(),
            static_cast<std::size_t>(Unsigned(input.at("pose")))};
        else {
            const auto domain=input.at("domain").get<std::string>();
            if(domain!="battle" && domain!="exploration")throw std::invalid_argument("invalid configured action domain");
            selection.key=ConfiguredActionKey{domain=="battle"?ActionDomain::Alternate:ActionDomain::Exploration,input.at("name").get<std::string>()};
        }
        return SelectCatalogAction{id,std::move(selection)};
    }
    if(op=="state" || op=="free_state") {
        const auto domain=input.at("domain").get<std::string>();
        if(domain!="battle" && domain!="exploration")throw std::invalid_argument("domain must be battle or exploration");
        const auto name=input.at("name").get<std::string>();if(name.empty())throw std::invalid_argument("state name is empty");
        const auto layer=U32(input.value("layer",Json(0)));if(layer>INT_MAX)throw std::invalid_argument("layer exceeds int32");
        const auto progress=Number(input.value("normalized_time",Json(0)));
        if(op=="free_state")return SelectCharacterAction{id,{domain=="battle"?ActionDomain::Alternate:ActionDomain::Exploration,
            FreeStyleState{name,Number(input.value("blend",Json(0))),progress}}};
        return SelectCharacterAction{id,{domain=="battle"?ActionDomain::Alternate:ActionDomain::Exploration,
            AnimatorState{name,static_cast<int>(layer),progress}}};
    }
    if(op=="place") {
        CharacterPlacement value;value.position=Position(input.at("position"));
        if(input.contains("snap_to_ground"))value.snap_to_ground=input.at("snap_to_ground").get<bool>();
        const auto& rotation=input.at("rotation");
        value.rotation={Number(rotation.at("x")),Number(rotation.at("y")),Number(rotation.at("z")),Number(rotation.at("w"))};
        return PlaceCharacter{id,value,input.value("relative",false)};
    }
    if(op=="look") {
        LookIntent value;const auto mode=input.at("mode").get<std::string>();
        if(mode=="automatic")value.mode=LookMode::Automatic;
        else if(mode=="disabled")value.mode=LookMode::Disabled;
        else if(mode=="player")value.mode=LookMode::Player;
        else if(mode=="instance"){value.mode=LookMode::Instance;value.target=Unsigned(input.at("target"));}
        else if(mode=="point"){value.mode=LookMode::Point;value.point=Position(input.at("point"));}
        else throw std::invalid_argument("unknown gaze mode");
        value.max_distance=Number(input.value("max_distance",Json(0)));return LookCharacter{id,value};
    }
    throw std::invalid_argument("unsupported development command: "+op);
}
struct DevelopmentControl::Replies {
    struct Completed {std::string request;std::size_t index;CommandReply value;};
    using Record=std::variant<Json,Completed>;
    std::mutex mutex;std::vector<Record> rows;
};
DevelopmentControl::DevelopmentControl(unsigned process,Services services)
    :replies_(std::make_shared<Replies>()),services_(std::move(services)),process_(process){}
void DevelopmentControl::Submit(const std::string& text) {
    if(text.find_first_not_of(" \t\r\n")==std::string::npos)return;
    std::string id;
    auto emit=[this](Json row){std::lock_guard<std::mutex> lock(replies_->mutex);replies_->rows.push_back(std::move(row));};
    try {
        const auto input=Json::parse(text);
        if(input.at("format")!="hksr.development-commands")throw std::invalid_argument("unknown command format");
        if(U32(input.at("process_id"))!=process_)throw std::invalid_argument("request targets a different process; not replayed");
        id=input.at("request_id").get<std::string>();if(id.empty())throw std::invalid_argument("request_id is empty");
        if(!seen_.insert(id).second){emit({{"request_id",id},{"event","duplicate_ignored"}});return;}
        const auto& commands=input.at("commands");if(!commands.is_array())throw std::invalid_argument("commands must be an array");
        for(std::size_t i=0;i<commands.size();++i) {
            try {
                if(commands[i].at("op")=="snapshot") {
                    emit({{"request_id",id},{"index",i},{"event","snapshot"},{"snapshot",Snapshot(services_.snapshot(),commands[i].value("catalog",false),commands[i].value("actions",false))}});continue;
                }
                if(commands[i].at("op")=="visual_inputs") {
                    const auto& command=commands[i];
                    for(auto it=command.begin();it!=command.end();++it)
                        if(it.key()!="op" && it.key()!="character" && it.key()!="targets" && it.key()!="choices" && it.key()!="anchors")
                            throw std::invalid_argument("unknown visual input field: "+it.key());
                    const auto character=Unsigned(command.at("character"));
                    if(!character)throw std::invalid_argument("visual input requires an instance ID");
                    VisualProgramParameters parameters;
                    const auto targets=command.value("targets",Json::object());
                    if(!targets.is_object())throw std::invalid_argument("visual targets must be an object");
                    for(auto it=targets.begin();it!=targets.end();++it) {
                        const auto target=Unsigned(it.value());
                        if(it.key().empty() || it.key()=="Caster" || it.key().find('\0')!=std::string::npos || !target)
                            throw std::invalid_argument("visual target requires an alias and positive instance ID");
                        parameters.targets.emplace(it.key(),target);
                    }
                    parameters.choices=command.value("choices",Json::object()).get<std::map<std::string,std::string>>();
                    parameters.anchors=ParseVisualSpatialAnchors(command.value("anchors",Json::object()).dump());
                    if(!services_.visual_inputs)throw std::logic_error("visual input control unavailable");
                    services_.visual_inputs(character,std::move(parameters));
                    emit({{"request_id",id},{"index",i},{"event","visual_inputs_set"},{"character",character},
                        {"applies","next_program_action"}});continue;
                }
                if(commands[i].at("op")=="visual_signal") {
                    const auto& command=commands[i];
                    for(auto it=command.begin();it!=command.end();++it)
                        if(it.key()!="op" && it.key()!="character" && it.key()!="generation" && it.key()!="intent" && it.key()!="name")
                            throw std::invalid_argument("unknown visual signal field: "+it.key());
                    const auto character=Unsigned(command.at("character"));
                    VisualProgramSignal signal{Unsigned(command.at("generation")),Unsigned(command.at("intent")),command.at("name").get<std::string>()};
                    if(!character || !signal.generation || !signal.intent || signal.name.empty() || signal.name.find('\0')!=std::string::npos)
                        throw std::invalid_argument("visual signal requires instance, generation, intent and name");
                    if(!services_.visual_signal)throw std::logic_error("visual signal control unavailable");
                    services_.visual_signal(character,std::move(signal));
                    emit({{"request_id",id},{"index",i},{"event","visual_signal_queued"},{"character",character}});continue;
                }
                auto queue=replies_;
                auto command=ParseDevelopmentCommand(commands[i]);
                const auto serial=services_.post(std::move(command),[queue,id,i](const CommandReply& reply){
                    // Frame callback copies a typed result; worker encodes it.
                    std::lock_guard<std::mutex> lock(queue->mutex);
                    queue->rows.emplace_back(Replies::Completed{id,i,reply});
                });
                emit({{"request_id",id},{"index",i},{"event","queued"},{"serial",serial}});
            } catch(const std::exception& e){emit({{"request_id",id},{"index",i},{"event","command_error"},{"error",e.what()}});}
        }
    }catch(const std::exception& e){emit({{"request_id",id},{"event","request_error"},{"error",e.what()}});}
}
std::vector<std::string> DevelopmentControl::Drain() {
    std::vector<Replies::Record> rows;
    {std::lock_guard<std::mutex> lock(replies_->mutex);rows.swap(replies_->rows);}
    std::vector<std::string> result;result.reserve(rows.size());
    for(const auto& record:rows)result.push_back(std::visit([](const auto& row)->std::string {
        using T=std::decay_t<decltype(row)>;
        if constexpr(std::is_same_v<T,Json>)return row.dump();
        else {
            const auto& reply=row.value;
            Json result={{"request_id",row.request},{"index",row.index},{"event","command_reply"},{"serial",reply.serial},
                {"accepted",reply.accepted},{"character",reply.character},{"source_request",reply.source_request},
                {"action_intent",reply.action_intent},{"operation",reply.operation},{"error",reply.error}};
            if(reply.task)result["task"]=TaskResult(*reply.task);
            return result.dump();
        }
    },record));
    return result;
}
}
