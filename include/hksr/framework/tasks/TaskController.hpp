#pragma once
#include "TaskCatalog.hpp"
#include "TaskPresentation.hpp"
#include "TaskControlTypes.hpp"

namespace hksr::framework {
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
    void Service(double seconds,std::uint64_t frame,bool allow_start=true);
    TaskContext& Context();
    const TaskContext& Context() const;
    const TaskControllerStatus& Status() const;
    std::optional<TaskSelection> Selection() const;
    VisualStepResult RestoreInputsReady(const TaskSelection&) const;
    std::vector<TaskRequestSnapshot> Requests() const;
    bool Drained() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
