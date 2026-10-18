#include <hksr/framework/tasks/TaskContext.hpp>
#include <hksr/framework/tasks/TaskSources.hpp>
#include <limits>
#include <stdexcept>

namespace {
using namespace hksr::framework;
void Check(bool value) {if(!value)throw std::runtime_error("task model test failed");}
template<class F> void Throws(F&& f) {bool threw=false;try{f();}catch(const std::invalid_argument&){threw=true;}Check(threw);}
void Contexts() {
    TaskContext context;TaskDefinition task;task.key={"actor-config","task-a"};task.entry="entry-a";
    task.inputs={{"variant",TaskValueType::Text,TaskValue{std::string("base")}}};task.tags={"attack"};
    TaskQuery query;query.slot=TaskSlot{2};
    Check(context.Query(task,query).availability==TaskAvailability::Available);
    const auto a=context.Enter("phase-a"),b=context.Enter("phase-b");
    context.SetValue(a,"variant",std::string("a"));context.SetPhase(a,"a");
    context.SetValue(b,"variant",std::string("b"));context.SetPhase(b,"b");
    context.SetSlot(a,{2},{"actor-config","a"});context.SetSlot(b,{2},{"actor-config","b"});
    context.Disable(a,{TaskTag{"attack"},"locked by a"});context.Disable(b,{TaskSlot{2},"locked by b"});
    auto result=context.Query(task,query);
    Check(result.availability==TaskAvailability::Unavailable && result.reasons.size()==2 && std::get<std::string>(result.inputs.at("variant"))=="b");
    Check(context.Values().at("variant").owner.value==b.value && context.Phase()=="b" && context.ResolveSlot({2},task.key).name=="b");
    Check(context.Exit(b) && !context.Exit(b));
    Check(context.Phase()=="a" && context.ResolveSlot({2},task.key).name=="a");
    result=context.Query(task,query);Check(result.availability==TaskAvailability::Unavailable && result.reasons.size()==1);
    query.inputs["variant"]=std::string("local");
    Check(std::get<std::string>(context.Query(task,query).inputs.at("variant"))=="local" && std::get<std::string>(context.Values().at("variant").value)=="a");
    Check(context.Exit(a));Check(context.Query(task,query).availability==TaskAvailability::Available);
    Check(!context.Phase() && context.ResolveSlot({2},task.key)==task.key);
    Throws([&]{context.SetValue(b,"variant",true);});
    const auto fresh=context.Enter("phase-b");Check(fresh.value!=b.value);
    // A second actor has its own phase/disable state.
    TaskContext other;Check(other.Query(task,query).availability==TaskAvailability::Available);
}
void Queries() {
    TaskContext context;TaskDefinition task;task.key={"source","non-ready-task"};task.entry="configured-sequence";
    task.inputs={{"count",TaskValueType::Integer,{}}};
    task.requirements={TaskValueEquals{"mode",std::string("boost")},TaskNeedsParticipant{{"partner"}},TaskPhaseEquals{"enhanced"}};
    TaskQuery query;auto result=context.Query(task,query);
    Check(result.availability==TaskAvailability::Unavailable && result.reasons.size()==4); // all reasons, not first gate
    query.inputs["count"]=std::int64_t{2};query.participants={TaskParticipant{"partner"}};
    Check(context.Query(task,query).availability==TaskAvailability::Unknown);
    const auto phase=context.Enter("boost");context.SetValue(phase,"mode",std::string("boost"));context.SetPhase(phase,"enhanced");
    Check(context.Query(task,query).availability==TaskAvailability::Available);
    context.SetValue(phase,"mode",std::string("base"));Check(context.Query(task,query).availability==TaskAvailability::Unavailable);
    context.SetValue(phase,"mode",true);Check(context.Query(task,query).availability==TaskAvailability::Unknown);
    query.inputs["count"]=2.0;Check(context.Query(task,query).availability==TaskAvailability::Unavailable); // no numeric coercion
    query.inputs["count"]=std::int64_t{2};query.inputs["typo"]=true;
    Check(context.Query(task,query).availability==TaskAvailability::Unavailable);
    query.inputs.erase("typo");task.requirements.clear();task.entry.clear();
    Check(context.Query(task,query).availability==TaskAvailability::Unknown);
    task.entry="entry";task.requirements={TaskUnresolvedRequirement{"config.condition","not qualified"}};
    Check(context.Query(task,query).availability==TaskAvailability::Unknown);
    task.inputs[0].default_value=std::string("wrong");Throws([&]{ValidateTaskDefinition(task);});
    task.inputs.clear();task.requirements.clear();task.inputs={{"number",TaskValueType::Number,{}}};
    query.inputs.clear();query.inputs["number"]=std::numeric_limits<double>::quiet_NaN();
    Check(context.Query(task,query).availability==TaskAvailability::Unavailable);
}
void Sources() {
    hksr::api::TaskConfigInput input;input.actions.path="character-a";input.actions.loaded=true;
    input.actions.skills.enumerated=input.skill_abilities.enumerated=true;
    hksr::api::SkillTaskInput first;first.skill.source_index=0;first.skill.name={true,"SharedName",{}};
    first.skill.entry_ability={true,"Passive_Entry",{}};first.skill.ready_state={true,"",{}};
    auto second=first;second.skill.source_index=4;second.skill.entry_ability.value="Inserted_Entry";
    input.skills={first,second};
    hksr::api::SkillAbilityInput group;group.source_index=3;group.skill={true,"SharedName",{}};group.abilities={true,{"Installed"},{}};
    auto missing=group;missing.source_index=7;missing.skill.value="Unmatched";
    auto unread=group;unread.source_index=9;unread.skill.available=false;
    input.skill_abilities.rows={group,missing,unread};
    const auto inventory=BuildTaskSources(input);
    Check(inventory.loaded && inventory.skills.size()==2 && inventory.groups.size()==3 && inventory.issues.size()==3);
    Check(inventory.skills[0].entries.size()==1 && inventory.skills[0].entries[0].ability=="Passive_Entry");
    Check(inventory.skills[1].entries[0].source.field=="SkillList[4].EntryAbility");
    Check(inventory.groups[0].skill_indices==std::vector<std::int32_t>({0,4}));
    Check(inventory.groups[1].skill_indices.empty() && inventory.groups[2].input.abilities.values[0]=="Installed");
}
}
void TestTaskModel() {Contexts();Queries();Sources();}
