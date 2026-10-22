#include <hksr/framework/tasks/TaskCatalog.hpp>
#include "VisualCodec.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace hksr::framework {
namespace {
using Json=nlohmann::json;
void Need(bool ok,const std::string& detail){if(!ok)throw std::invalid_argument(detail);}
void Fields(const Json& row,std::initializer_list<const char*> names) {
    Need(row.is_object(),"task object expected");
    for(auto it=row.begin();it!=row.end();++it) {
        bool known=false;for(const auto* name:names)known=known || it.key()==name;
        Need(known,"unknown task field: "+it.key());
    }
}
std::string Name(const Json& value) {
    const auto name=value.get<std::string>();
    Need(!name.empty() && name.find('\0')==std::string::npos,"invalid task name");return name;
}
std::int64_t Integer(const Json& value) {
    Need(value.is_number_integer(),"task integer expected");
    if(value.is_number_unsigned())Need(value.get<std::uint64_t>()<=INT64_MAX,"task integer out of range");
    return value.get<std::int64_t>();
}
double Number(const Json& value) {
    Need(value.is_number(),"task number expected");const auto number=value.get<double>();
    Need(std::isfinite(number),"nonfinite task number");return number;
}
TaskValue Value(const Json& value) {
    if(value.is_boolean())return value.get<bool>();
    if(value.is_number_integer())return Integer(value);
    if(value.is_number_float())return Number(value);
    if(value.is_string())return value.get<std::string>();
    throw std::invalid_argument("task value must be bool, integer, number or text");
}
TaskValues Values(const Json& json) {
    Need(json.is_object(),"task values must be an object");TaskValues result;
    for(auto it=json.begin();it!=json.end();++it)result.emplace(Name(it.key()),Value(it.value()));
    return result;
}
TaskRequirement Requirement(const Json& row) {
    const auto op=Name(row.at("op"));
    if(op=="value") {Fields(row,{"op","name","is"});return TaskValueEquals{Name(row.at("name")),Value(row.at("is"))};}
    if(op=="phase") {Fields(row,{"op","is"});return TaskPhaseEquals{Name(row.at("is"))};}
    if(op=="participant") {Fields(row,{"op","name"});return TaskNeedsParticipant{{Name(row.at("name"))}};}
    if(op=="target") {Fields(row,{"op","name"});return TaskNeedsTarget{Name(row.at("name"))};}
    if(op=="anchor") {Fields(row,{"op","name"});return TaskNeedsAnchor{Name(row.at("name"))};}
    if(op=="choice") {Fields(row,{"op","name","is"});return TaskChoiceEquals{Name(row.at("name")),Name(row.at("is"))};}
    if(op=="unresolved") {Fields(row,{"op","source","detail"});return TaskUnresolvedRequirement{Name(row.at("source")),Name(row.at("detail"))};}
    throw std::invalid_argument("unknown task predicate: "+op);
}
TaskKey Key(const Json& json,const std::string& catalog){return {catalog,Name(json)};}
TaskInputDefinition Input(const std::string& name,const Json& row) {
    Fields(row,{"type","default"});TaskInputDefinition input;input.name=Name(name);
    const auto type=Name(row.at("type"));
    if(type=="bool")input.type=TaskValueType::Boolean;
    else if(type=="integer")input.type=TaskValueType::Integer;
    else if(type=="number")input.type=TaskValueType::Number;
    else if(type=="text")input.type=TaskValueType::Text;
    else throw std::invalid_argument("unknown task input type: "+type);
    if(row.contains("default"))input.default_value=Value(row.at("default"));
    return input;
}
struct SequenceParser {
    const VisualProgram& visual;
    TaskSequence sequence;
    TaskNodeIndex Node(const Json& row,const std::string& path) {
        try {
            const auto op=Name(row.at("op"));TaskNodeOperation operation;
            if(op=="serial" || op=="parallel") {
                Fields(row,{"op","children","source"});const auto& children=row.at("children");
                Need(children.is_array(),"task children must be an array");
                std::vector<TaskNodeIndex> compiled;
                for(std::size_t i=0;i<children.size();++i)compiled.push_back(Node(children[i],path+"/children/"+std::to_string(i)));
                if(op=="serial")operation=TaskSerial{std::move(compiled)};else operation=TaskParallel{std::move(compiled)};
            } else if(op=="animation") {
                Fields(row,{"op","request","participant","state","layer","transition","source"});
                const auto layer=Integer(row.value("layer",Json(0)));Need(layer>=0 && layer<=INT_MAX,"animation layer out of range");
                const auto transition=Number(row.value("transition",Json(0)));
                Need(transition>=0 && transition<=std::numeric_limits<float>::max(),"animation transition out of range");
                operation=TaskSubmit{Name(row.at("request")),TaskAnimationCommand{{Name(row.at("participant"))},Name(row.at("state")),
                    static_cast<int>(layer),static_cast<float>(transition)}};
            } else if(op=="visual") {
                Fields(row,{"op","request","instruction","source"});
                auto steps=visual_codec::Instructions(Json::array({row.at("instruction")}),visual);
                operation=TaskSubmit{Name(row.at("request")),std::move(steps.front())};
            } else if(op=="wait_request") {
                Fields(row,{"op","request","signal","progress","source"});
                const auto signal=row.value("signal",std::string("performance"));
                TaskWaitRequest wait;wait.request=Name(row.at("request"));
                if(signal=="submitted")wait.signal=TaskWaitSignal::Submitted;
                else if(signal=="accepted")wait.signal=TaskWaitSignal::RequestComplete;
                else if(signal=="performance")wait.signal=TaskWaitSignal::PerformanceComplete;
                else if(signal=="drained")wait.signal=TaskWaitSignal::Drained;
                else if(signal=="progress")wait.signal=TaskWaitSignal::Progress;
                else throw std::invalid_argument("unknown task wait signal: "+signal);
                Need(signal=="progress" || !row.contains("progress"),"progress requires progress wait signal");
                if(signal=="progress")wait.progress=Number(row.at("progress"));
                operation=wait;
            } else if(op=="wait_seconds") {
                Fields(row,{"op","seconds","source"});operation=TaskWaitSeconds{Number(row.at("seconds"))};
            } else if(op=="wait_frame") {
                Fields(row,{"op","source"});operation=TaskWaitFrame{};
            } else if(op=="repeat") {
                Fields(row,{"op","child","count","until_finish","source"});TaskRepeat repeat;
                repeat.child=Node(row.at("child"),path+"/child");
                if(row.contains("count")) {
                    const auto count=Integer(row.at("count"));Need(count>=0,"negative repeat count");
                    repeat.count=static_cast<std::uint64_t>(count);
                }
                repeat.until_finish=row.value("until_finish",false);operation=repeat;
            } else if(op=="branch") {
                Fields(row,{"op","condition","yes","no","source"});
                operation=TaskBranch{Requirement(row.at("condition")),Node(row.at("yes"),path+"/yes"),Node(row.at("no"),path+"/no")};
            } else throw std::invalid_argument("unknown task operation: "+op);
            const auto index=sequence.nodes.size();
            sequence.nodes.push_back({row.contains("source")?Name(row.at("source")):path,std::move(operation)});
            return index;
        }catch(const std::exception& error){throw std::invalid_argument(path+": "+error.what());}
    }
};
}
TaskCatalog ParseTaskCatalog(const std::string& text) {
    const auto json=Json::parse(text);
    Fields(json,{"format","id","domain","visual","sequences","tasks","contexts"});
    Need(json.at("format")=="hksr.task-catalog","unsupported task catalog format");
    TaskCatalog out;out.id=Name(json.at("id"));
    const auto domain=Name(json.at("domain"));
    if(domain=="exploration")out.domain=ActionDomain::Exploration;
    else Need(domain=="battle","unknown task catalog domain: "+domain);
    const auto& visual=json.at("visual");
    Fields(visual,{"id","source","scope","effects","partners","signals","contexts","phases","restore"});
    out.visual=std::make_shared<const VisualProgram>(visual_codec::Resources(visual));
    const auto& sequences=json.at("sequences");Need(sequences.is_object(),"task sequences must be an object");
    for(auto it=sequences.begin();it!=sequences.end();++it) {
        const auto& row=it.value();Fields(row,{"entry","interruption"});SequenceParser parser{*out.visual,{}};
        parser.sequence.entry=parser.Node(row.at("entry"),"sequences/"+it.key()+"/entry");
        if(row.contains("interruption"))parser.sequence.interruption=parser.Node(row.at("interruption"),"sequences/"+it.key()+"/interruption");
        out.sequences.emplace(Name(it.key()),std::make_shared<const TaskSequence>(std::move(parser.sequence)));
    }
    const auto& tasks=json.at("tasks");Need(tasks.is_object(),"tasks must be an object");
    for(auto it=tasks.begin();it!=tasks.end();++it) {
        const auto& row=it.value();Fields(row,{"sources","inputs","requires","tags","entry","next","bindings"});
        TaskPlan plan;auto& definition=plan.definition;definition.key=Key(it.key(),out.id);
        if(row.contains("entry"))definition.entry=Name(row.at("entry"));
        if(row.contains("sources")) {
            Need(row.at("sources").is_array(),"task sources must be an array");
            for(const auto& source:row.at("sources")) {
                Fields(source,{"document","field"});definition.sources.push_back({Name(source.at("document")),Name(source.at("field"))});
            }
        }
        if(row.contains("inputs")) {
            Need(row.at("inputs").is_object(),"task inputs must be an object");
            for(auto input=row.at("inputs").begin();input!=row.at("inputs").end();++input)definition.inputs.push_back(Input(input.key(),input.value()));
        }
        if(row.contains("requires")) {
            Need(row.at("requires").is_array(),"task prerequisites must be an array");
            for(const auto& condition:row.at("requires"))definition.requirements.push_back(Requirement(condition));
        }
        if(row.contains("bindings")) {
            const auto& bindings=row.at("bindings");Fields(bindings,{"targets","anchors","choices"});
            for(const auto* key:{"targets","anchors"})if(bindings.contains(key)) {
                Need(bindings.at(key).is_array(),"task binding names must be an array");
                auto& names=std::string(key)=="targets"?definition.bindings.targets:definition.bindings.anchors;
                for(const auto& name:bindings.at(key))names.push_back(Name(name));
            }
            if(bindings.contains("choices")) {
                const auto& choices=bindings.at("choices");Need(choices.is_object(),"task choices must be an object");
                for(auto choice=choices.begin();choice!=choices.end();++choice) {
                    Need(choice.value().is_array(),"task choice options must be an array");
                    auto& options=definition.bindings.choices[Name(choice.key())];
                    for(const auto& value:choice.value())options.push_back(Name(value));
                }
            }
        }
        if(row.contains("tags")) {
            Need(row.at("tags").is_array(),"task tags must be an array");
            for(const auto& tag:row.at("tags"))definition.tags.push_back(Name(tag));
        }
        if(row.contains("next")) {
            const auto& next=row.at("next");Fields(next,{"task","inputs"});
            plan.next=TaskContinuation{Key(next.at("task"),out.id),Values(next.value("inputs",Json::object()))};
        }
        out.tasks.emplace(it.key(),std::move(plan));
    }
    if(json.contains("contexts")) {
        Need(json.at("contexts").is_object(),"task contexts must be an object");
        for(auto it=json.at("contexts").begin();it!=json.at("contexts").end();++it) {
            const auto& row=it.value();Fields(row,{"values","phase","slots","disables"});TaskContextDefinition definition;
            definition.values=Values(row.value("values",Json::object()));
            if(row.contains("phase"))definition.phase=Name(row.at("phase"));
            if(row.contains("slots")) {
                Need(row.at("slots").is_array(),"context slots must be an array");
                for(const auto& slot:row.at("slots")) {
                    Fields(slot,{"slot","task"});const auto key=Integer(slot.at("slot"));
                    Need(key>=INT32_MIN && key<=INT32_MAX,"task slot out of range");
                    Need(definition.slots.emplace(static_cast<std::int32_t>(key),Key(slot.at("task"),out.id)).second,"duplicate context slot");
                }
            }
            if(row.contains("disables")) {
                Need(row.at("disables").is_array(),"context disables must be an array");
                for(const auto& disable:row.at("disables")) {
                    Fields(disable,{"task","slot","tag","reason"});
                    Need(static_cast<int>(disable.contains("task"))+disable.contains("slot")+disable.contains("tag")==1,"disable needs one selector");
                    TaskDisableContribution value;value.reason=Name(disable.at("reason"));
                    if(disable.contains("task"))value.selector=Key(disable.at("task"),out.id);
                    else if(disable.contains("tag"))value.selector=TaskTag{Name(disable.at("tag"))};
                    else {
                        const auto slot=Integer(disable.at("slot"));Need(slot>=INT32_MIN && slot<=INT32_MAX,"task slot out of range");
                        value.selector=TaskSlot{static_cast<std::int32_t>(slot)};
                    }
                    definition.disables.push_back(std::move(value));
                }
            }
            out.contexts.emplace(Name(it.key()),std::move(definition));
        }
    }
    ValidateTaskCatalog(out);return out;
}
}
