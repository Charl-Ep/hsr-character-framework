#pragma once
#include <hksr/framework/tasks/TaskAnimationParameter.hpp>
#include <hksr/framework/tasks/TaskModel.hpp>
#include <hksr/framework/tasks/TaskLifetimeTypes.hpp>
#include <hksr/framework/tasks/TaskEvents.hpp>
#include <hksr/framework/presentation/scene/VisualInstruction.hpp>
#include <hksr/framework/presentation/movement/VisualMovement.hpp>
#include <hksr/framework/presentation/projectiles/VisualProjectiles.hpp>
#include <hksr/framework/presentation/reactions/HitInbox.hpp>
#include <hksr/framework/presentation/playback/PoseRequests.hpp>
#include <functional>
#include <memory>

namespace hksr::framework {
using TaskNodeIndex = std::size_t;
struct TaskAnimationCommand {
    TaskParticipant participant{"body"};
    std::string state;
    int layer = 0;
    float transition = 0;
    float normalized_start = 0;
    std::optional<float> normalized_end;
    std::optional<AnimationTrack> track;
    bool restart = true;
    bool current_idle = false;
};
struct TaskSoundCommand {
    TaskParticipant participant{"body"};
    std::string event;
};
struct TaskMovementCommand {
    std::string animation;
    VisualMovement movement;
}; // state comes from this request
struct TaskPoseCommand {
    PoseSelection selection;
}; // always this controller's owned body
struct TaskHitCommand {
    std::string target;
    HitInput hit;
    bool allow_position = false;
    std::vector<TaskEventKind> events; // authored hit-result projection, never inferred from body completion
};
struct TaskExpressionCommand {
    std::string state;
    float transition = .2f;
    bool current_idle = false;
};
using TaskLeafOperation =
    std::variant<TaskAnimationCommand, VisualInstruction, TaskSoundCommand, TaskMovementCommand, VisualProjectilePlan,
                 TaskPoseCommand, TaskHitCommand, TaskExpressionCommand, TaskLifetimeCommand, TaskEventCommand,
                 TaskAttackCommand, TaskAnimationParameterCommand>;
bool RequiresBodyPlayback(const TaskLeafOperation&);
struct TaskSubmit {
    std::string request;
    TaskLeafOperation operation;
    VisualTargetAliases target_bindings;
};
enum class TaskWaitSignal {
    Submitted,
    RequestComplete,
    PerformanceComplete,
    Progress,
    Drained,
    Started,
    AnimationEnd,
    StateLeft,
    AnimationFinished
};
struct TaskWaitRequest {
    std::string request;
    TaskWaitSignal signal = TaskWaitSignal::PerformanceComplete;
    double progress = 1;
};
struct TaskWaitSeconds {
    double seconds = 0;
};
struct TaskWaitFrame {};
struct TaskSerial {
    std::vector<TaskNodeIndex> children;
};
struct TaskParallel {
    std::vector<TaskNodeIndex> children;
};
// Invokes a maintained visual ability without joining its body. Calls of an
// already-running identity are ignored; a ready identity can be invoked again.
// Registration survives re-entry within this execution, independently of
// resource drain. This does not construct a native battle ability.
struct TaskAbilityCall {
    std::string ability;
    TaskNodeIndex child = 0;
    bool wait_for_performance = false;
};
struct TaskWaitAbilities {};
struct TaskRepeat {
    TaskNodeIndex child = 0;
    std::optional<std::uint64_t> count;
    bool until_finish = false;
};
struct TaskBranch {
    TaskRequirement condition;
    TaskNodeIndex yes = 0, no = 0;
};
// Native success continuations do not run after a skipped/unchanged producer.
struct TaskOnSuccess {
    std::string request;
    TaskNodeIndex child = 0;
};
// Explicitly selected named inputs, not a battle selector. Each member has a
// private cursor/receipt namespace and inherited bindings; empty uses fallback.
struct TaskForTargets {
    std::string bind;
    std::vector<std::string> targets;
    TaskNodeIndex child = 0;
    std::optional<TaskNodeIndex> empty;
    std::string collection; // supplied collection overrides targets, including an empty collection
};
struct TaskOmitted {
    std::string request, reason, unit;
    std::vector<std::string> dependencies; // retained source metadata, not inferred execution edges
};
using TaskNodeOperation =
    std::variant<TaskSubmit, TaskWaitRequest, TaskWaitSeconds, TaskWaitFrame, TaskSerial, TaskParallel, TaskAbilityCall,
                 TaskWaitAbilities, TaskRepeat, TaskBranch, TaskForTargets, TaskOmitted, TaskOnSuccess>;
struct TaskNode {
    std::string source;
    TaskNodeOperation operation;
};
struct TaskSequence;
struct TaskSequenceInput {
    std::vector<TaskNode> nodes;
    TaskNodeIndex entry = 0;
    std::optional<TaskNodeIndex> interruption;
    TaskSequenceInput() = default;
    TaskSequenceInput(const TaskSequence&);
};
// A detached, validated plan. Public input remains editable until construction;
// playback retains this immutable value and never revalidates its graph.
struct TaskSequence {
    explicit TaskSequence(const TaskSequenceInput&);
    const std::vector<TaskNode> nodes;
    const TaskNodeIndex entry;
    const std::optional<TaskNodeIndex> interruption;
    bool NeedsMovementOrigin() const {
        return needs_movement_origin_;
    }
  private:
    bool needs_movement_origin_ = false;
};
struct TaskSubmission {
    TaskRequestReceipt receipt;
    TaskRequestState state;
};
struct TaskSequenceServices {
    // Resolve participants through the owned actor/partner bindings. Return
    // reserved receipts for queued or partially failed work. A queued model
    // request may have model=0/submitted=false. Include synchronous observations
    // in the returned state before later Observe calls are possible.
    std::function<TaskSubmission(TaskExecutionToken, std::uint64_t, const TaskSubmit&)> submit;
    // At most one normal finish and one cancellation per receipt. A later
    // deletion may cancel a naturally finishing tail. Existing resource owners
    // perform the actual release and retain in-flight callbacks until drained.
    std::function<void(const TaskRequestReceipt&, TaskExitReason)> release;
    std::function<TaskAvailability(const TaskRequirement&)> condition;
    // Cooperative work budget. Yield retains the cursor; it never drops nodes.
    std::function<bool()> yield;
    std::function<std::optional<std::vector<std::string>>(const std::string&)> targets;
};
// One execution, not one actor. The coordinator retains interrupted instances
// until Drained, so old native callbacks cannot advance a newer execution.
// All methods and service callbacks run on the owning engine thread.
class TaskSequenceExecution {
  public:
    TaskSequenceExecution(TaskExecutionToken, std::shared_ptr<const TaskSequence>, TaskSequenceServices);
    ~TaskSequenceExecution(); // no engine calls
    TaskSequenceExecution(const TaskSequenceExecution&) = delete;
    TaskSequenceExecution& operator=(const TaskSequenceExecution&) = delete;
    void Advance(double seconds, std::uint64_t frame, double playback_rate = 1);
    bool Observe(const TaskRequestReceipt&, const TaskRequestState&);
    void RequestFinish();      // lets an until_finish loop enter its normal successor
    void Stop(TaskExitReason); // actor/world loss skips authored interruption
    void Fail(std::string);    // scoped provider failure, including before first submit
    const TaskExecutionStatus& Status() const;
    std::vector<TaskRequestSnapshot> Requests() const; // explicit diagnostic read, no native calls
    bool Drained() const;
    bool ProducersComplete() const; // root and all invoked visual ability bodies
  private:
    struct State;
    std::unique_ptr<State> state_;
};
void ValidateTaskSequence(const TaskSequence&);
void ValidateTaskSequence(const TaskSequenceInput&);
}
