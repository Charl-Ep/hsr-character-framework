#include <hksr/framework/tasks/TaskCatalog.hpp>
#include <hksr/framework/presentation/scene/VisualProgram.hpp>
#include <algorithm>
#include <stdexcept>
#include <set>
#include <type_traits>
#include "TaskPreparation.hpp"

namespace hksr::framework {
namespace {
TaskCatalogInput PreparePlayback(TaskCatalogInput input) {
    bool needed = false;
    input.contexts.erase(TaskPlaybackContext);
    input.initial_contexts.erase(std::remove(input.initial_contexts.begin(), input.initial_contexts.end(),
                                            TaskPlaybackContext), input.initial_contexts.end());
    auto reads_playback = [&](const TaskRequirement& requirement) {
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, TaskValueEquals> || std::is_same_v<T, TaskValueOneOf> ||
                          std::is_same_v<T, TaskNumberCompare>)
                needed = needed || (value.from_context && value.from_context->context == TaskPlaybackContext);
        }, requirement);
    };
    for (auto& [name, plan] : input.tasks) {
        (void)name;
        for (const auto& requirement : plan.definition.requirements)
            reads_playback(requirement);
        auto& on = plan.definition.on;
        // Re-publication of a typed catalog prepares the same policy once.
        for (auto* writes : {&on.accepted, &on.started, &on.completed, &on.interrupted, &on.replaced, &on.failed})
            writes->erase(std::remove_if(writes->begin(), writes->end(), [](const auto& write) {
                return write.context == TaskPlaybackContext;
            }), writes->end());
        if (!plan.definition.runtime)
            continue;
        needed = true;
        const auto& tracking = *plan.definition.runtime;
        const bool ready = tracking.policy == TaskRuntimePolicy::Ready || tracking.policy == TaskRuntimePolicy::Setting;
        TaskValues admission{{"performance", std::string(ready ? "ready" : "accepted")}};
        if (tracking.selected)
            admission.emplace("selected", *tracking.selected);
        on.accepted.push_back({TaskPlaybackContext, std::move(admission)});
        if (!ready)
            on.started.push_back({TaskPlaybackContext, {{"performance", std::string("working")}}});
        if (tracking.policy == TaskRuntimePolicy::Ready || tracking.policy == TaskRuntimePolicy::Performance)
            on.completed.push_back({TaskPlaybackContext, {{"performance", std::string("ready")}}});
        if (tracking.policy != TaskRuntimePolicy::Setting) {
            on.interrupted.push_back({TaskPlaybackContext, {{"performance", std::string("ready")}}});
            on.replaced.push_back({TaskPlaybackContext, {{"performance", std::string("ready")}}});
        }
        on.failed.push_back({TaskPlaybackContext, {{"performance", std::string("failed")}}});
    }
    for (const auto& [name, sequence] : input.sequences) {
        (void)name;
        if (sequence)
            for (const auto& node : sequence->nodes) {
                if (const auto* branch = std::get_if<TaskBranch>(&node.operation))
                    reads_playback(branch->condition);
                if (const auto* submit = std::get_if<TaskSubmit>(&node.operation))
                    if (const auto* command = std::get_if<TaskLifetimeCommand>(&submit->operation))
                        needed = needed || command->context == TaskPlaybackContext ||
                                 (command->value_from && command->value_from->context == TaskPlaybackContext);
            }
    }
    auto visual = input.visual ? std::make_shared<VisualProgram>(*input.visual) : nullptr;
    if (visual)
        visual->contexts.erase(TaskPlaybackContext);
    if (needed) {
        TaskContextDefinition playback;
        playback.state_only = true;
        playback.values = {{"selected", std::string{}}, {"performance", std::string("ready")}};
        input.contexts[TaskPlaybackContext] = std::move(playback);
        if (visual) {
            VisualProgramContext owner;
            owner.restore.emplace();
            visual->contexts[TaskPlaybackContext] = std::move(owner);
        }
        if (std::find(input.initial_contexts.begin(), input.initial_contexts.end(), TaskPlaybackContext) ==
            input.initial_contexts.end())
            input.initial_contexts.push_back(TaskPlaybackContext);
    }
    input.visual = std::move(visual);
    return input;
}
}
TaskCatalogInput::TaskCatalogInput(const TaskCatalog& catalog)
    : id(catalog.id), domain(catalog.domain), visual(catalog.visual), sequences(catalog.sequences),
      tasks(catalog.tasks), contexts(catalog.contexts), initial_contexts(catalog.initial_contexts) {}
