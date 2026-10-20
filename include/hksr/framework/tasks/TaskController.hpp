#pragma once
#include "TaskCatalog.hpp"
#include "TaskPresentation.hpp"

namespace hksr::framework {
struct TaskSelection {
    TaskKey task;
    TaskQuery query;
    VisualProgramParameters visual;
};
struct TaskSelectionResult {
    std::uint64_t intent=0;
    TaskKey resolved;
    TaskQueryResult query;
};
struct TaskControllerStatus {
    std::uint64_t requested=0,started=0;
    std::optional<TaskKey> current,pending;
    std::optional<TaskExecutionStatus> execution;
    std::optional<TaskQueryResult> blocked;
    std::size_t retiring=0;
    bool closing=false;
    std::string error;
};
// One already-prepared actor/domain. Call after the owned animation players'
// frame service. Model/domain changes close this controller before rebinding.
class TaskController {
public:
    TaskController(std::uint64_t owner_generation,std::shared_ptr<const TaskCatalog>,
        std::shared_ptr<VisualProgramPlayer>,TaskPresentationServices);
    ~TaskController(); // no engine calls
    TaskSelectionResult Query(const TaskSelection&) const;
    TaskSelectionResult Select(TaskSelection); // data-only, no native submission
    void RequestFinish();
    void Cancel();
    void Reset(TaskExitReason); // release this domain's tasks/contexts; reusable
    void Close(TaskExitReason); // domain/world/actor loss; also ends visual contexts
    void Service(double seconds,std::uint64_t frame);
    TaskContext& Context();
    const TaskContext& Context() const;
    const TaskControllerStatus& Status() const;
    std::vector<TaskRequestSnapshot> Requests() const;
    bool Drained() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
enum class TaskControlOperation { Query,Select,Finish,Cancel };
struct TaskControlInput {
    TaskControlOperation operation=TaskControlOperation::Query;
    std::optional<TaskSelection> selection;
};
struct TaskControlResult {
    std::optional<TaskSelectionResult> selection;
    TaskControllerStatus status;
    std::vector<TaskContextView> contexts;
    std::map<std::string,TaskQueryResult> catalog; // populated by query without a selection
    std::vector<TaskRequestSnapshot> requests; // active/retiring executions, query only
};
}
