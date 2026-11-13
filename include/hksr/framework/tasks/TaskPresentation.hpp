#pragma once
#include <hksr/framework/tasks/TaskSequence.hpp>
#include <hksr/presentation/audio/OwnedSoundRequest.hpp>

namespace hksr::framework {
using TaskAnimationEndpoint = AnimationRequestEndpoint;
class VisualProgramPlayer;
struct TaskSoundEndpoint {
    std::uint64_t entity_generation = 0;
    std::function<void()> service, cancel;
    std::function<api::SoundRequestStatus()> status;
};
struct TaskPresentationServices {
    // Empty while the current receiver configuration is loading. An empty
    // body/expression in a returned value leaves the authored fallback intact.
    // A pose delegates the complete body/expression to the existing resolver.
    std::function<std::optional<CurrentIdleResult>()> current_idle;
    // Empty means a declared owned participant is still being constructed.
    // Throw for unknown participants. First resolution is retained per request.
    std::function<std::optional<TaskAnimationEndpoint>(const TaskParticipant&)> animation;
    // Accepted body/pose effects, excluding current-idle resumption. Logical
    // reaction state belongs to the controller, not this request adapter.
    std::function<void(const AcceptedPlaybackEffect&)> accepted_playback;
    std::function<TaskAvailability(const TaskRequirement&)> condition;
    std::function<bool()> yield;
    // Empty while an owned participant's model is being prepared.
    std::function<std::optional<TaskSoundEndpoint>(const TaskSoundCommand&)> sound;
    std::optional<PoseRequestEndpoint> pose;
    std::function<std::shared_ptr<const TaskLifetimeReceipt>(const TaskLifetimeCommand&,
                                                             const VisualProgramParameters&)>
        lifetime;
    std::function<std::shared_ptr<const TaskEventReceipt>(const TaskEventCommand&, const VisualProgramParameters&)>
        event;
    // Null only while the owned model is being prepared. Any returned result
    // is terminal for this command; parameters do not acquire playback/rollback.
    std::function<std::optional<api::AnimationParameterResult>(const TaskParticipant&, const api::AnimationParameter&,
                                                               api::AnimationParameterPropagation)>
        parameter;
};
// One task execution over an existing visual scene and owned animation players.
// Service those animation players on their normal owner frame, then call
// Service here. The visual scene can outlive this task through named contexts.
class TaskPresentation {
  public:
    TaskPresentation(TaskExecutionToken, std::uint64_t owner_generation, std::shared_ptr<const TaskSequence>,
                     std::shared_ptr<VisualProgramPlayer>, VisualProgramParameters, TaskPresentationServices,
                     bool captured = false, std::uint64_t context = 0, bool owns_playback = true,
                     bool needs_movement_origin = false);
    ~TaskPresentation(); // data destruction only; call Stop/Service to drain first
    void Service(double seconds, std::uint64_t frame, double playback_rate = 1);
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
