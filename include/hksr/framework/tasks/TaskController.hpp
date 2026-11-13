#pragma once
#include <hksr/framework/tasks/TaskCatalog.hpp>
#include <hksr/framework/tasks/TaskPresentation.hpp>
#include <hksr/framework/tasks/TaskControlTypes.hpp>
#include <hksr/framework/presentation/scene/VisualStepResult.hpp>
#include <hksr/framework/presentation/reactions/ReactionPolicy.hpp>

namespace hksr::framework {
// One already-prepared actor/domain. Call after the owned animation players'
// frame service. Model/domain changes close this controller before rebinding.
class TaskController {
  public:
    TaskController(std::uint64_t owner_generation, std::shared_ptr<const TaskCatalog>,
                   std::shared_ptr<VisualProgramPlayer>, TaskPresentationServices);
    ~TaskController(); // no engine calls
    TaskSelectionResult Query(const TaskSelection&) const;
    TaskSelectionResult Select(TaskSelection); // data-only, no native submission
    void PrepareState();                       // local defaults, after any queued visual restore
    // Trusted layout restoration after model/visual restoration and state
    // initialization. Internal checkpoints remain inaccessible to Select.
    TaskSelectionResult RestoreSelection(TaskSelection);
    void RequestFinish();
    void Cancel();
    void ReleasePlayback();     // keep persistent inputs; Service until Drained before handing off
    void Reset(TaskExitReason); // release this domain's tasks/contexts; reusable
    void Close(TaskExitReason); // domain/world/actor loss; also ends visual contexts
    void Service(double seconds, std::uint64_t frame, bool allow_start = true, bool allow_inputs = true,
                 double playback_rate = 1);
    TaskContext& Context();
    const TaskContext& Context() const;
    const TaskControllerStatus& Status() const;
    // Includes receiver contributions admitted by another actor this frame.
    HitControlInputs ReactionState() const;
    void ApplyPlaybackEffect(const AcceptedPlaybackEffect&);
    void RestoreManualReaction(std::optional<TaskVisualReaction>);
    std::optional<TaskVisualReaction> ManualVisualReaction() const;
    std::optional<TaskSelection> Selection() const;
    VisualStepResult RestoreInputsReady(const TaskSelection&) const;
    std::vector<TaskRequestSnapshot> Requests() const;
    bool Drained() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
}