TaskCatalog::TaskCatalog(const TaskCatalogInput& input)
    : TaskCatalog(PreparePlayback(input), PreparedInput{}) {}
TaskCatalog::TaskCatalog(TaskCatalogInput&& input, PreparedInput)
    : id(std::move(input.id)), domain(input.domain), visual(std::move(input.visual)),
      sequences(std::move(input.sequences)), tasks(std::move(input.tasks)), contexts(std::move(input.contexts)),
      initial_contexts(std::move(input.initial_contexts)) {
    ValidateTaskCatalog(*this);
    detail::TaskPreparation preparation(*this);
    for (const auto& [name, context] : contexts)
        if (preparation.ContextNeedsMovementOrigin(name))
            moving_contexts_.insert(name);
    for (const auto& [name, sequence] : sequences)
        if (preparation.NeedsMovementOrigin(*sequence))
            moving_sequences_.insert(name);
    for (const auto& [name, plan] : tasks)
        prepared_.emplace(name, PreparedTaskInfo{preparation.HasPresentation(plan.definition),
                                                SequenceNeedsMovementOrigin(plan.definition.entry)});
}
const PreparedTaskInfo& TaskCatalog::Prepared(const std::string& task) const {
    return prepared_.at(task);
}
bool TaskCatalog::ContextNeedsMovementOrigin(const std::string& context) const {
    return moving_contexts_.count(context) != 0;
}
bool TaskCatalog::SequenceNeedsMovementOrigin(const std::string& sequence) const {
    return moving_sequences_.count(sequence) != 0;
}
const TaskPlan& TaskCatalog::Resolve(const TaskKey& key) const {
    if (key.catalog != id)
        throw std::invalid_argument("task belongs to another catalog: " + key.catalog);
    const auto found = tasks.find(key.name);
    if (found == tasks.end())
        throw std::invalid_argument("unknown task: " + key.name);
    return found->second;
}
void ValidateTaskCatalog(const TaskCatalog& catalog) {
    if (catalog.id.empty() || !catalog.visual)
        throw std::invalid_argument("task catalog requires identity and visual resources");
    auto requirement = [&](const TaskRequirement& condition) {
        TaskDefinition check;
        check.key = {catalog.id, "predicate"};
        check.requirements = {condition};
        ValidateTaskDefinition(check);
        if (const auto* present = std::get_if<TaskContextPresent>(&condition))
            if (present->catalog != catalog.id || !catalog.contexts.count(present->name))
                throw std::invalid_argument("unknown task condition context: " + present->name);
        std::visit(
            [&](const auto& item) {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, TaskValueEquals> || std::is_same_v<T, TaskNumberCompare> ||
                              std::is_same_v<T, TaskValueOneOf>)
                    if (item.from_context) {
                        const auto found = catalog.contexts.find(item.from_context->context);
                        if (item.from_context->catalog != catalog.id || found == catalog.contexts.end())
                            throw std::invalid_argument("unknown task value context: " + item.from_context->context);
                        if (!found->second.values.count(item.name) && !found->second.numbers.count(item.name))
                            throw std::invalid_argument("unknown explicit context value: " + item.name);
                    }
            },
            condition);
    };
    for (const auto& [key, sequence] : catalog.sequences) {
        if (key.empty() || !sequence)
            throw std::invalid_argument("empty task sequence");
        for (const auto& node : sequence->nodes)
            if (const auto* branch = std::get_if<TaskBranch>(&node.operation))
                requirement(branch->condition);
    }
    for (const auto& [key, task] : catalog.tasks) {
        ValidateTaskDefinition(task.definition);
        for (const auto& condition : task.definition.requirements)
            requirement(condition);
        for (const auto* writes :
             {&task.definition.on.accepted, &task.definition.on.started, &task.definition.on.completed,
              &task.definition.on.interrupted, &task.definition.on.replaced, &task.definition.on.failed})
            for (const auto& write : *writes) {
                const auto context = catalog.contexts.find(write.context);
                if (context == catalog.contexts.end() || !context->second.state_only)
                    throw std::invalid_argument("task state hook requires a state-only context: " + write.context);
                for (const auto& [name, value] : write.values) {
                    requirement(TaskValueEquals{name, value, TaskValueContext{write.context, catalog.id}});
                    const auto declared = context->second.values.find(name);
                    const auto number = context->second.numbers.find(name);
                    if (declared == context->second.values.end() && number == context->second.numbers.end())
                        throw std::invalid_argument("unknown task state hook value: " + name);
                    const auto expected = declared != context->second.values.end() ? TypeOf(declared->second)
                                          : std::holds_alternative<std::int64_t>(number->second.initial)
                                              ? TaskValueType::Integer
                                              : TaskValueType::Number;
                    if (TypeOf(value) != expected)
                        throw std::invalid_argument("task state hook value type differs: " + name);
                }
            }
        if (task.definition.key.catalog != catalog.id || key != task.definition.key.name)
            throw std::invalid_argument("task catalog identity mismatch: " + key);
        if (!task.definition.entry.empty() && !catalog.sequences.count(task.definition.entry))
            throw std::invalid_argument("unknown task sequence: " + task.definition.entry);
        if (task.definition.mode == TaskExecutionMode::Input && !task.definition.entry.empty())
            for (const auto& node : catalog.sequences.at(task.definition.entry)->nodes)
                if (const auto* submit = std::get_if<TaskSubmit>(&node.operation);
                    submit && RequiresBodyPlayback(submit->operation))
                    throw std::invalid_argument("independent input requires body playback: " + key);
        if (task.next) {
            const auto& successor = catalog.Resolve(task.next->task).definition;
            // Query also reports dynamic prerequisites. Only static argument
            // errors are rejected here; missing values may come from context.
            TaskContext context;
            const auto query = context.Query(successor, {task.next->inputs});
            for (const auto& reason : query.reasons)
                if (reason.kind == TaskReasonKind::InputType)
                    throw std::invalid_argument("invalid successor input: " + reason.source);
        }
    }
    for (const auto& [name, context] : catalog.contexts) {
        if (!catalog.visual->contexts.count(name))
            throw std::invalid_argument("unknown visual context contribution: " + name);
        TaskContext checked;
        auto token = checked.Enter(name);
        checked.SetResilience(token, context.resilience);
        for (const auto& [key, value] : context.values)
            checked.SetValue(token, key, value);
        checked.SetVisualReaction(token, context.visual_reaction);
        if (context.state_only) {
            if (context.phase || context.instances || !context.hit_effects.empty() || context.resilience ||
                context.visual_reaction || !context.events.empty())
                throw std::invalid_argument("state-only context cannot contribute visual or control behavior: " + name);
            if (context.callbacks && (!context.callbacks->enter.empty() || !context.callbacks->exit.empty() ||
                                      !context.callbacks->reapply.empty() || !context.callbacks->restore.empty() ||
                                      !context.callbacks->signals.empty()))
                throw std::invalid_argument("state-only context cannot own callbacks: " + name);
            const auto& visual = catalog.visual->contexts.at(name);
            if (!visual.enter.empty() || !visual.exit.empty() || !visual.restore || !visual.restore->empty() ||
                !visual.event_scope.empty() || !visual.phase.empty() || !visual.restore_effects.empty())
                throw std::invalid_argument("state-only context cannot own visual resources: " + name);
        }
        if (!context.numbers.empty() && !context.callbacks && !context.state_only)
            throw std::invalid_argument("context numbers require a task lifetime: " + name);
        if (!context.events.empty() && !context.callbacks)
            throw std::invalid_argument("context events require a task lifetime: " + name);
        for (const auto& [key, rule] : context.numbers) {
            if (key.empty() || key.find('\0') != std::string::npos || context.values.count(key))
                throw std::invalid_argument("invalid or duplicate context number: " + key);
            ValidateContextNumberRule(rule);
        }
        if (context.instances) {
            const auto& policy = *context.instances;
            if (!context.callbacks || policy.name.empty() || policy.category.empty() || policy.receiver.empty() ||
                policy.caster.empty() ||
                (policy.match == TaskInstanceMatch::ReceiverCasterAbility && policy.ability.empty()))
                throw std::invalid_argument("context instances require callback and matching inputs: " + name);
            // One official animation event has one resource owner. Multi-owner
            // contexts submit their effects through their callback sequences.
            if (!catalog.visual->contexts.at(name).event_scope.empty())
                throw std::invalid_argument("instance callbacks cannot share a singleton native event scope: " + name);
        }
        if (context.callbacks) {
            const auto& visual = catalog.visual->contexts.at(name);
            if (!visual.enter.empty() || !visual.exit.empty())
                throw std::invalid_argument("task callbacks own context entry/exit: " + name);
            auto callback = [&](const std::string& key) {
                if (key.empty())
                    return;
                const auto found = catalog.sequences.find(key);
                if (found == catalog.sequences.end())
                    throw std::invalid_argument("unknown context callback: " + key);
            };
            callback(context.callbacks->enter);
            callback(context.callbacks->exit);
            callback(context.callbacks->reapply);
            callback(context.callbacks->restore);
            for (const auto& [event, key] : context.callbacks->signals)
                callback(key);
            for (const auto& [event, key] : context.events) {
                TaskEventName(event);
                callback(key);
            }
        }
    }
    for (const auto& [key, sequence] : catalog.sequences)
        for (const auto& node : sequence->nodes)
            if (const auto* submit = std::get_if<TaskSubmit>(&node.operation)) {
                if (const auto* command = std::get_if<TaskLifetimeCommand>(&submit->operation)) {
                    const auto definition = catalog.contexts.find(command->context);
                    if (definition == catalog.contexts.end() ||
                        (!definition->second.callbacks && !definition->second.state_only))
                        throw std::invalid_argument("unknown task callback context: " + command->context);
                    if (command->operation == TaskLifetimeOperation::Signal &&
                        (!definition->second.callbacks || !definition->second.callbacks->signals.count(command->event)))
                        throw std::invalid_argument("unknown task context signal: " + command->event);
                    if (command->operation == TaskLifetimeOperation::ChangeValue) {
                        if (bool(command->change) == bool(command->set_value))
                            throw std::invalid_argument("context value operation requires exactly one change");
                        if (command->set_value) {
                            const auto& assignment = *command->set_value;
                            const auto value = definition->second.values.find(assignment.name);
                            const auto number = definition->second.numbers.find(assignment.name);
                            if (value == definition->second.values.end() && number == definition->second.numbers.end())
                                throw std::invalid_argument("unknown context value: " + assignment.name);
                            if (command->value_from)
                                throw std::invalid_argument("typed context set cannot use a number source");
                            if (command->value_input) {
                                if (command->value_input->empty() ||
                                    command->value_input->find('\0') != std::string::npos)
                                    throw std::invalid_argument("invalid context input source");
                            } else {
                                const auto expected = value != definition->second.values.end() ? TypeOf(value->second)
                                                      : std::holds_alternative<std::int64_t>(number->second.initial)
                                                          ? TaskValueType::Integer
                                                          : TaskValueType::Number;
                                if (TypeOf(assignment.value) != expected)
                                    throw std::invalid_argument("context set value type differs");
                            }
                            continue;
                        }
                        if (command->value_input)
                            throw std::invalid_argument("entry input set requires a typed value assignment");
                        const auto found = definition->second.numbers.find(command->change->name);
                        if (found == definition->second.numbers.end())
                            throw std::invalid_argument("unknown context number: " + command->change->name);
                        auto operand = command->change->operand;
                        if (command->value_from) {
                            const auto source = catalog.contexts.find(command->value_from->context);
                            if (source == catalog.contexts.end() ||
                                !source->second.numbers.count(command->value_from->name))
                                throw std::invalid_argument("context number source is absent");
                            operand = source->second.numbers.at(command->value_from->name).initial;
                        }
                        ValidateContextNumber(operand);
                        if (operand.index() != found->second.initial.index())
                            throw std::invalid_argument("context operand type differs");
                    } else if (command->change || command->value_from || command->set_value || command->value_input)
                        throw std::invalid_argument("unexpected context value change");
                }
                if (const auto* instruction = std::get_if<VisualInstruction>(&submit->operation))
                    if (const auto* command = std::get_if<VisualContextOperation>(&instruction->operation)) {
                        const auto definition = catalog.contexts.find(command->name);
                        if (definition != catalog.contexts.end() &&
                            (definition->second.callbacks || definition->second.state_only))
                            throw std::invalid_argument("callback context requires a task lifecycle operation: " +
                                                        command->name);
                    }
            }
    std::set<std::string> initial;
    for (const auto& name : catalog.initial_contexts) {
        const auto found = catalog.contexts.find(name);
        if (found == catalog.contexts.end() || !found->second.state_only || !initial.insert(name).second)
            throw std::invalid_argument("initial context must identify a unique state-only context: " + name);
    }
}
}
