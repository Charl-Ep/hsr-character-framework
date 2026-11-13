#pragma once
#include <hksr/framework/tasks/TaskModel.hpp>
#include <hksr/framework/presentation/reactions/HitEffectPolicy.hpp>
#include <hksr/framework/presentation/reactions/DeathOccurrence.hpp>

namespace hksr::framework {
struct TaskVisualReaction {
    std::string source;
    std::int32_t flag = 0;
    DeathOccurrence occurrence = DeathOccurrence::Configured;
};
struct TaskContextView {
    TaskContextToken token;
    std::string name;
    TaskValues values;
    std::optional<std::string> phase;
    std::string catalog;
    std::vector<HitEffectOverride> hit_effects;
    std::optional<int> resilience;
    std::optional<TaskVisualReaction> visual_reaction;
};
struct TaskResolvedValue {
    TaskValue value;
    TaskContextToken owner;
    std::string context;
};
// Per actor, engine-thread data. Removing one owner cannot clear another
// owner's contribution. Native resources remain with VisualEffectContext and
// the existing partner/Timeline owners; this class never calls the engine.
class TaskContext {
  public:
    TaskContextToken Enter(std::string name, std::string catalog = {});
    bool Exit(TaskContextToken);
    void SetValue(TaskContextToken, const std::string&, TaskValue);
    void SetPhase(TaskContextToken, std::optional<std::string>);
    void SetHitEffects(TaskContextToken, std::vector<HitEffectOverride>);
    std::vector<ContextHitEffects> HitEffects() const;
    void SetResilience(TaskContextToken, std::optional<int>);
    std::optional<int> Resilience() const;
    void SetVisualReaction(TaskContextToken, std::optional<TaskVisualReaction>);
    std::optional<TaskVisualReaction> VisualReaction() const;
    const std::vector<TaskContextView>& Owners() const {
        return owners_;
    }
    std::map<std::string, TaskResolvedValue> Values() const;
    // Exact catalog/name; the last active contribution wins. Inputs do not
    // shadow an explicitly selected live context.
    std::optional<TaskResolvedValue> Value(const TaskValueContext&, const std::string&) const;
    std::optional<std::string> Phase() const;
    // The definition is validated at catalog publication; this reads live inputs.
    TaskQueryResult Query(const TaskDefinition&, const TaskQuery&) const;
    TaskQueryResult Evaluate(const TaskRequirement&, const TaskQuery&) const;

  private:
    TaskContextView& Owner(TaskContextToken);
    std::uint64_t next_ = 0;
    std::vector<TaskContextView> owners_;
};
void ValidateTaskDefinition(const TaskDefinition&);
}
