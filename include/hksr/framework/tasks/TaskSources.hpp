#pragma once
#include "TaskModel.hpp"
#include <hksr/animation/OfficialCatalog.hpp>

namespace hksr::framework {
enum class TaskEntryRole { Entry,Prepare };
struct TaskEntrySource {TaskEntryRole role;TaskSourceLocation source;std::string ability;};
struct TaskSkillSource {
    TaskSourceLocation source;
    api::SkillTaskInput input;
    std::vector<TaskEntrySource> entries;
};
struct TaskAbilityGroupSource {
    TaskSourceLocation source;
    api::SkillAbilityInput input;
    std::vector<std::int32_t> skill_indices; // zero or multiple matches stay explicit
};
struct TaskSourceInventory {
    std::string document;
    bool loaded=false,skills_enumerated=false,groups_enumerated=false;
    std::vector<TaskSkillSource> skills;
    std::vector<TaskAbilityGroupSource> groups;
    std::vector<api::CatalogIssue> issues;
};
// All configured skills, including passive/inserted entries without a ready
// pose. This is provenance, not a list of executable visual tasks.
TaskSourceInventory BuildTaskSources(const api::TaskConfigInput&);
}
