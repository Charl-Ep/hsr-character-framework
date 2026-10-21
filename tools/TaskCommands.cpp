#include "TaskCommands.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace hksr::research {
namespace {
using Json=nlohmann::json;
using namespace framework;
std::uint64_t Id(const Json& json) {
    if(!json.is_number_integer() || (!json.is_number_unsigned() && json.get<std::int64_t>()<=0) || json==0)
        throw std::invalid_argument("task command requires positive instance and creation IDs");
    return json.get<std::uint64_t>();
}
std::string Name(const Json& json) {
    const auto name=json.get<std::string>();
    if(name.empty() || name.find('\0')!=std::string::npos)throw std::invalid_argument("invalid task command name");
    return name;
}
Json Key(const TaskKey& key){return {{"catalog",key.catalog},{"name",key.name}};}
Json Values(const TaskValues& values) {
    auto out=Json::object();for(const auto& [key,value]:values)std::visit([&](const auto& v){out[key]=v;},value);return out;
}
Json Query(const TaskQueryResult& query) {
    const char* availability=query.availability==TaskAvailability::Available?"available":
        query.availability==TaskAvailability::Unavailable?"unavailable":"unknown";
    Json reasons=Json::array();for(const auto& reason:query.reasons)
        reasons.push_back({{"kind",static_cast<int>(reason.kind)},{"source",reason.source},{"detail",reason.detail}});
    return {{"availability",availability},{"inputs",Values(query.inputs)},{"reasons",reasons}};
}
const char* Phase(TaskExecutionPhase phase) {
    switch(phase) {
    case TaskExecutionPhase::Running:return "running";
    case TaskExecutionPhase::Interrupting:return "interrupting";
    case TaskExecutionPhase::Draining:return "draining";
    default:return "finished";
    }
}
}
framework::ControlCharacterTask ParseTaskCommand(const Json& row) {
    using namespace framework;ControlCharacterTask out;
    for(auto it=row.begin();it!=row.end();++it)
        if(it.key()!="op" && it.key()!="character" && it.key()!="creation" && it.key()!="catalog" &&
            it.key()!="task" && it.key()!="inputs" && it.key()!="slot" && it.key()!="choices" &&
            it.key()!="targets" && it.key()!="anchors")
            throw std::invalid_argument("unknown task command field: "+it.key());
    out.id=Id(row.at("character"));out.creation=Id(row.at("creation"));
    const auto op=Name(row.at("op"));
    if(op=="task_select")out.input.operation=TaskControlOperation::Select;
    else if(op=="task_query")out.input.operation=TaskControlOperation::Query;
    else if(op=="task_finish")out.input.operation=TaskControlOperation::Finish;
    else if(op=="task_cancel")out.input.operation=TaskControlOperation::Cancel;
    else throw std::invalid_argument("unknown task control operation");
    if(row.contains("task")) {
        if(out.input.operation!=TaskControlOperation::Select && out.input.operation!=TaskControlOperation::Query)
            throw std::invalid_argument("task identity only applies to select/query");
        TaskSelection selection;selection.task={Name(row.at("catalog")),Name(row.at("task"))};
        const auto inputs=row.value("inputs",Json::object());
        if(!inputs.is_object())throw std::invalid_argument("task inputs must be an object");
        for(auto it=inputs.begin();it!=inputs.end();++it) {
            const auto& value=it.value();const auto name=Name(it.key());
            if(value.is_boolean())selection.query.inputs[name]=value.get<bool>();
            else if(value.is_number_integer()) {
                if(value.is_number_unsigned() && value.get<std::uint64_t>()>INT64_MAX)throw std::invalid_argument("task integer out of range");
                selection.query.inputs[name]=value.get<std::int64_t>();
            } else if(value.is_number_float() && std::isfinite(value.get<double>()))selection.query.inputs[name]=value.get<double>();
            else if(value.is_string())selection.query.inputs[name]=value.get<std::string>();
            else throw std::invalid_argument("invalid task input: "+name);
        }
        if(row.contains("slot")) {
            const auto& slot=row.at("slot");
            if(!slot.is_number_integer() || (slot.is_number_unsigned() && slot.get<std::uint64_t>()>INT32_MAX) ||
                slot.get<std::int64_t>()<INT32_MIN || slot.get<std::int64_t>()>INT32_MAX)throw std::invalid_argument("task slot out of range");
            selection.query.slot=TaskSlot{slot.get<std::int32_t>()};
        }
        const auto targets=row.value("targets",Json::object());
        if(!targets.is_object())throw std::invalid_argument("task targets must be an object");
        for(auto it=targets.begin();it!=targets.end();++it) {
            const auto name=Name(it.key());
            if(name=="Caster")throw std::invalid_argument("caster cannot be overridden");
            selection.visual.targets.emplace(name,Id(it.value()));
        }
        selection.visual.choices=row.value("choices",Json::object()).get<std::map<std::string,std::string>>();
        selection.visual.anchors=ParseVisualSpatialAnchors(row.value("anchors",Json::object()).dump());
        out.input.selection=std::move(selection);
    } else {
        if(out.input.operation==TaskControlOperation::Select)throw std::invalid_argument("task selection missing");
        for(const auto* field:{"catalog","inputs","slot","choices","targets","anchors"})
            if(row.contains(field))throw std::invalid_argument("task inputs require a task identity");
    }
    return out;
}
Json TaskResult(const framework::TaskControlResult& result) {
    const auto& state=result.status;
    Json out={{"requested",state.requested},{"started",state.started},{"retiring",state.retiring},
        {"closing",state.closing},{"error",state.error},{"contexts",Json::array()},{"catalog",Json::object()},
        {"requests",Json::array()}};
    for(const auto& request:result.requests) {
        const auto& r=request.receipt;const auto& s=request.state;
        Json row={{"name",request.name},{"source",request.source},{"execution",r.execution.value},
            {"request",r.request},{"entity_generation",r.entity_generation},{"model_generation",r.model},
            {"participant",r.participant.name},{"submitted",s.submitted},{"accepted",s.request_complete},
            {"performance_complete",s.performance_complete},{"drained",s.drained},{"error",s.error}};
        row["superseded"]=s.superseded;
        if(s.progress)row["progress"]=*s.progress;
        out["requests"].push_back(std::move(row));
    }
    for(const auto& [name,query]:result.catalog)out["catalog"][name]=Query(query);
    if(state.current)out["current"]=Key(*state.current);
    if(state.pending)out["pending"]=Key(*state.pending);
    if(state.blocked)out["blocked"]=Query(*state.blocked);
    if(result.selection)out["selection"]={{"intent",result.selection->intent},{"resolved",Key(result.selection->resolved)},{"query",Query(result.selection->query)}};
    if(state.execution) {
        const auto& run=*state.execution;
        out["execution"]={{"token",run.token.value},{"phase",Phase(run.phase)},{"finish_requested",run.finish_requested},
            {"retained_requests",run.retained_requests},{"issues",run.issues},{"waiting",run.waiting}};
        if(run.reason)out["execution"]["reason"]=static_cast<int>(*run.reason);
    }
    for(const auto& context:result.contexts) {
        Json row={{"token",context.token.value},{"name",context.name},{"values",Values(context.values)},{"slots",Json::array()},{"disables",Json::array()}};
        if(context.phase)row["phase"]=*context.phase;
        for(const auto& [slot,key]:context.slots)row["slots"].push_back({{"slot",slot},{"task",Key(key)}});
        for(const auto& disable:context.disables) {
            Json item={{"reason",disable.reason}};
            std::visit([&](const auto& selector) {
                using T=std::decay_t<decltype(selector)>;
                if constexpr(std::is_same_v<T,TaskKey>)item["task"]=Key(selector);
                else if constexpr(std::is_same_v<T,TaskSlot>)item["slot"]=selector.value;
                else item["tag"]=selector.name;
            },disable.selector);row["disables"].push_back(std::move(item));
        }
        out["contexts"].push_back(std::move(row));
    }
    return out;
}
}
