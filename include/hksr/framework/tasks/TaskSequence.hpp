#pragma once
#include "TaskModel.hpp"
#include <hksr/framework/presentation/VisualProgram.hpp>
#include <functional>
#include <memory>

namespace hksr::framework {
using TaskNodeIndex=std::size_t;
struct TaskAnimationCommand {TaskParticipant participant;std::string state;int layer=0;float transition=0;};
struct TaskSoundCommand {TaskParticipant participant;std::string event;};
using TaskLeafOperation=std::variant<TaskAnimationCommand,VisualInstruction,TaskSoundCommand>;
struct TaskSubmit {std::string request;TaskLeafOperation operation;};
enum class TaskWaitSignal { Submitted,RequestComplete,PerformanceComplete,Progress,Drained,Started };
struct TaskWaitRequest {std::string request;TaskWaitSignal signal=TaskWaitSignal::PerformanceComplete;double progress=1;};
struct TaskWaitSeconds {double seconds=0;};
struct TaskWaitFrame {};
struct TaskSerial {std::vector<TaskNodeIndex> children;};
struct TaskParallel {std::vector<TaskNodeIndex> children;};
struct TaskRepeat {TaskNodeIndex child=0;std::optional<std::uint64_t> count;bool until_finish=false;};
struct TaskBranch {TaskRequirement condition;TaskNodeIndex yes=0,no=0;};
using TaskNodeOperation=std::variant<TaskSubmit,TaskWaitRequest,TaskWaitSeconds,TaskWaitFrame,TaskSerial,TaskParallel,TaskRepeat,TaskBranch>;
struct TaskNode {std::string source;TaskNodeOperation operation;};
struct TaskSequence {
    std::vector<TaskNode> nodes;
    TaskNodeIndex entry=0;
    std::optional<TaskNodeIndex> interruption;
};
struct TaskSubmission {TaskRequestReceipt receipt;TaskRequestState state;};
struct TaskSequenceServices {
    // Resolve participants through the owned actor/partner bindings. Return
    // reserved receipts for queued or partially failed work. A queued model
    // request may have model=0/submitted=false. Include synchronous observations
    // in the returned state before later Observe calls are possible.
    std::function<TaskSubmission(TaskExecutionToken,std::uint64_t,const TaskSubmit&)> submit;
    // At most one normal finish and one cancellation per receipt. A later
    // deletion may cancel a naturally finishing tail. Existing resource owners
    // perform the actual release and retain in-flight callbacks until drained.
    std::function<void(const TaskRequestReceipt&,TaskExitReason)> release;
    std::function<TaskAvailability(const TaskRequirement&)> condition;
    // Cooperative work budget. Yield retains the cursor; it never drops nodes.
    std::function<bool()> yield;
};
// One execution, not one actor. The coordinator retains interrupted instances
// until Drained, so old native callbacks cannot advance a newer execution.
// All methods and service callbacks run on the owning engine thread.
class TaskSequenceExecution {
public:
    TaskSequenceExecution(TaskExecutionToken,std::shared_ptr<const TaskSequence>,TaskSequenceServices);
    ~TaskSequenceExecution(); // no engine calls
    TaskSequenceExecution(const TaskSequenceExecution&)=delete;
    TaskSequenceExecution& operator=(const TaskSequenceExecution&)=delete;
    void Advance(double seconds,std::uint64_t frame);
    bool Observe(const TaskRequestReceipt&,const TaskRequestState&);
    void RequestFinish(); // lets an until_finish loop enter its normal successor
    void Stop(TaskExitReason); // actor/world loss skips authored interruption
    void Fail(std::string); // scoped provider failure, including before first submit
    const TaskExecutionStatus& Status() const;
    std::vector<TaskRequestSnapshot> Requests() const; // explicit diagnostic read, no native calls
    bool Drained() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
void ValidateTaskSequence(const TaskSequence&);
}
