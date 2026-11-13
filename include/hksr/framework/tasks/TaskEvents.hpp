#pragma once
#include <hksr/framework/tasks/TaskLifetimeTypes.hpp>
#include <functional>
#include <memory>
#include <string_view>

namespace hksr::framework {
// Authored visual notifications, not native battle event IDs. Producers must
// select the boundary explicitly; animation completion does not produce one.
enum class TaskEventKind {
    AfterHit,
    AfterHitAll,
    AfterHitMiss,
    AfterAttackedAdv,
    AfterAttack,
    AfterAttacked,
    AfterAttackEnd,
    AfterAttackedEnd,
    AfterDamageChunk,
    AfterBeingDamageChunk,
    BeingLimbo,
    BeforeDying
};
TaskEventKind ParseTaskEvent(std::string_view);
std::string_view TaskEventName(TaskEventKind);
struct TaskEventCommand {
    TaskEventKind kind = TaskEventKind::AfterHitAll;
    std::string recipient, source = "Caster";
    bool allow_position = false;
};
struct TaskEventInput {
    TaskEventKind kind;
    std::uint64_t recipient = 0, source = 0;
    // Captured endpoint epochs. A queued notification never follows a rebuilt
    // actor or a new action domain even when its stable character ID is reused.
    std::function<bool()> current;
};
struct TaskEventSubscription {
    std::function<std::shared_ptr<const TaskLifetimeReceipt>(const TaskEventInput&)> receive;
};
struct TaskEventReceipt {
    std::vector<std::shared_ptr<const TaskLifetimeReceipt>> callbacks;
    TaskRequestState State() const;
};
enum class TaskAttackBoundary { Record, Finish, Complete, BeginChunk, EndChunk };
struct TaskAttackCommand {
    TaskAttackBoundary boundary = TaskAttackBoundary::Finish;
    std::string target; // Record only; a position starts an attack without a receiver
    bool allow_position = false;
};
// One authored attack history, shared by ability calls within a presentation.
// No destructor/animation-completion notifications. Interrupted histories die
// with their presentation; callers explicitly author settlement boundaries.
class TaskAttackHistory {
  public:
    using Emit = std::function<std::shared_ptr<const TaskEventReceipt>(TaskEventKind)>;
    void Record(std::uint64_t receiver, Emit);
    std::shared_ptr<const TaskEventReceipt> Boundary(TaskAttackBoundary, Emit sender);

  private:
    enum class Phase { Idle, Active, Finished };
    struct Target {
        std::uint64_t id;
        Emit emit;
    };
    Phase phase_ = Phase::Idle;
    bool chunk_ = false;
    std::vector<Target> receivers_, chunk_receivers_;
};
}
