#pragma once
#include <hksr/framework/tasks/TaskModel.hpp>

namespace hksr::framework {
enum class TaskLifetimeOperation { Enter, Signal, Exit, ChangeValue };
struct TaskContextNumberReference {
    std::string context, name;
};
struct TaskContextValueSet {
    std::string name;
    TaskValue value;
};
struct TaskLifetimeCommand {
    std::string context, event;
    TaskLifetimeOperation operation = TaskLifetimeOperation::Enter;
    bool all = false;
    std::optional<bool> only_caster;
    std::optional<ContextNumberChange> change;
    std::optional<TaskContextNumberReference> value_from;
    std::optional<TaskContextValueSet> set_value;
    std::optional<std::string> value_input; // resolved from this entry's captured typed inputs before dispatch
};
// Authored visual projection, not the game's gameplay stacking enum. Catalog
// and context identify the definition; these fields select running instances.
enum class TaskInstanceMatch { Receiver, ReceiverCaster, ReceiverCasterAbility };
enum class TaskInstanceRepeat { Keep, ReplaceInputs, NewInstance };
struct TaskInstancePolicy {
    std::string name, category = "Default", receiver = "Caster", caster = "Caster", ability;
    TaskInstanceMatch match = TaskInstanceMatch::Receiver;
    TaskInstanceRepeat repeat = TaskInstanceRepeat::Keep;
};
// The receipt describes one operation, not ownership of the persistent state.
// Cancelling its caller does not undo an already admitted enter/exit.
struct TaskLifetimeReceipt {
    TaskRequestState state;
    std::uint64_t generation = 0;
};
struct TaskLifetimeCallbacks {
    std::string enter, exit;
    std::map<std::string, std::string> signals;
    std::string reapply;
    bool activate_before_callbacks = false;
    std::string restore;
};
}
