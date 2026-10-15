#pragma once
#include <hksr/animation/OfficialCatalog.hpp>
#include <algorithm>

namespace hksr::framework {
struct ActionRetirement {
    std::string state;
    api::EffectRemovalSource removal;
};
// Preview EXIT policy, not a reconstruction of the ability scheduler/timing.
// Keep an explicit self-target removal after a matching animation/wait in the
// SAME ordered configuration array. Never merge sibling predicate branches or
// infer a relationship from names such as Skill02, scene, tail, etc.
inline bool SameSequence(const std::vector<api::ConfigStep>& a,const std::vector<api::ConfigStep>& b) {
    if(a.empty() || a.size()!=b.size() || a.back().selection!=api::ConfigSelection::Index ||
       b.back().selection!=api::ConfigSelection::Index)return false;
    for(std::size_t i=0;i<a.size();++i) {
        const auto& x=a[i];const auto& y=b[i];
        if(x.declaring_type!=y.declaring_type || x.field!=y.field || x.selection!=y.selection || x.key!=y.key ||
           (i+1<a.size() && x.index!=y.index))return false;
    }
    return true;
}
inline bool LiteralSelfRemoval(const api::EffectRemovalInput& r) {
    return r.enabled.available && r.enabled.value && r.target_alias.available && r.target_alias.value=="Caster" &&
        r.effect_path.available && !r.effect_path.value.empty() && r.path_key.available && r.path_key.value.empty();
}
inline std::vector<ActionRetirement> LinkActionRetirements(const api::AbilitySources& source) {
    std::vector<ActionRetirement> result;
    for(const auto& removal:source.removals) {
        if(!LiteralSelfRemoval(removal.input))continue;
        if(removal.path.empty())continue;
        const auto& sequence=removal.path.back();
        // Only the serial containers qualified by current configuration and
        // task execution evidence. Array order alone does not make a parallel
        // task container sequential. Unlinked sources remain in AbilitySources.
        const bool serial=(sequence.declaring_type=="RPG.GameCore.PredicateTaskList" &&
            (sequence.field=="SuccessTaskList" || sequence.field=="FailedTaskList"));
        if(!serial)continue;
        const api::AnimationTaskSource* previous=nullptr;
        for(const auto& animation:source.animations) {
            if(animation.ability!=removal.ability || !SameSequence(animation.path,removal.path) ||
                animation.path.back().index>=removal.path.back().index)continue;
            if(!previous || animation.path.back().index>previous->path.back().index)previous=&animation;
        }
        // A later unsupported/disabled/different-target animation is a boundary,
        // never permission to attach its cleanup to an earlier state.
        if(!previous || !previous->enabled.available || !previous->enabled.value ||
            !previous->state.available || previous->state.value.empty() ||
            !previous->target_alias.available || previous->target_alias.value!="Caster")continue;
        const auto matching_wait=std::find_if(source.waits.begin(),source.waits.end(),[&](const auto& wait){
            return wait.ability==removal.ability && SameSequence(wait.path,removal.path) &&
                wait.path.back().index>previous->path.back().index && wait.path.back().index<removal.path.back().index &&
                wait.enabled.available && wait.enabled.value && wait.state.available && wait.state.value==previous->state.value &&
                wait.target_alias.available && wait.target_alias.value=="Caster";
        });
        if(matching_wait!=source.waits.end())result.push_back({previous->state.value,removal});
    }
    return result;
}
}
