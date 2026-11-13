#pragma once
#include <hksr/framework/tasks/TaskCatalog.hpp>
#include <hksr/framework/presentation/scene/VisualProgram.hpp>
#include <algorithm>
#include <type_traits>
#include <set>

namespace hksr::framework::detail {
// Static metadata prepared once with the validated catalog. This does not
// establish native playback availability.
class TaskPreparation {
  public:
    explicit TaskPreparation(const TaskCatalog& catalog) : catalog_(catalog) {}
    bool HasPresentation(const TaskDefinition& task) {
        const bool idle = std::find(task.tags.begin(), task.tags.end(), "script-idle") != task.tags.end() ||
                          std::find(task.tags.begin(), task.tags.end(), "idle-provider") != task.tags.end();
        return Sequence(task.entry, idle);
    }
    bool ContextNeedsMovementOrigin(const std::string& name) {
        if (!moving_contexts_.insert(name).second)
            return false;
        struct Exit {
            std::set<std::string>& values;
            const std::string& name;
            ~Exit() { values.erase(name); }
        } exit{moving_contexts_, name};
        const auto context = catalog_.contexts.find(name);
        if (context == catalog_.contexts.end() || context->second.state_only)
            return false;
        auto sequence = [&](const std::string& name) {
            const auto found = catalog_.sequences.find(name);
            return found != catalog_.sequences.end() && NeedsMovementOrigin(*found->second);
        };
        const auto& definition = context->second;
        if (definition.callbacks) {
            const auto& callbacks = *definition.callbacks;
            if (sequence(callbacks.enter) || sequence(callbacks.exit) || sequence(callbacks.reapply) ||
                sequence(callbacks.restore))
                return true;
            for (const auto& [event, callback] : callbacks.signals)
                if (sequence(callback))
                    return true;
        }
        for (const auto& [event, callback] : definition.events)
            if (sequence(callback))
                return true;
        if (catalog_.visual) {
            const auto visual = catalog_.visual->contexts.find(name);
            if (visual != catalog_.visual->contexts.end()) {
                auto moving = [&](const auto& instructions) {
                    return std::any_of(instructions.begin(), instructions.end(), [&](const auto& instruction) {
                        const auto* change = std::get_if<VisualContextOperation>(&instruction.operation);
                        return change && ContextNeedsMovementOrigin(change->name);
                    });
                };
                return moving(visual->second.enter) || moving(visual->second.exit) ||
                       (visual->second.restore && moving(*visual->second.restore));
            }
        }
        return false;
    }
    bool NeedsMovementOrigin(const TaskSequence& sequence) {
        if (sequence.NeedsMovementOrigin())
            return true;
        std::set<TaskNodeIndex> seen;
        std::function<bool(TaskNodeIndex)> visit = [&](TaskNodeIndex index) {
            if (!seen.insert(index).second)
                return false;
            return std::visit([&](const auto& operation) {
                using T = std::decay_t<decltype(operation)>;
                if constexpr (std::is_same_v<T, TaskSubmit>) {
                    if (const auto* lifetime = std::get_if<TaskLifetimeCommand>(&operation.operation))
                        return ContextNeedsMovementOrigin(lifetime->context);
                    if (const auto* instruction = std::get_if<VisualInstruction>(&operation.operation))
                        if (const auto* context = std::get_if<VisualContextOperation>(&instruction->operation))
                            return ContextNeedsMovementOrigin(context->name);
                    return false;
                } else if constexpr (std::is_same_v<T, TaskSerial> || std::is_same_v<T, TaskParallel>)
                    return std::any_of(operation.children.begin(), operation.children.end(), visit);
                else if constexpr (std::is_same_v<T, TaskAbilityCall> || std::is_same_v<T, TaskRepeat> ||
                                   std::is_same_v<T, TaskOnSuccess>)
                    return visit(operation.child);
                else if constexpr (std::is_same_v<T, TaskBranch>)
                    return visit(operation.yes) || visit(operation.no);
                else if constexpr (std::is_same_v<T, TaskForTargets>)
                    return visit(operation.child) || (operation.empty && visit(*operation.empty));
                else
                    return false;
            }, sequence.nodes[index].operation);
        };
        return visit(sequence.entry) || (sequence.interruption && visit(*sequence.interruption));
    }

