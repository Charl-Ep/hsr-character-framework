#pragma once
#include <hksr/animation/OfficialCatalog.hpp>
#include <memory>
#include <map>

namespace hksr::framework {
struct ReadyPose {
    int skill_index=-1,variant_index=-1;
    std::string label,state;
    api::ConfigValue<bool> use_transition,has_condition;
    api::TextInput effect_path;
    api::ConfigValue<float> wait;
    // Keep the official relationship, not a state inferred from a skill name.
    api::TextInput entry_ability,prepare_ability;
    api::TextArrayInput child_skills;
    std::string skill_name,preparation_source;
};
// Visual preview of the declared BASE SkillList, not a live battle skill map.
// Preserve original indices (including omitted/null entries); variants share
// their parent's index. No skill/layout/condition/gameplay objects are created.
struct ReadyActions {
    std::string source;
    std::string initial_state;
    // Includes children with no ready pose of their own. ChildSkillList links
    // SkillConfig.Name, not pose indices or Animator state names.
    std::vector<api::SkillInput> skills;
    std::vector<ReadyPose> poses;
    api::CatalogBatch<api::ReadyTransitionInput> transitions;
    api::ReadyAnimationDefaults defaults;
};
struct ReadyState {std::shared_ptr<const ReadyActions> actions;std::size_t pose=0;};
struct ReadyEdge {
    std::string state,source,note;
    float blend=0,reverse_until=0;
};
ReadyActions BuildReadyActions(const api::ActionConfigInput& config,const api::ReadyAnimationDefaults& defaults);
// Explicit entry/loop names from the linked official Elation configuration.
// This enumerates selectable poses; it does not activate Elation gameplay or
// invent the timing of a transition between them.
void AppendElationReadyPoses(ReadyActions&,const api::ActionConfigInput& elation);
void ResolveReadyPreparations(ReadyActions&,const std::vector<api::ConfigReference>&,
    const std::map<std::string,api::AbilitySources>&);
ReadyEdge SelectReadyEdge(const ReadyActions& actions,int from_index,const ReadyPose& target);
}
