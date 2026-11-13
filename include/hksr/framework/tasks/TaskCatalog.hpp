#pragma once
#include <hksr/framework/tasks/TaskContext.hpp>
#include <hksr/framework/tasks/TaskSequence.hpp>
#include <hksr/framework/presentation/playback/ActionTypes.hpp>
#include <set>

namespace hksr::framework {
struct VisualProgram;
struct TaskContinuation {
    TaskKey task;
    TaskValues inputs;
};
struct TaskPlan {
    TaskDefinition definition;
    std::optional<TaskContinuation> next;
};
// Contributions apply only while the corresponding visual context is active.
// The visual lifetime, not a copied phase string, owns these values.
struct TaskContextDefinition {
    TaskValues values;
    std::optional<std::string> phase;
    std::optional<TaskLifetimeCallbacks> callbacks;
    std::optional<TaskInstancePolicy> instances;
    std::map<std::string, ContextNumberRule> numbers;
    std::vector<HitEffectOverride> hit_effects;
    std::optional<int> resilience;
    std::optional<TaskVisualReaction> visual_reaction;
    std::map<TaskEventKind, std::string> events;
    bool state_only = false;
};
struct TaskCatalog;
struct TaskCatalogInput {
    std::string id;
    ActionDomain domain = ActionDomain::Battle;
    std::shared_ptr<const VisualProgram> visual;
    std::map<std::string, std::shared_ptr<const TaskSequence>> sequences;
    std::map<std::string, TaskPlan> tasks;
    std::map<std::string, TaskContextDefinition> contexts;
    std::vector<std::string> initial_contexts;
    TaskCatalogInput() = default;
    TaskCatalogInput(const TaskCatalog&);
};
struct PreparedTaskInfo {
    bool has_presentation = false;
    bool needs_movement_origin = false;
};
// Construction is the single validation/preparation boundary for file and
// programmatic input. Visual data is copied to detach mutable source aliases.
struct TaskCatalog {
    explicit TaskCatalog(const TaskCatalogInput&);
    const std::string id;
    const ActionDomain domain;
    const std::shared_ptr<const VisualProgram> visual;
    const std::map<std::string, std::shared_ptr<const TaskSequence>> sequences;
    const std::map<std::string, TaskPlan> tasks;
    const std::map<std::string, TaskContextDefinition> contexts;
    const std::vector<std::string> initial_contexts;
    const TaskPlan& Resolve(const TaskKey&) const;
    const PreparedTaskInfo& Prepared(const std::string& task) const;
    bool ContextNeedsMovementOrigin(const std::string& context) const;
    bool SequenceNeedsMovementOrigin(const std::string& sequence) const;
  private:
    struct PreparedInput {};
    TaskCatalog(TaskCatalogInput&&, PreparedInput);
    std::map<std::string, PreparedTaskInfo> prepared_;
    std::set<std::string> moving_contexts_;
    std::set<std::string> moving_sequences_;
};
TaskCatalog ParseTaskCatalog(const std::string& text);
void ValidateTaskCatalog(const TaskCatalog&);
}
