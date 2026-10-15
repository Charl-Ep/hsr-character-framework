#include <hksr/framework/presentation/ReadyActions.hpp>
#include <hksr/animation/ActionRelations.hpp>
#include <cmath>
#include <set>
#include <algorithm>

namespace hksr::framework {
ReadyActions BuildReadyActions(const api::ActionConfigInput& config,const api::ReadyAnimationDefaults& defaults) {
    ReadyActions out;out.source=config.path;out.transitions=config.transitions;out.defaults=defaults;out.skills=config.skills.rows;
    if(config.presentation.initial_state.available)out.initial_state=config.presentation.initial_state.value;
    for(const auto& skill:config.skills.rows) {
        auto add=[&](int variant,const api::TextInput& state,api::ConfigValue<bool> condition) {
            if(!state.available || state.value.empty())return;
            ReadyPose row;row.skill_index=skill.source_index;row.variant_index=variant;
            row.state=state.value;row.use_transition=skill.use_transition;row.has_condition=condition;
            row.effect_path=skill.ready_effect;row.wait=skill.ready_wait;
            row.entry_ability=skill.entry_ability;row.prepare_ability=skill.prepare_ability;row.child_skills=skill.child_skills;
            if(skill.name.available)row.skill_name=skill.name.value;
            row.label=(skill.name.available?skill.name.value:std::string{})+" | "+row.state;
            out.poses.push_back(std::move(row));
        };
        add(-1,skill.ready_state,{true,false,{}});
        for(const auto& variant:skill.variants.rows)add(variant.source_index,variant.state,variant.has_condition);
    }
    return out;
}
void AppendElationReadyPoses(ReadyActions& actions,const api::ActionConfigInput& config) {
    for(const auto& source:config.elation_ready.rows) {
        const api::SkillInput* skill=nullptr;unsigned matches=0;
        for(const auto& candidate:actions.skills)
            if(source.skill.available && candidate.name.available && candidate.name.value==source.skill.value){skill=&candidate;++matches;}
        if(matches!=1)skill=nullptr; // pose itself does not depend on a unique skill match
        for(const auto* state:{&source.entry,&source.loop}) {
            if(!state->available || state->value.empty())continue;
            const bool exists=std::any_of(actions.poses.begin(),actions.poses.end(),[&](const ReadyPose& row){
                return row.skill_name==source.skill.value && row.variant_index==-1 && row.state==state->value;
            });
            if(exists)continue;
            ReadyPose row;row.skill_name=source.skill.value;row.state=state->value;
            row.label=row.skill_name+" | "+row.state;row.preparation_source=config.path;
            row.use_transition={true,false,{}};row.has_condition={true,false,{}};
            if(skill) {
                row.skill_index=skill->source_index;row.use_transition=skill->use_transition;
                row.entry_ability=skill->entry_ability;row.prepare_ability=skill->prepare_ability;row.child_skills=skill->child_skills;
                row.effect_path=skill->ready_effect;row.wait=skill->ready_wait;
            }
            actions.poses.push_back(std::move(row));
        }
    }
}
void ResolveReadyPreparations(ReadyActions& actions,const std::vector<api::ConfigReference>& roots,
    const std::map<std::string,api::AbilitySources>& sources) {
    // A narrowly proven configuration relationship, not a skill interpreter:
    // straight OnStart sequence maps a self skill key, plays one self pose,
    // then asks for that same control slot again. No predicates are evaluated.
    std::map<std::string,std::map<std::string,std::string>> found;
    std::set<std::string> ambiguous;
    std::set<std::string> visited;
    for(const auto& root:roots) {
        if(!visited.insert(root.value).second)continue;
        const auto source=sources.find(root.value);if(source==sources.end())continue;
        for(const auto& sequence:api::FindPreparationSequences(source->second)) {
            const auto& finish=sequence.continuation;
            const auto& mapping=sequence.mapping;
            if(!finish.enabled.available || !finish.enabled.value || !finish.child.available || finish.child.value!=-1)continue;
            if(!mapping.enabled.available || !mapping.enabled.value || !mapping.skill.available ||
                !mapping.target_alias.available || mapping.target_alias.value!="Caster")continue;
            std::set<std::string> states;
            for(const auto& animation:sequence.animations) {
                if(animation.state.available && !animation.state.value.empty() &&
                    animation.enabled.available && animation.enabled.value && animation.target_alias.available && animation.target_alias.value=="Caster")
                    states.insert(animation.state.value);
            }
            if(states.size()==1)found[mapping.skill.value].emplace(*states.begin(),root.value);
            else ambiguous.insert(mapping.skill.value);
        }
    }
    for(auto& pose:actions.poses) {
        const auto source=found.find(pose.skill_name);
        if(pose.variant_index!=-1 || source==found.end() || source->second.size()!=1 || ambiguous.count(pose.skill_name))continue;
        pose.state=source->second.begin()->first;pose.preparation_source=source->second.begin()->second;
        pose.label=pose.skill_name+" | "+pose.state;
    }
}
ReadyEdge SelectReadyEdge(const ReadyActions& actions,int from,const ReadyPose& target) {
    ReadyEdge out;out.source="direct";
    const auto number=[](const auto& value){return value.available && std::isfinite(value.value) && value.value>=0;};
    if(number(actions.defaults.transition_in))out.blend=actions.defaults.transition_in.value;
    if(from<0 || from==target.skill_index || !target.use_transition.available || !target.use_transition.value) {
        if(!target.use_transition.available)out.note="transition policy unread; direct visual pose";
        if(from==target.skill_index)out.blend=0;
        return out;
    }
    const auto match=api::FindReadyTransition(actions.transitions,actions.defaults.transitions,from,target.skill_index);
    switch(match.resolution) {
    case api::ReadyTransitionResolution::UnreadKey:
        out.note="earlier transition key unread; first-match result unknown";return out;
    case api::ReadyTransitionResolution::IncompleteTable:
        out.note="character transition table incomplete; fallback not asserted";return out;
    case api::ReadyTransitionResolution::InvalidIndex:
        out.note="invalid transition index; direct visual pose";return out;
    case api::ReadyTransitionResolution::Absent:return out;
    case api::ReadyTransitionResolution::Found:break;
    }
    out.source=match.table==api::ReadyTransitionTable::Character?"character":"official-default";
    const auto& selected=match.row;
    if(!selected->state.available){out.note="transition name unread; direct visual pose";return out;}
    out.state=selected->state.value;
    if(number(selected->transition_in))out.blend=selected->transition_in.value;
    if(number(selected->reverse_outro))out.reverse_until=selected->reverse_outro.value;
    return out;
}
}
