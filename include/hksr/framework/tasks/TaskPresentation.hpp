#pragma once
#include "TaskSequence.hpp"
#include <hksr/framework/presentation/ActionPlayback.hpp>
#include <hksr/presentation/audio/OwnedSoundRequest.hpp>

namespace hksr::framework {
using TaskAnimationEndpoint=AnimationRequestEndpoint;
// The bound ActionPlayback must outlive the task's drain. This never resolves
// official actors from a global name, target selector or battle registry.
TaskAnimationEndpoint BindTaskAnimation(ActionPlayback&);
PoseRequestEndpoint BindTaskPose(ActionPlayback&,PoseResolver);
struct TaskSoundEndpoint {
    std::uint64_t entity_generation=0;
    std::function<void()> service,cancel;
    std::function<api::SoundRequestStatus()> status;
};
TaskSoundEndpoint BindTaskSound(std::uint64_t entity_generation,std::unique_ptr<api::OwnedSoundRequest>);
struct TaskPresentationServices {
    // Empty means a declared owned participant is still being constructed.
    // Throw for unknown participants. First resolution is retained per request.
    std::function<std::optional<TaskAnimationEndpoint>(const TaskParticipant&)> animation;
    std::function<TaskAvailability(const TaskRequirement&)> condition;
    std::function<bool()> yield;
    // Empty while an owned participant's model is being prepared.
    std::function<std::optional<TaskSoundEndpoint>(const TaskSoundCommand&)> sound;
    // Synchronous, value-only changes on this execution's accepted control
    // ticket. No native battle entity, task factory or event broadcast.
    std::function<void(const std::vector<ControlChange>&)> control;
    std::optional<PoseRequestEndpoint> pose;
};
// One task execution over an existing visual scene and owned animation players.
// Service those animation players on their normal owner frame, then call
// Service here. The visual scene can outlive this task through named contexts.
class TaskPresentation {
public:
    TaskPresentation(TaskExecutionToken,std::uint64_t owner_generation,std::shared_ptr<const TaskSequence>,
        std::shared_ptr<VisualProgramPlayer>,VisualProgramParameters,TaskPresentationServices,bool captured=false);
    ~TaskPresentation(); // data destruction only; call Stop/Service to drain first
    void Service(double seconds,std::uint64_t frame);
    void Stop(TaskExitReason);
    void RequestFinish();
    const TaskExecutionStatus& Status() const;
    std::vector<TaskRequestSnapshot> Requests() const;
    bool Drained() const;
    bool Started() const;
    bool YieldedControl() const; // no pending producer work, tails may remain
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
