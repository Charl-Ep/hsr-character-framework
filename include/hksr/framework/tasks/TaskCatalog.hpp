#pragma once
#include "TaskContext.hpp"
#include "TaskSequence.hpp"
#include "SkillSlots.hpp"
#include <hksr/framework/presentation/ActionTypes.hpp>

namespace hksr::framework {
struct TaskContinuation {TaskKey task;TaskValues inputs;};
struct TaskPlan {
    TaskDefinition definition;
    std::optional<TaskContinuation> next;
};
// Contributions apply only while the corresponding visual context is active.
// The visual lifetime, not a copied phase string, owns these values.
struct TaskContextDefinition {
    TaskValues values;
    std::optional<std::string> phase;
    std::map<std::int32_t,TaskKey> slots;
    std::vector<TaskDisableContribution> disables;
};
struct TaskCatalog {
    std::string id;
    ActionDomain domain=ActionDomain::Alternate;
    std::shared_ptr<const VisualProgram> visual;
    std::map<std::string,std::shared_ptr<const TaskSequence>> sequences;
    std::map<std::string,TaskPlan> tasks;
    std::map<std::string,TaskContextDefinition> contexts;
    std::vector<std::string> automatic;
    SkillSlotSetup control_slots;
    const TaskPlan& Resolve(const TaskKey&) const;
};
TaskCatalog ParseTaskCatalog(const std::string& text);
void ValidateTaskCatalog(const TaskCatalog&);
}
