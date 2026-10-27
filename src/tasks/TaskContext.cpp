#include <hksr/framework/tasks/TaskContext.hpp>
#include <hksr/framework/tasks/BattleControl.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace hksr::framework {
TaskValueType TypeOf(const TaskValue& value) {
    return std::visit([](const auto& v) {
        using T=std::decay_t<decltype(v)>;
        if constexpr(std::is_same_v<T,bool>)return TaskValueType::Boolean;
        else if constexpr(std::is_same_v<T,std::int64_t>)return TaskValueType::Integer;
        else if constexpr(std::is_same_v<T,double>)return TaskValueType::Number;
        else return TaskValueType::Text;
    },value);
}
namespace {
bool ValidValue(const TaskValue& value) {
    const auto* number=std::get_if<double>(&value);return !number || std::isfinite(*number);
}
bool ValidKey(const TaskKey& key) {return !key.catalog.empty() && !key.name.empty();}
}
TaskBindingSchema TaskBindings(const TaskDefinition& task) {
    auto out=task.bindings;
    auto add=[](auto& values,const std::string& name) {
        if(std::find(values.begin(),values.end(),name)==values.end())values.push_back(name);
    };
    for(const auto& requirement:task.requirements)std::visit([&](const auto& item) {
        using T=std::decay_t<decltype(item)>;
        if constexpr(std::is_same_v<T,TaskNeedsTarget>) {if(item.name!="Caster")add(out.targets,item.name);}
        else if constexpr(std::is_same_v<T,TaskNeedsAnchor>)add(out.anchors,item.name);
        else if constexpr(std::is_same_v<T,TaskChoiceEquals>) {
            if(!out.choices.count(item.name))out.choices[item.name]={item.value};
        }
    },requirement);
    return out;
}
void ValidateTaskDefinition(const TaskDefinition& task) {
    if(task.control)ValidateControlRelation(*task.control);
    if(!ValidKey(task.key))throw std::invalid_argument("task identity is incomplete");
    std::set<std::string> names;
    for(const auto& input:task.inputs) {
        if(input.name.empty() || !names.insert(input.name).second)throw std::invalid_argument("duplicate or empty task input");
        if(input.default_value && (TypeOf(*input.default_value)!=input.type || !ValidValue(*input.default_value)))
            throw std::invalid_argument("task input default has wrong type or nonfinite value: "+input.name);
    }
    for(const auto& condition:task.requirements)if(const auto* value=std::get_if<TaskValueEquals>(&condition))
        if(value->name.empty() || !ValidValue(value->value))throw std::invalid_argument("invalid task value predicate");
    auto names_valid=[](const auto& values) {
        std::set<std::string> unique;
        for(const auto& name:values)if(name.empty() || name.find('\0')!=std::string::npos || !unique.insert(name).second)
            throw std::invalid_argument("invalid or duplicate task binding");
    };
    names_valid(task.bindings.targets);names_valid(task.bindings.anchors);
    if(std::find(task.bindings.targets.begin(),task.bindings.targets.end(),"Caster")!=task.bindings.targets.end())
        throw std::invalid_argument("caster cannot be an editable target");
    for(const auto& [name,options]:task.bindings.choices) {
        names_valid(std::vector<std::string>{name});names_valid(options);
        if(options.empty())throw std::invalid_argument("task choice has no options");
    }
}
TaskContextToken TaskContext::Enter(std::string name) {
    if(name.empty())throw std::invalid_argument("context name is empty");
    TaskContextView view;view.token={++next_};view.name=std::move(name);owners_.push_back(std::move(view));return owners_.back().token;
}
TaskContextView& TaskContext::Owner(TaskContextToken token) {
    const auto found=std::find_if(owners_.begin(),owners_.end(),[&](const auto& owner){return owner.token.value==token.value;});
    if(found==owners_.end())throw std::invalid_argument("context token is not active");
    return *found;
}
bool TaskContext::Exit(TaskContextToken token) {
    const auto found=std::find_if(owners_.begin(),owners_.end(),[&](const auto& owner){return owner.token.value==token.value;});
    if(found==owners_.end())return false;
    owners_.erase(found);return true;
}
void TaskContext::SetValue(TaskContextToken token,const std::string& name,TaskValue value) {
    if(name.empty() || !ValidValue(value))throw std::invalid_argument("invalid context value");
    Owner(token).values.insert_or_assign(name,std::move(value));
}
void TaskContext::SetPhase(TaskContextToken token,std::optional<std::string> phase) {Owner(token).phase=std::move(phase);}
void TaskContext::SetSlot(TaskContextToken token,TaskSlot slot,TaskKey key) {
    if(!ValidKey(key))throw std::invalid_argument("slot task identity is incomplete");
    Owner(token).slots.insert_or_assign(slot.value,std::move(key));
}
void TaskContext::Disable(TaskContextToken token,TaskDisableContribution value) {Owner(token).disables.push_back(std::move(value));}
std::map<std::string,TaskResolvedValue> TaskContext::Values() const {
    std::map<std::string,TaskResolvedValue> result;
    for(const auto& owner:owners_)for(const auto& [name,value]:owner.values)result.insert_or_assign(name,TaskResolvedValue{value,owner.token,owner.name});
    return result;
}
std::optional<std::string> TaskContext::Phase() const {
    for(auto it=owners_.rbegin();it!=owners_.rend();++it)if(it->phase)return it->phase;
    return {};
}
TaskKey TaskContext::ResolveSlot(TaskSlot slot,const TaskKey& baseline) const {
    for(auto it=owners_.rbegin();it!=owners_.rend();++it)if(const auto found=it->slots.find(slot.value);found!=it->slots.end())return found->second;
    return baseline;
}
TaskQueryResult TaskContext::Query(const TaskDefinition& task,const TaskQuery& query) const {
    ValidateTaskDefinition(task);TaskQueryResult result;auto values=Values();
    auto reason=[&](TaskAvailability state,TaskReasonKind kind,std::string source,std::string detail) {
        if(state==TaskAvailability::Unavailable || result.availability==TaskAvailability::Available)result.availability=state;
        result.reasons.push_back({kind,std::move(source),std::move(detail)});
    };
    if(task.entry.empty() && (!task.control || task.control->presentation))
        reason(TaskAvailability::Unknown,TaskReasonKind::Unimplemented,task.key.name,"no visual task entry");
    for(const auto& input:task.inputs) {
        const auto explicit_value=query.inputs.find(input.name);
        const auto inherited=values.find(input.name);
        const TaskValue* selected=explicit_value!=query.inputs.end()?&explicit_value->second:
            inherited!=values.end()?&inherited->second.value:input.default_value?&*input.default_value:nullptr;
        if(!selected)reason(TaskAvailability::Unavailable,TaskReasonKind::MissingInput,input.name,"required input is absent");
        else if(TypeOf(*selected)!=input.type || !ValidValue(*selected))reason(TaskAvailability::Unavailable,TaskReasonKind::InputType,input.name,"input type differs or value is nonfinite");
        else result.inputs.emplace(input.name,*selected);
    }
    for(const auto& [name,value]:query.inputs) {
        (void)value;
        if(std::none_of(task.inputs.begin(),task.inputs.end(),[&](const auto& input){return input.name==name;}))
            reason(TaskAvailability::Unavailable,TaskReasonKind::InputType,name,"undeclared task input");
    }
    TaskQuery conditions=query;conditions.inputs=result.inputs;
    for(const auto& [name,options]:task.bindings.choices)if(const auto choice=query.choices.find(name);choice!=query.choices.end())
        if(std::find(options.begin(),options.end(),choice->second)==options.end())
            reason(TaskAvailability::Unavailable,TaskReasonKind::InputType,name,"visual choice is not an allowed option");
    for(const auto& requirement:task.requirements) {
        const auto evaluated=Evaluate(requirement,conditions);
        for(const auto& item:evaluated.reasons)reason(evaluated.availability,item.kind,item.source,item.detail);
    }
    for(const auto& owner:owners_)for(const auto& disable:owner.disables) {
        const auto matches=std::visit([&](const auto& selector) {
            using T=std::decay_t<decltype(selector)>;
            if constexpr(std::is_same_v<T,TaskKey>)return selector==task.key;
            else if constexpr(std::is_same_v<T,TaskSlot>)return query.slot && query.slot->value==selector.value;
            else return std::find(task.tags.begin(),task.tags.end(),selector.name)!=task.tags.end();
        },disable.selector);
        if(matches)reason(TaskAvailability::Unavailable,TaskReasonKind::Disabled,owner.name,disable.reason);
    }
    return result;
}
TaskQueryResult TaskContext::Evaluate(const TaskRequirement& requirement,const TaskQuery& query) const {
    TaskQueryResult result;auto values=Values();
    auto reason=[&](TaskAvailability state,TaskReasonKind kind,std::string source,std::string detail) {
        result.availability=state;result.reasons.push_back({kind,std::move(source),std::move(detail)});
    };
    for(const auto& [name,value]:query.inputs)values.insert_or_assign(name,TaskResolvedValue{value,{},"input"});
    std::visit([&](const auto& item) {
        using T=std::decay_t<decltype(item)>;
        if constexpr(std::is_same_v<T,TaskValueEquals>) {
            const auto found=values.find(item.name);
            if(found==values.end())reason(TaskAvailability::Unknown,TaskReasonKind::Unresolved,item.name,"condition value is absent");
            else if(TypeOf(found->second.value)!=TypeOf(item.value))reason(TaskAvailability::Unknown,TaskReasonKind::InputType,item.name,"condition value type differs");
            else if(found->second.value!=item.value)reason(TaskAvailability::Unavailable,TaskReasonKind::Condition,item.name,"condition is false");
        } else if constexpr(std::is_same_v<T,TaskPhaseEquals>) {
            const auto phase=Phase();
            if(!phase)reason(TaskAvailability::Unknown,TaskReasonKind::Unresolved,"phase","phase is not set");
            else if(*phase!=item.phase)reason(TaskAvailability::Unavailable,TaskReasonKind::Condition,"phase","phase differs");
        } else if constexpr(std::is_same_v<T,TaskNeedsParticipant>) {
            if(std::none_of(query.participants.begin(),query.participants.end(),[&](const auto& participant){return participant.name==item.participant.name;}))
                reason(TaskAvailability::Unavailable,TaskReasonKind::Participant,item.participant.name,"participant is absent");
        } else if constexpr(std::is_same_v<T,TaskNeedsTarget> || std::is_same_v<T,TaskNeedsAnchor>) {
            const auto& names=std::is_same_v<T,TaskNeedsTarget>?query.targets:query.anchors;
            bool present=std::find(names.begin(),names.end(),item.name)!=names.end();
            if constexpr(std::is_same_v<T,TaskNeedsTarget>)
                present=present || (item.allow_position && std::find(query.position_targets.begin(),query.position_targets.end(),item.name)!=query.position_targets.end());
            if(!present)
                reason(TaskAvailability::Unavailable,TaskReasonKind::MissingInput,item.name,
                    std::is_same_v<T,TaskNeedsTarget>?"owned target is unavailable":"spatial anchor is absent");
        } else if constexpr(std::is_same_v<T,TaskChoiceEquals>) {
            const auto found=query.choices.find(item.name);
            if(found==query.choices.end())reason(TaskAvailability::Unavailable,TaskReasonKind::MissingInput,item.name,"visual choice is absent");
            else if(found->second!=item.value)reason(TaskAvailability::Unavailable,TaskReasonKind::Condition,item.name,"visual choice differs");
        } else reason(TaskAvailability::Unknown,TaskReasonKind::Unresolved,item.source,item.detail);
    },requirement);
    return result;
}
}
