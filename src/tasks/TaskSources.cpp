#include <hksr/framework/tasks/TaskSources.hpp>

namespace hksr::framework {
TaskSourceInventory BuildTaskSources(const api::TaskConfigInput& input) {
    TaskSourceInventory result;result.document=input.actions.path;result.loaded=input.actions.loaded;
    result.skills_enumerated=input.actions.skills.enumerated;result.groups_enumerated=input.skill_abilities.enumerated;
    result.issues=input.actions.issues;result.issues.insert(result.issues.end(),input.issues.begin(),input.issues.end());
    for(const auto& skill:input.skills) {
        TaskSkillSource row;row.source={result.document,"SkillList["+std::to_string(skill.skill.source_index)+"]"};row.input=skill;
        auto add=[&](TaskEntryRole role,const api::TextInput& value,const char* field) {
            if(value.available && !value.value.empty())row.entries.push_back({role,{result.document,row.source.field+"."+field},value.value});
        };
        add(TaskEntryRole::Entry,skill.skill.entry_ability,"EntryAbility");
        add(TaskEntryRole::Prepare,skill.skill.prepare_ability,"PrepareAbility");
        result.skills.push_back(std::move(row));
    }
    for(const auto& group:input.skill_abilities.rows) {
        TaskAbilityGroupSource row;row.source={result.document,"SkillAbilityList["+std::to_string(group.source_index)+"]"};row.input=group;
        if(group.skill.available && !group.skill.value.empty())for(const auto& skill:input.skills)
            if(skill.skill.name.available && skill.skill.name.value==group.skill.value)row.skill_indices.push_back(skill.skill.source_index);
        if(!group.skill.available || row.skill_indices.size()!=1)
            result.issues.push_back({group.source_index,row.source.field+".Skill",!group.skill.available?"skill owner was not read":
                row.skill_indices.empty()?"no literal skill owner match":"multiple literal skill owner matches"});
        result.groups.push_back(std::move(row));
    }
    return result;
}
}
