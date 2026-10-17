#include <hksr/framework/presentation/VisualProgram.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace hksr::framework {
namespace {
using Json=nlohmann::json;
void Need(bool condition,const std::string& message){if(!condition)throw std::invalid_argument(message);}
void Fields(const Json& row,std::initializer_list<const char*> names) {
    Need(row.is_object(),"visual entry must be an object");
    for(auto i=row.begin();i!=row.end();++i) {
        bool known=false;for(const auto* name:names)known=known || i.key()==name;
        Need(known,"unknown visual field: "+i.key());
    }
}
std::string Name(const Json& value) {
    const auto text=value.get<std::string>();Need(!text.empty() && text.find('\0')==std::string::npos,"invalid visual name");return text;
}
float Number(const Json& value,bool nonnegative=false) {
    Need(value.is_number(),"visual number expected");const auto n=value.get<float>();
    Need(std::isfinite(n) && (!nonnegative || n>=0),"invalid visual number");return n;
}
api::Position3 Vector(const Json& value) {
    Need(value.is_array() && value.size()==3,"visual vector must have three values");
    return {Number(value[0]),Number(value[1]),Number(value[2])};
}
std::vector<std::string> Names(const Json& value) {
    Need(value.is_array() && !value.empty(),"visual effect list must be nonempty");
    std::vector<std::string> out;std::set<std::string> unique;
    for(const auto& item:value){auto name=Name(item);Need(unique.insert(name).second,"duplicate effect key: "+name);out.push_back(std::move(name));}
    return out;
}
std::vector<VisualInstruction> Instructions(const Json& rows,const VisualProgram& program) {
    Need(rows.is_array(),"visual instructions must be an array");std::vector<VisualInstruction> out;
    for(const auto& row:rows) {
        Fields(row,{"op","source","when","effects","variable","value","effect","seconds","fade","immediate","trigger","reset","key","path","type","wrap_mode","release_volume","optional","targets","state","transition","restart","context","name"});
        VisualInstruction instruction;instruction.source=Name(row.at("source"));
        if(row.contains("context")) {
            instruction.context=Name(row.at("context"));
            Need(program.contexts.count(instruction.context)!=0,"unknown visual context: "+instruction.context);
        }
        if(row.contains("when")){const auto& when=row.at("when");Fields(when,{"choice","is"});instruction.choice=Name(when.at("choice"));instruction.value=Name(when.at("is"));}
        const auto op=Name(row.at("op"));
        Need(op=="remove" || !row.contains("optional"),"optional effect keys require remove");
        if(op=="context_enter" || op=="context_exit") {
            const auto name=Name(row.at("name"));Need(program.contexts.count(name)!=0,"unknown visual context: "+name);
            instruction.operation=VisualContextOperation{name,op=="context_enter",row.value("fade",true)};
        } else if(op=="create") {
            auto names=Names(row.at("effects"));for(const auto& key:names)Need(program.effects.count(key)!=0,"unknown effect input: "+key);
            instruction.operation=VisualCreate{std::move(names)};
        } else if(op=="assign")instruction.operation=VisualAssign{Name(row.at("variable")),Number(row.at("value"))};
        else if(op=="progress") {
            VisualProgress progress;progress.effect=Name(row.at("effect"));progress.seconds=Number(row.value("seconds",Json(0)),true);
            if(row.at("value").is_object()) {Fields(row.at("value"),{"variable"});progress.value=Name(row.at("value").at("variable"));}
            else progress.value=Number(row.at("value"));
            instruction.operation=std::move(progress);
        } else if(op=="remove") {
            VisualRemove remove{Names(row.at("effects")),row.value("fade",true),row.value("immediate",false),{}};
            if(row.contains("optional"))for(const auto& key:Names(row.at("optional"))) {
                Need(std::find(remove.effects.begin(),remove.effects.end(),key)!=remove.effects.end(),"optional key not in removal: "+key);
                remove.optional.insert(key);
            }
            instruction.operation=std::move(remove);
        }
        else if(op=="trigger")instruction.operation=VisualTrigger{Names(row.at("effects")),Name(row.at("trigger")),row.value("reset",false)};
        else if(op=="follow") {
            const auto& targets=row.at("targets");Need(targets.is_array(),"follow targets must be an array");
            instruction.operation=VisualFollow{Name(row.at("effect")),targets.empty()?std::vector<std::string>{}:Names(targets)};
        }
        else if(op=="effect_state")instruction.operation=VisualEffectState{Name(row.at("effect")),Name(row.at("state")),
            Number(row.value("transition",Json(0)),true),row.value("restart",false)};
        else if(op=="timeline_play" || op=="timeline_stop") {
            VisualTimeline timeline;timeline.key=Name(row.at("key"));timeline.play=op=="timeline_play";
            if(timeline.play)timeline.path=Name(row.at("path"));
            timeline.type=row.value("type",0);timeline.wrap_mode=row.value("wrap_mode",0);timeline.release_volume=row.value("release_volume",false);
            instruction.operation=std::move(timeline);
        } else throw std::invalid_argument("unknown visual operation: "+op);
        if(!instruction.context.empty())Need(!std::holds_alternative<VisualContextOperation>(instruction.operation) &&
            !std::holds_alternative<VisualAssign>(instruction.operation),
            "context selection applies to effect and Timeline operations");
        out.push_back(std::move(instruction));
    }
    return out;
}
void ContextInstructions(const std::vector<VisualInstruction>& steps) {
    for(const auto& step:steps) {
        Need(step.context.empty(),"context instructions operate on their own lifetime");
        Need(!std::holds_alternative<VisualContextOperation>(step.operation),"nested context lifecycle is unsupported");
    }
}
}
VisualProgram ParseVisualProgram(const std::string& text) {
    const auto json=Json::parse(text);Fields(json,{"format","id","source","scope","effects","actions","signals","contexts","phases"});
    Need(json.at("format")=="hksr.visual-program","unsupported visual program format");
    VisualProgram out;out.id=Name(json.at("id"));out.source=Name(json.at("source"));out.scope=Name(json.at("scope"));
    Need(json.at("effects").is_object(),"visual effects must be an object");
    for(auto it=json.at("effects").begin();it!=json.at("effects").end();++it) {
        const auto& row=it.value();Fields(row,{"path","target","anchor","position_offset","rotation_offset","scale","attach_to_target","attach_point","fade_in","simulate_immediately","unique_name"});
        VisualEffectInput input;input.path=Name(row.at("path"));
        Need(row.contains("target")!=row.contains("anchor"),"effect needs exactly one target or world anchor");
        if(row.contains("anchor")) {
            input.anchor=Name(row.at("anchor"));
            Need(!row.contains("position_offset") && !row.contains("rotation_offset") && !row.contains("attach_to_target") && !row.contains("attach_point"),
                "world-anchored effect cannot use entity-relative inputs");
        }else input.target=Name(row.at("target"));
        if(row.contains("position_offset"))input.options.position_offset=Vector(row.at("position_offset"));
        if(row.contains("rotation_offset"))input.options.rotation_offset=Vector(row.at("rotation_offset"));
        if(row.contains("scale"))input.options.scale=Vector(row.at("scale"));
        if(row.contains("unique_name"))input.options.unique_name=Name(row.at("unique_name"));
        if(row.contains("attach_point"))input.options.attach_point=Name(row.at("attach_point"));
        input.options.attach_to_target=row.value("attach_to_target",false);input.options.fade_in=row.value("fade_in",true);
        input.options.simulate_immediately=row.value("simulate_immediately",false);
        out.effects.emplace(Name(it.key()),std::move(input));
    }
    if(json.contains("phases")) {
        Need(json.at("phases").is_object(),"visual phases must be an object");
        for(auto it=json.at("phases").begin();it!=json.at("phases").end();++it) {
            const auto& row=it.value();Fields(row,{"root","path"});api::PhaseVisualSource source;
            const auto& root=row.at("root");Fields(root,{"kind","name"});
            const auto kind=Name(root.at("kind"));source.name=Name(root.at("name"));
            if(kind=="ability")source.root=api::PhaseConfigRoot::Ability;
            else if(kind=="global_modifier")source.root=api::PhaseConfigRoot::GlobalModifier;
            else if(kind=="global_template")source.root=api::PhaseConfigRoot::GlobalTemplate;
            else throw std::invalid_argument("unknown phase root kind: "+kind);
            const auto& path=row.at("path");
            Need(path.is_array() && !path.empty(),"phase field path must be nonempty");
            for(const auto& edge:path) {
                Fields(edge,{"owner","field","index","key"});api::ConfigStep step;
                step.declaring_type=Name(edge.at("owner"));step.field=Name(edge.at("field"));
                Need(!edge.contains("index") || !edge.contains("key"),"phase edge cannot have both index and key");
                if(edge.contains("index")) {
                    Need(edge.at("index").is_number_integer(),"phase index must be an integer");
                    const auto index=edge.at("index").get<std::int64_t>();
                    Need(index>=0 && index<=INT32_MAX,"phase index is out of range");
                    step.selection=api::ConfigSelection::Index;step.index=static_cast<std::int32_t>(index);
                } else if(edge.contains("key")){step.selection=api::ConfigSelection::StringKey;step.key=Name(edge.at("key"));}
                source.path.push_back(std::move(step));
            }
            out.phases.emplace(Name(it.key()),std::move(source));
        }
    }
    if(json.contains("contexts")) {
        Need(json.at("contexts").is_object(),"visual contexts must be an object");
        std::set<std::string> event_scopes{out.scope};
        for(auto it=json.at("contexts").begin();it!=json.at("contexts").end();++it)out.contexts.emplace(Name(it.key()),VisualProgramContext{});
        for(auto it=json.at("contexts").begin();it!=json.at("contexts").end();++it) {
            const auto& row=it.value();Fields(row,{"enter","exit","event_scope","phase"});auto& context=out.contexts.at(it.key());
            if(row.contains("phase")) {
                context.phase=Name(row.at("phase"));Need(out.phases.count(context.phase)!=0,"unknown phase: "+context.phase);
            }
            if(row.contains("event_scope")) {
                context.event_scope=Name(row.at("event_scope"));
                Need(event_scopes.insert(context.event_scope).second,"event scope has multiple context owners: "+context.event_scope);
            }
            context.enter=Instructions(row.value("enter",Json::array()),out);
            context.exit=Instructions(row.value("exit",Json::array()),out);
            ContextInstructions(context.enter);ContextInstructions(context.exit);
        }
    }
    Need(json.at("actions").is_object() && !json.at("actions").empty(),"visual actions must be nonempty object");
    for(auto it=json.at("actions").begin();it!=json.at("actions").end();++it) {
        const auto& row=it.value();Fields(row,{"state","restart","enter","lanes","completion_state","complete_at","completion_at","after_completion"});
        VisualProgramAction action;action.state=Name(row.at("state"));action.restart=row.value("restart",false);
        action.complete_at=Number(row.value("complete_at",Json(1)),true);
        if(row.contains("completion_state"))action.completion_state=Name(row.at("completion_state"));
        Need(!action.completion_state.empty() || (!row.contains("completion_at") && !row.contains("after_completion")),
            "successor instructions require completion_state");
        action.completion_at=Number(row.value("completion_at",Json(0)),true);
        action.after_completion=Instructions(row.value("after_completion",Json::array()),out);
        action.enter=Instructions(row.value("enter",Json::array()),out);
        if(row.contains("lanes")) {
            Need(row.at("lanes").is_array(),"visual lanes must be an array");
            for(const auto& cues:row.at("lanes")) {
                Need(cues.is_array(),"visual lane must be an array");std::vector<VisualCue> lane;
                for(const auto& cue:cues) {
                    Fields(cue,{"at","instructions"});const float at=Number(cue.at("at"),true);
                    Need(lane.empty() || at>lane.back().at,"visual lane times must increase");
                    Need(at<=action.complete_at,"cue follows action completion");
                    lane.push_back({at,Instructions(cue.at("instructions"),out)});
                }
                action.lanes.push_back(std::move(lane));
            }
        }
        out.actions.emplace(Name(it.key()),std::move(action));
    }
    if(json.contains("signals")) {
        Need(json.at("signals").is_object(),"visual signals must be an object");
        for(auto it=json.at("signals").begin();it!=json.at("signals").end();++it) {
            VisualProgramSignalDefinition signal;
            if(it.value().is_array())signal.instructions=Instructions(it.value(),out);
            else {
                Fields(it.value(),{"context","instructions"});signal.context=Name(it.value().at("context"));
                Need(out.contexts.count(signal.context)!=0,"unknown signal context: "+signal.context);
                signal.instructions=Instructions(it.value().at("instructions"),out);ContextInstructions(signal.instructions);
            }
            out.signals.emplace(Name(it.key()),std::move(signal));
        }
    }
    return out;
}
VisualSpatialAnchors ParseVisualSpatialAnchors(const std::string& text) {
    const auto json=Json::parse(text);Need(json.is_object(),"visual anchors must be an object");
    VisualSpatialAnchors out;
    for(auto it=json.begin();it!=json.end();++it) {
        const auto& row=it.value();Fields(row,{"position","rotation"});
        VisualSpatialAnchor anchor;anchor.position=Vector(row.at("position"));
        if(row.contains("rotation")) {
            const auto& r=row.at("rotation");Need(r.is_array() && r.size()==4,"anchor rotation must be a quaternion");
            anchor.rotation={Number(r[0]),Number(r[1]),Number(r[2]),Number(r[3])};
            const auto& q=anchor.rotation;
            Need(double(q.x)*q.x+double(q.y)*q.y+double(q.z)*q.z+double(q.w)*q.w>0,"anchor rotation must be nonzero");
        }
        out.emplace(Name(it.key()),anchor);
    }
    return out;
}
}
