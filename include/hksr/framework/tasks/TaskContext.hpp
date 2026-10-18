#pragma once
#include "TaskModel.hpp"

namespace hksr::framework {
struct TaskTag {std::string name;};
using TaskDisableSelector=std::variant<TaskKey,TaskSlot,TaskTag>;
struct TaskDisableContribution {TaskDisableSelector selector;std::string reason;};
struct TaskContextView {
    TaskContextToken token;
    std::string name;
    TaskValues values;
    std::optional<std::string> phase;
    std::map<std::int32_t,TaskKey> slots;
    std::vector<TaskDisableContribution> disables;
};
struct TaskResolvedValue {TaskValue value;TaskContextToken owner;std::string context;};
// Per actor, engine-thread data. Removing one owner cannot clear another
// owner's contribution. Native resources remain with VisualEffectContext and
// the existing partner/Timeline owners; this class never calls the engine.
class TaskContext {
public:
    TaskContextToken Enter(std::string name);
    bool Exit(TaskContextToken);
    void SetValue(TaskContextToken,const std::string&,TaskValue);
    void SetPhase(TaskContextToken,std::optional<std::string>);
    void SetSlot(TaskContextToken,TaskSlot,TaskKey);
    void Disable(TaskContextToken,TaskDisableContribution);
    const std::vector<TaskContextView>& Owners() const {return owners_;}
    std::map<std::string,TaskResolvedValue> Values() const;
    std::optional<std::string> Phase() const;
    TaskKey ResolveSlot(TaskSlot,const TaskKey& baseline) const;
    TaskQueryResult Query(const TaskDefinition&,const TaskQuery&) const;
private:
    TaskContextView& Owner(TaskContextToken);
    std::uint64_t next_=0;
    std::vector<TaskContextView> owners_;
};
void ValidateTaskDefinition(const TaskDefinition&);
}