  private:
    const TaskCatalog& catalog_;
    std::set<std::pair<const TaskSequence*, TaskNodeIndex>> visiting_;
    std::set<std::string> contexts_;
    std::set<std::string> moving_contexts_;
    bool Sequence(const std::string& name, bool idle = false) {
        const auto found = catalog_.sequences.find(name);
        return found != catalog_.sequences.end() && found->second && Node(*found->second, found->second->entry, idle);
    }
    bool Context(const std::string& name) {
        if (!contexts_.insert(name).second)
            return false;
        struct Exit {
            std::set<std::string>& values;
            const std::string& name;
            ~Exit() {
                values.erase(name);
            }
        } exit{contexts_, name};
        const auto logical = catalog_.contexts.find(name);
        if (logical != catalog_.contexts.end()) {
            const auto& context = logical->second;
            if (!context.hit_effects.empty() || context.resilience || context.visual_reaction)
                return true; // consumed by owned hit presentation
            if (context.callbacks) {
                const auto& callbacks = *context.callbacks;
                if (Sequence(callbacks.enter) || Sequence(callbacks.exit) || Sequence(callbacks.reapply))
                    return true;
                for (const auto& row : callbacks.signals)
                    if (Sequence(row.second))
                        return true;
            }
            for (const auto& row : context.events)
                if (Sequence(row.second))
                    return true;
            auto reads = [&](const TaskRequirement& requirement) {
                return std::visit(
                    [&](const auto& input) {
                        using T = std::decay_t<decltype(input)>;
                        if constexpr (std::is_same_v<T, TaskValueEquals> || std::is_same_v<T, TaskValueOneOf> ||
                                      std::is_same_v<T, TaskNumberCompare>) {
                            const bool owner = !input.from_context || (input.from_context->context == name &&
                                                                       input.from_context->catalog == catalog_.id);
                            return owner && (context.values.count(input.name) || context.numbers.count(input.name));
                        } else if constexpr (std::is_same_v<T, TaskPhaseEquals>)
                            return context.phase == input.phase;
                        else if constexpr (std::is_same_v<T, TaskContextPresent>)
                            return input.name == name && input.catalog == catalog_.id;
                        else
                            return false;
                    },
                    requirement);
            };
            for (const auto& row : catalog_.tasks)
                if (std::any_of(row.second.definition.requirements.begin(), row.second.definition.requirements.end(),
                                reads) &&
                    Sequence(row.second.definition.entry))
                    return true;
            // State-only data counts only if a real presentation reads it.
            // Merely naming a number/phase in an inert context is not enough.
            for (const auto& row : catalog_.tasks) {
                const auto found = catalog_.sequences.find(row.second.definition.entry);
                if (found == catalog_.sequences.end())
                    continue;
                const auto& sequence = *found->second;
                std::vector<TaskNodeIndex> pending{sequence.entry};
                std::set<TaskNodeIndex> seen;
                while (!pending.empty()) {
                    const auto index = pending.back();
                    pending.pop_back();
                    if (index >= sequence.nodes.size() || !seen.insert(index).second)
                        continue;
                    const auto& operation = sequence.nodes[index].operation;
                    if (const auto* branch = std::get_if<TaskBranch>(&operation);
                        branch && reads(branch->condition) &&
                        (Node(sequence, branch->yes, false) || Node(sequence, branch->no, false)))
                        return true;
                    std::visit(
                        [&](const auto& command) {
                            using T = std::decay_t<decltype(command)>;
                            if constexpr (std::is_same_v<T, TaskSerial> || std::is_same_v<T, TaskParallel>)
                                pending.insert(pending.end(), command.children.begin(), command.children.end());
                            else if constexpr (std::is_same_v<T, TaskBranch>) {
                                pending.push_back(command.yes);
                                pending.push_back(command.no);
                            } else if constexpr (std::is_same_v<T, TaskAbilityCall> || std::is_same_v<T, TaskRepeat> ||
                                                 std::is_same_v<T, TaskOnSuccess>)
                                pending.push_back(command.child);
                            else if constexpr (std::is_same_v<T, TaskForTargets>) {
                                pending.push_back(command.child);
                                if (command.empty)
                                    pending.push_back(*command.empty);
                            }
                        },
                        operation);
                }
            }
        }
        if (catalog_.visual) {
            const auto visual = catalog_.visual->contexts.find(name);
            if (visual != catalog_.visual->contexts.end()) {
                const auto& context = visual->second;
                if (!context.event_scope.empty() || !context.phase.empty() || !context.restore_effects.empty())
                    return true;
                for (const auto* steps : {&context.enter, &context.exit})
                    for (const auto& step : *steps)
                        if (Instruction(step))
                            return true;
                if (context.restore)
                    for (const auto& step : *context.restore)
                        if (Instruction(step))
                            return true;
            }
        }
        return false;
    }
    bool Effect(const std::string& name) const {
        if (!catalog_.visual)
            return false;
        const auto found = catalog_.visual->effects.find(name);
        return found != catalog_.visual->effects.end() && !found->second.path.empty();
    }
    bool Instruction(const VisualInstruction& instruction) {
        return std::visit(
            [&](const auto& command) {
                using T = std::decay_t<decltype(command)>;
                if constexpr (std::is_same_v<T, VisualAssign>)
                    return false;
                else if constexpr (std::is_same_v<T, VisualContextOperation>)
                    return Context(command.name);
                else if constexpr (std::is_same_v<T, VisualCreate> || std::is_same_v<T, VisualRemove> ||
                                   std::is_same_v<T, VisualTrigger>) {
                    return std::any_of(command.effects.begin(), command.effects.end(),
                                       [&](const auto& effect) { return Effect(effect); });
                } else if constexpr (std::is_same_v<T, VisualPartnerOperation>) {
                    if (!catalog_.visual)
                        return false;
                    const auto found = catalog_.visual->partners.find(command.partner);
                    return found != catalog_.visual->partners.end() && !found->second.model.empty();
                } else if constexpr (std::is_same_v<T, VisualTimeline>)
                    return !command.path.empty() || (!command.play && !command.key.empty());
                else if constexpr (std::is_same_v<T, VisualProgress> || std::is_same_v<T, VisualFollow> ||
                                   std::is_same_v<T, VisualEffectState> || std::is_same_v<T, VisualEffectParameter> ||
                                   std::is_same_v<T, VisualEffectLayout>)
                    return Effect(command.effect);
                else
                    return true; // typed renderer/model/attachment/timing/audio units have an actual native consumer
            },
            instruction.operation);
    }
    bool Leaf(const TaskLeafOperation& operation, bool idle) {
        return std::visit(
            [&](const auto& command) {
                using T = std::decay_t<decltype(command)>;
                if constexpr (std::is_same_v<T, TaskAnimationCommand> || std::is_same_v<T, TaskExpressionCommand>)
                    return !command.state.empty() && (idle || !command.current_idle);
                else if constexpr (std::is_same_v<T, TaskSoundCommand>)
                    return !command.event.empty();
                else if constexpr (std::is_same_v<T, TaskHitCommand>)
                    return !command.hit.animation.empty() || (command.hit.effect && !command.hit.effect->path.empty());
                else if constexpr (std::is_same_v<T, TaskPoseCommand> ||
                                   std::is_same_v<T, TaskAnimationParameterCommand>)
                    return true;
                else if constexpr (std::is_same_v<T, TaskMovementCommand>) {
                    if (!command.animation.empty())
                        return true;
                    return std::any_of(
                        command.movement.segments.begin(), command.movement.segments.end(), [](const auto& segment) {
                            return segment.destination != MovementDestination::Origin || segment.offset.x != 0 ||
                                   segment.offset.y != 0 || segment.offset.z != 0;
                        });
                } else if constexpr (std::is_same_v<T, VisualProjectilePlan>)
                    return !command.shots.empty();
                else if constexpr (std::is_same_v<T, VisualInstruction>)
                    return Instruction(command);
                else if constexpr (std::is_same_v<T, TaskLifetimeCommand>)
                    return Context(command.context);
                else
                    return false; // pure event/attack bookkeeping is not itself a visible or audible unit
            },
            operation);
    }
    bool Node(const TaskSequence& sequence, TaskNodeIndex index, bool idle) {
        if (index >= sequence.nodes.size() || !visiting_.insert({&sequence, index}).second)
            return false;
        struct Exit {
            decltype(visiting_)& values;
            std::pair<const TaskSequence*, TaskNodeIndex> key;
            ~Exit() {
                values.erase(key);
            }
        } exit{visiting_, {&sequence, index}};
        const auto& node = sequence.nodes[index];
        return std::visit(
            [&](const auto& command) {
                using T = std::decay_t<decltype(command)>;
                if constexpr (std::is_same_v<T, TaskSubmit>)
                    return Leaf(command.operation, idle);
                else if constexpr (std::is_same_v<T, TaskSerial> || std::is_same_v<T, TaskParallel>) {
                    return std::any_of(command.children.begin(), command.children.end(),
                                       [&](auto child) { return Node(sequence, child, idle); });
                } else if constexpr (std::is_same_v<T, TaskAbilityCall> || std::is_same_v<T, TaskRepeat> ||
                                     std::is_same_v<T, TaskOnSuccess>)
                    return Node(sequence, command.child, idle);
                else if constexpr (std::is_same_v<T, TaskBranch>)
                    return Node(sequence, command.yes, idle) || Node(sequence, command.no, idle);
                else if constexpr (std::is_same_v<T, TaskForTargets>)
                    return Node(sequence, command.child, idle) ||
                           (command.empty && Node(sequence, *command.empty, idle));
                else
                    return false;
            },
            node.operation);
    }
};

}

