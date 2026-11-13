#include <hksr/framework/tasks/TaskPresentation.hpp>
#include <hksr/framework/presentation/scene/VisualProgramPlayer.hpp>
#include <stdexcept>
#include <set>

namespace hksr::framework {
namespace {
TaskLeafOperation BindTargets(TaskLeafOperation operation, const VisualTargetAliases& bindings) {
    const auto target = [&](std::string& name) { name = BindVisualTarget(name, bindings); };
    if (auto* hit = std::get_if<TaskHitCommand>(&operation))
        target(hit->target);
    if (auto* event = std::get_if<TaskEventCommand>(&operation)) {
        target(event->recipient);
        target(event->source);
    }
    if (auto* attack = std::get_if<TaskAttackCommand>(&operation);
        attack && attack->boundary == TaskAttackBoundary::Record)
        target(attack->target);
    if (auto* motion = std::get_if<TaskMovementCommand>(&operation))
        for (auto& segment : motion->movement.segments)
            if (segment.destination == MovementDestination::Target)
                target(segment.name);
    if (auto* projectiles = std::get_if<VisualProjectilePlan>(&operation))
        for (auto& shot : projectiles->shots)
            target(shot.target);
    if (auto* instruction = std::get_if<VisualInstruction>(&operation)) {
        instruction->target_bindings = bindings;
        if (auto* face = std::get_if<VisualFace>(&instruction->operation))
            target(face->target);
        if (auto* follow = std::get_if<VisualFollow>(&instruction->operation))
            for (auto& name : follow->targets)
                target(name);
        if (auto* model = std::get_if<VisualModelOperation>(&instruction->operation)) {
            if (auto* follow = std::get_if<VisualModelScaleFollow>(&model->command); follow && !follow->target.empty())
                target(follow->target);
            if (auto* scale = std::get_if<VisualCharacterScale>(&model->command))
                for (auto& name : scale->targets)
                    target(name);
        }
        if (auto* clock = std::get_if<VisualActorTimeScale>(&instruction->operation))
            target(clock->recipient);
        if (auto* rendering = std::get_if<VisualRenderingOperation>(&instruction->operation))
            target(rendering->recipient);
        if (auto* partner = std::get_if<VisualPartnerOperation>(&instruction->operation))
            std::visit(
                [&](auto& command) {
                    using T = std::decay_t<decltype(command)>;
                    if constexpr (std::is_same_v<T, VisualPartnerAttach> ||
                                  std::is_same_v<T, VisualPartnerAnimationFollow> ||
                                  std::is_same_v<T, VisualPartnerVisibilityFollow>)
                        target(command.target);
                },
                partner->command);
    }
    return operation;
}
}
struct TaskPresentation::State {
    struct Request {
        TaskSubmission value;
        std::string name;
        VisualTargetAliases target_bindings;
        TaskLeafOperation operation;
        std::shared_ptr<Request> animation_dependency;
        std::optional<TaskAnimationEndpoint> endpoint;
        std::optional<PoseRequestEndpoint> pose_endpoint;
        std::optional<HitDelivery> hit;
        std::optional<SpatialHitEndpoint> point_hit;
        std::shared_ptr<const AnimationRequestStatus> animation;
        std::shared_ptr<const VisualInstructionStatus> visual;
        std::optional<TaskSoundEndpoint> sound;
        std::optional<MovementRequestEndpoint> movement;
        std::optional<ProjectileRequestEndpoint> projectiles;
        std::shared_ptr<const TaskLifetimeReceipt> lifetime;
        std::shared_ptr<const TaskEventReceipt> event;
        std::optional<api::AnimationParameterResult> parameter;
        bool hit_events_sent = false;
        bool released = false, finishing = false, submitting = false;
        bool delegated_expression = false;
        bool accepted_effect_notified = false;
    };
    TaskExecutionToken token;
    std::uint64_t owner = 0, scope = 0;
    std::shared_ptr<VisualProgramPlayer> visual;
    TaskPresentationServices services;
    std::unique_ptr<TaskSequenceExecution> execution;
    std::map<std::uint64_t, std::shared_ptr<Request>> requests;
    TaskAttackHistory attacks;
    bool servicing = false, forgotten = false, cancel_scope = false, started = false;
    bool owns_playback = true;
    std::shared_ptr<const TaskEventReceipt> Attack(const TaskAttackCommand& command,
                                                   const VisualProgramParameters& inputs) {
        const auto port = services.event;
        const auto emit = [port, inputs](const std::string& target) -> TaskAttackHistory::Emit {
            return [port, inputs, target](TaskEventKind kind) {
                if (!port)
                    throw std::runtime_error("attack event service unavailable");
                return port({kind, target, "Caster", false}, inputs);
            };
        };
        if (command.boundary != TaskAttackBoundary::Record)
            return attacks.Boundary(command.boundary, emit("Caster"));
        auto target = command.target;
        std::set<std::string> visited;
        while (inputs.target_aliases.count(target)) {
            if (!visited.insert(target).second)
                throw std::invalid_argument("cyclic attack target alias");
            target = inputs.target_aliases.at(target);
        }
        std::uint64_t id = 0;
        if (target == "Caster")
            id = visual->Character();
        else if (const auto found = inputs.targets.find(target); found != inputs.targets.end() && found->second)
            id = found->second;
        else if (!command.allow_position || !inputs.position_targets.count(target))
            throw std::invalid_argument("attack record requires an owned target or position: " + target);
        attacks.Record(id, emit(target));
        return std::make_shared<TaskEventReceipt>();
    }
    TaskSubmission Submit(TaskExecutionToken actual, std::uint64_t serial, const TaskSubmit& input) {
        if (!owns_playback && RequiresBodyPlayback(input.operation))
            throw std::logic_error("body playback requires a presentation task, not an independent input");
        auto request = std::make_shared<Request>();
        request->operation = BindTargets(input.operation, input.target_bindings);
        request->name = input.request;
        request->target_bindings = input.target_bindings;
        const auto* animation = std::get_if<TaskAnimationCommand>(&input.operation);
        const auto* sound = std::get_if<TaskSoundCommand>(&input.operation);
        const auto* parameter = std::get_if<TaskAnimationParameterCommand>(&input.operation);
        const auto* movement = std::get_if<TaskMovementCommand>(&input.operation);
        const auto* hit = std::get_if<TaskHitCommand>(&request->operation);
        const bool pose = std::holds_alternative<TaskPoseCommand>(input.operation);
        const bool expression = std::holds_alternative<TaskExpressionCommand>(input.operation);
        request->value.receipt = {actual, serial, 0,
                                  animation   ? animation->participant
                                  : sound     ? sound->participant
                                  : parameter ? parameter->participant
                                              : TaskParticipant{movement || pose || expression ? "body" : "visual"},
                                  parameter                                              ? owner
                                  : animation || sound || movement || pose || expression ? 0
                                                                                         : owner};
        if (hit) {
            request->value.receipt.participant = {"target:" + hit->target};
            request->value.receipt.entity_generation = 0;
        }
        if (movement && !movement->movement.instant) {
            for (const auto& [id, prior] : requests)
                if (id < serial && prior->name == movement->animation)
                    request->animation_dependency = prior;
            if (!request->animation_dependency)
                request->value.state.error = "movement animation request has not been submitted";
        }
        requests.emplace(serial, request);
        return request->value;
    }
    void Release(const TaskRequestReceipt& receipt, TaskExitReason reason) {
        const auto found = requests.find(receipt.request);
        if (found == requests.end())
            return;
        const auto request = found->second;
        if (request->released)
            return;
        if (request->delegated_expression) {
            request->released = request->value.state.drained = true;
            return;
        }
        if (std::holds_alternative<TaskAnimationParameterCommand>(request->operation)) {
            if (reason == TaskExitReason::Completed && !request->parameter && request->value.state.error.empty()) {
                request->finishing = true;
                return;
            }
            // A setter has no rollback lease. Cancellation before resolution
            // prevents delivery; a call already in flight finishes once.
            request->released = true;
            request->value.state.drained = !request->submitting;
            return;
        }
        if (std::holds_alternative<TaskLifetimeCommand>(request->operation) ||
            std::holds_alternative<TaskEventCommand>(request->operation) ||
            std::holds_alternative<TaskAttackCommand>(request->operation)) {
            const auto state = request->event      ? request->event->State()
                               : request->lifetime ? request->lifetime->state
                                                   : TaskRequestState{};
            if (reason == TaskExitReason::Completed && !state.performance_complete && !state.drained) {
                request->finishing = true;
                return;
            }
            request->released = true;
            request->value.state.drained = true;
            return;
        }
        if (std::holds_alternative<TaskExpressionCommand>(request->operation)) {
            if (reason == TaskExitReason::Completed &&
                (!request->animation || (!request->animation->complete && !request->animation->skipped))) {
                request->finishing = true;
                return;
            }
            request->released = true;
            if (request->animation)
                request->endpoint->release_expression(request->animation->serial);
            request->value.state.drained = true;
            return;
        }
        if (std::holds_alternative<TaskHitCommand>(request->operation)) {
            if (reason == TaskExitReason::Completed &&
                (request->point_hit ? !request->point_hit->status->closed : !request->value.state.request_complete)) {
                request->finishing = true;
                return;
            }
            request->released = true;
            if (request->hit)
                request->hit->cancel(); // delivered hits remain receiver-owned
            if (request->point_hit && reason != TaskExitReason::Completed)
                request->point_hit->cancel();
            request->value.state.drained =
                request->point_hit ? request->point_hit->status->closed : !request->submitting;
            return;
        }
        if (std::holds_alternative<VisualProjectilePlan>(request->operation)) {
            if (reason == TaskExitReason::Completed &&
                (!request->projectiles || !request->projectiles->status().closed)) {
                request->finishing = true;
                return;
            }
            request->released = true;
            if (request->projectiles) {
                if (reason != TaskExitReason::Completed)
                    request->projectiles->cancel();
                request->value.state.drained = request->projectiles->status().closed;
            } else
                request->value.state.drained = !request->submitting;
            return;
        }
        if (reason == TaskExitReason::Completed && std::holds_alternative<TaskAnimationCommand>(request->operation)) {
            for (const auto& [id, dependent] : requests)
                if (dependent->animation_dependency == request && !dependent->value.state.drained) {
                    request->finishing = true;
                    return; // retain the bound animation through motion drain
                }
            const auto& animation = std::get<TaskAnimationCommand>(request->operation);
            if (animation.track)
                for (const auto& [id, dependent] : requests)
                    if (const auto* next = std::get_if<TaskAnimationCommand>(&dependent->operation);
                        next && next->track && next->track->previous_end &&
                        next->participant.name == animation.participant.name && next->layer == animation.layer &&
                        !dependent->released && !dependent->value.state.request_complete) {
                        request->finishing = true;
                        return;
                    }
        }
        if (std::holds_alternative<TaskMovementCommand>(request->operation)) {
            if (reason == TaskExitReason::Completed && (!request->movement || !request->movement->status().closed)) {
                request->finishing = true;
                return;
            }
            request->released = true;
            if (reason == TaskExitReason::Replaced || reason == TaskExitReason::Interrupted)
                request->value.state.superseded = !request->value.state.performance_complete;
            if (request->movement) {
                if (reason != TaskExitReason::Completed)
                    request->movement->cancel();
                request->value.state.drained = request->movement->status().closed;
            } else
                request->value.state.drained = !request->submitting;
            return;
        }
        if (std::holds_alternative<TaskSoundCommand>(request->operation)) {
            if (reason == TaskExitReason::Completed && (!request->sound || !request->sound->status().closed)) {
                request->finishing = true;
                return; // natural audio tail, not queue completion
            }
            request->released = true;
            if (request->sound) {
                if (reason != TaskExitReason::Completed)
                    request->sound->cancel();
                request->value.state.drained = request->sound->status().closed;
            } else
                request->value.state.drained = !request->submitting;
            return;
        }
        if (reason == TaskExitReason::Completed && !request->value.state.request_complete &&
            !request->value.state.skipped) {
            request->finishing = true;
            return; // a queued command must still be delivered
        }
        request->released = true;
        if (request->animation) {
            if (request->pose_endpoint)
                request->pose_endpoint->release(request->animation->serial, reason != TaskExitReason::Completed &&
                                                                                reason != TaskExitReason::Replaced);
            else
                request->endpoint->release(request->animation->serial);
        }
        if (request->visual)
            visual->CancelInstruction(scope, request->visual);
        request->value.state.drained = true;
    }
    void Observe(const std::shared_ptr<Request>& request) {
        auto& value = request->value;
        if (request->delegated_expression) {
            value.state.request_complete = value.state.performance_complete = true;
            value.state.drained = request->released;
        } else if (request->parameter) {
            const auto& result = *request->parameter;
            value.receipt.model = static_cast<std::uint64_t>(result.model_identity);
            value.state.submitted = result.body_attempted && result.body.returned;
            value.state.request_complete = true;
            // Completion is for this synchronous parameter instruction, not a
            // clip possibly selected by it. No progress/end clock is invented.
            value.state.performance_complete =
                result.state == api::AnimationParameterState::Applied && result.body.returned;
            value.state.drained = true;
            if (result.state != api::AnimationParameterState::Applied || !result.body.returned)
                value.state.error = result.detail.empty() ? "animation parameter command failed" : result.detail;
            else if (!result.references_released)
                value.state.error = "animation parameter references await SDK cleanup";
        } else if (request->event && !request->released) {
            value.state = request->event->State();
        } else if (request->lifetime && !request->released) {
            value.state = request->lifetime->state;
        } else if (request->point_hit) {
            const auto& state = *request->point_hit->status;
            value.state.submitted = state.admitted;
            value.state.request_complete = state.admitted || state.cancelled || !state.error.empty();
            value.state.performance_complete = state.admitted;
            value.state.started = state.submitted;
            value.state.drained = state.closed;
            if (!request->released)
                value.state.error = state.error;
        } else if (request->hit) {
            const auto& state = *request->hit->status;
            value.receipt.entity_generation = state.entity_generation;
            value.state.submitted = state.delivered;
            value.state.request_complete = state.delivered || state.cancelled || !state.error.empty();
            // Admission and the receiving actor's performance are different
            // boundaries. Attack scripts can continue after RequestComplete;
            // an explicit performance wait must not replace a self-hit with
            // its idle successor before the receiving animation even starts.
            const auto& playback = state.playback;
            value.state.performance_complete =
                state.delivered &&
                (state.ignored || (playback && !playback->cancelled &&
                                   (playback->released || (playback->clip_complete && playback->expression_released))));
            value.state.started = state.delivered && (!state.ignored || !state.effects_ignored);
            value.state.drained = request->released;
            if (!state.error.empty())
                value.state.error = state.error;
            if (playback && !playback->error.empty())
                value.state.error = playback->error;
            if (!request->released) {
                if (state.cancelled && value.state.error.empty())
                    value.state.error = "hit delivery cancelled before admission";
                if (playback && playback->cancelled && value.state.error.empty())
                    value.state.error = "receiver hit playback cancelled";
                if (state.delivered && !state.cancelled && !request->hit_events_sent) {
                    request->hit_events_sent = true;
                    const auto& command = std::get<TaskHitCommand>(request->operation);
                    if (!command.events.empty()) {
                        try {
                            if (!services.event)
                                throw std::runtime_error("hit event service unavailable");
                            const auto inputs =
                                BindVisualParameters(visual->SequenceParameters(scope), request->target_bindings);
                            for (const auto event : command.events) {
                                try {
                                    const auto receipt =
                                        services.event({event, command.target, "Caster", false}, inputs);
                                    if (!receipt)
                                        throw std::runtime_error("hit event returned no receipt");
                                    const auto status = receipt->State();
                                    if (!status.error.empty())
                                        throw std::runtime_error(status.error);
                                } catch (const std::exception& error) {
                                    if (!value.state.error.empty())
                                        value.state.error += "; ";
                                    value.state.error += error.what();
                                }
                            }
                        } catch (const std::exception& error) {
                            if (value.state.error.empty())
                                value.state.error = error.what();
                        }
                    }
                }
            }
        } else if (request->animation && !value.state.superseded) {
            const auto& state = *request->animation;
            value.receipt.model = state.model_generation;
            value.state.skipped = state.skipped;
            value.state.skip_reason = state.skip_reason;
            value.state.submitted = state.accepted;
            value.state.request_complete =
                !state.skipped && (state.accepted || state.released || state.request_finished);
            value.state.performance_complete = state.complete;
            value.state.progress = state.progress;
            value.state.started = state.entered;
            value.state.animation_end = state.end_reached;
            value.state.state_left = state.state_left;
            value.state.animation_finished = state.request_finished;
            value.state.superseded = state.advanced;
            value.state.drained = state.released;
            if (!request->released)
                value.state.error = state.error;
        } else if (request->projectiles) {
            request->projectiles->service();
            const auto state = request->projectiles->status();
            value.state.submitted = state.started;
            value.state.request_complete = state.emitted;
            value.state.started = state.started;
            value.state.performance_complete = state.arrived && state.error.empty();
            value.state.drained = state.closed;
            if (!state.error.empty())
                value.state.error = state.error;
            else if (state.cancelled && !request->released)
                value.state.error = "projectile source retired";
        } else if (request->movement) {
            request->movement->service();
            const auto state = request->movement->status();
            const bool superseded = request->movement->superseded && request->movement->superseded();
            value.state.superseded = superseded;
            value.state.submitted = state.accepted;
            value.state.request_complete = state.accepted || (superseded && state.closed);
            value.state.started = state.started;
            value.state.performance_complete = (state.completed || (superseded && state.closed)) && state.error.empty();
            value.state.progress = state.movement_time;
            value.state.drained = state.closed;
            if (!state.error.empty())
                value.state.error = state.error;
            else if (state.cancelled && !request->released && !superseded)
                value.state.error = "movement source retired";
        } else if (request->sound) {
            request->sound->service();
            const auto state = request->sound->status();
            value.state.submitted = state.accepted;
            value.state.request_complete = state.accepted;
            value.state.started = state.started;
            value.state.performance_complete = state.ended && state.error.empty();
            value.state.drained = state.closed;
            if (!state.error.empty())
                value.state.error = state.error;
            else if (state.cancelled && !request->released)
                value.state.error = "sound source retired";
        } else if (request->visual) {
            const auto& state = *request->visual;
            value.state.skipped = state.skipped;
            value.state.skip_reason = state.skip_reason;
            value.state.submitted = state.complete && !state.skipped;
            value.state.request_complete = (state.complete || state.cancelled) && !state.skipped;
            value.state.performance_complete = state.complete && !state.skipped;
            value.state.started = state.complete && !state.skipped && state.issues.empty() && !state.cancelled;
            // Resources belong to the sequence scope, not to each instruction.
            // Keep that scope until its effects, partners and Timelines drain.
            value.state.drained = state.complete || state.cancelled;
            if (!state.issues.empty())
                value.state.error = state.issues.front();
            else if (state.cancelled && !request->released)
                value.state.error = "visual instruction cancelled";
        }
        if (request->released && !request->sound && !request->movement && !request->projectiles &&
            !request->point_hit && !request->submitting)
            value.state.drained = true;
        if (!request->released && !request->accepted_effect_notified && value.state.submitted &&
            value.state.error.empty()) {
            const auto* command = std::get_if<TaskAnimationCommand>(&request->operation);
            auto effect = request->animation && value.receipt.participant.name == "body" &&
                                  (!command || !command->current_idle)
                              ? request->animation->accepted_effect
                              : std::optional<AcceptedPlaybackEffect>{};
            if (std::holds_alternative<TaskMovementCommand>(request->operation))
                effect = NormalBodyAccepted{};
            if (effect) {
                request->accepted_effect_notified = true;
                if (services.accepted_playback)
                    services.accepted_playback(*effect);
            }
        }
        if (request->finishing &&
            ((request->lifetime || request->event) ? (value.state.performance_complete || value.state.drained)
             : std::holds_alternative<TaskExpressionCommand>(request->operation)
                 ? (value.state.performance_complete || value.state.skipped)
             : request->sound || request->movement || request->projectiles || request->point_hit
                 ? value.state.drained
                 : value.state.request_complete || value.state.skipped))
            Release(value.receipt, TaskExitReason::Completed);
        started = started || value.state.started;
        if (!execution->Observe(value.receipt, value.state) && !request->released)
            execution->Fail("request observation identity changed: " + request->name);
        // An error observation can synchronously cancel this request through
        // release. Publish that drain before dropping our adapter record.
        if (request->released && value.state.drained) {
            TaskRequestState drained;
            drained.drained = true;
            execution->Observe(value.receipt, drained);
        }
    }
    void Supersede(const std::shared_ptr<Request>& request) {
        // Freeze the last actual observation before releasing its observer.
        // Release does not write Animator state or claim a missing final cue.
        request->value.state.superseded = !request->value.state.performance_complete;
        request->released = true;
        if (request->animation) {
            if (request->pose_endpoint)
                request->pose_endpoint->release(request->animation->serial, false);
            else
                request->endpoint->release(request->animation->serial);
        }
        request->value.state.drained = true;
        execution->Observe(request->value.receipt, request->value.state);
    }
    bool ClaimAnimationChannel(const std::shared_ptr<Request>& request, const TaskAnimationCommand& command) {
        std::vector<std::shared_ptr<Request>> motions;
        for (const auto& [id, move] : requests)
            if ((!command.track || command.track->force) && id < request->value.receipt.request &&
                move->animation_dependency) {
                const auto* animation = std::get_if<TaskAnimationCommand>(&move->animation_dependency->operation);
                if (animation && animation->participant.name == command.participant.name &&
                    animation->layer == command.layer)
                    motions.push_back(move);
            }
        bool drained = true;
        for (const auto& move : motions) {
            Release(move->value.receipt, TaskExitReason::Replaced);
            Observe(move);
            drained = drained && move->value.state.drained;
        }
        if (!drained || request->released)
            return false;
        std::vector<std::shared_ptr<Request>> prior;
        for (const auto& [id, other] : requests) {
            if (other == request || other->released)
                continue;
            const auto* animation = std::get_if<TaskAnimationCommand>(&other->operation);
            const bool pose = std::holds_alternative<TaskPoseCommand>(other->operation);
            if (pose ? (command.participant.name != "body" || command.layer != 0)
                     : (!animation || animation->participant.name != command.participant.name ||
                        animation->layer != command.layer))
                continue;
            if (command.track && animation && animation->track)
                continue;
            if (id > request->value.receipt.request) {
                Supersede(request);
                return false;
            }
            prior.push_back(other);
        }
        for (const auto& other : prior) {
            Observe(other); // external errors still fail; they are never relabelled
            if (request->released)
                return false;
            if (!other->released)
                Supersede(other);
            if (request->released)
                return false;
        }
        return true;
    }
    bool ClaimMovementChannel(const std::shared_ptr<Request>& request) {
        bool ready = true;
        std::vector<std::shared_ptr<Request>> previous;
        for (const auto& [id, other] : requests)
            if (id < request->value.receipt.request && std::holds_alternative<TaskMovementCommand>(other->operation))
                previous.push_back(other);
        for (const auto& other : previous) {
            if (!other->released)
                Release(other->value.receipt, TaskExitReason::Interrupted);
            Observe(other);
            ready = ready && other->value.state.drained;
        }
        return ready && !request->released;
    }
    void Pump() {
        if (const auto error = visual->SequenceError(scope); !error.empty()) {
            cancel_scope = true;
            execution->Fail("visual scope: " + error);
        }
        std::vector<std::shared_ptr<Request>> snapshot;
        for (const auto& [id, request] : requests)
            snapshot.push_back(request);
        for (const auto& request : snapshot) {
            try {
                if (!request->released && request->animation_dependency && request->animation_dependency->animation &&
                    request->animation_dependency->animation->advanced)
                    Release(request->value.receipt, TaskExitReason::Replaced);
                if (!request->released && !request->delegated_expression && !request->animation && !request->visual &&
                    !request->sound && !request->movement && !request->projectiles && !request->hit &&
                    !request->point_hit && !request->lifetime && !request->event && !request->parameter &&
                    request->value.state.error.empty()) {
                    if (const auto* attack = std::get_if<TaskAttackCommand>(&request->operation)) {
                        request->event = Attack(
                            *attack, BindVisualParameters(visual->SequenceParameters(scope), request->target_bindings));
                    } else if (const auto* event = std::get_if<TaskEventCommand>(&request->operation)) {
                        if (!services.event)
                            throw std::runtime_error("visual event service unavailable");
                        request->event = services.event(
                            *event, BindVisualParameters(visual->SequenceParameters(scope), request->target_bindings));
                        if (!request->event)
                            throw std::runtime_error("visual event returned no receipt");
                    } else if (const auto* lifecycle = std::get_if<TaskLifetimeCommand>(&request->operation)) {
                        if (!services.lifetime)
                            throw std::runtime_error("task lifetime service unavailable");
                        request->lifetime =
                            services.lifetime(*lifecycle, BindVisualParameters(visual->SequenceParameters(scope),
                                                                               request->target_bindings));
                        if (!request->lifetime)
                            throw std::runtime_error("task lifetime returned no receipt");
                    } else if (const auto* parameter =
                                   std::get_if<TaskAnimationParameterCommand>(&request->operation)) {
                        if (!services.parameter)
                            throw std::runtime_error("animation parameter service unavailable");
                        request->submitting = true;
                        try {
                            request->parameter = services.parameter(parameter->participant, parameter->parameter,
                                                                    parameter->propagation);
                        } catch (...) {
                            request->submitting = false;
                            throw;
                        }
                        request->submitting = false;
                    } else if (const auto* command = std::get_if<TaskAnimationCommand>(&request->operation)) {
                        auto selected = *command;
                        if (command->current_idle && services.current_idle) {
                            const auto idle = services.current_idle();
                            if (!idle)
                                continue;
                            if (idle->pose) {
                                if (!services.pose || services.pose->entity_generation != owner ||
                                    !services.pose->submit || !services.pose->release)
                                    throw std::runtime_error("owned current-idle pose endpoint unavailable");
                                request->pose_endpoint = services.pose;
                                request->value.receipt.entity_generation = owner;
                                if (!ClaimAnimationChannel(request, *command))
                                    continue;
                                request->animation = request->pose_endpoint->submit(*idle->pose);
                                if (request->animation && request->animation->entity_generation != owner)
                                    throw std::runtime_error("current-idle pose returned another entity generation");
                                if (request->released && request->animation)
                                    request->pose_endpoint->release(request->animation->serial, true);
                                Observe(request);
                                continue;
                            }
                            if (!idle->body.empty())
                                selected.state = idle->body;
                        }
                        const bool partner = command->participant.name.compare(0, 8, "partner:") == 0;
                        if (!partner && !services.animation)
                            throw std::runtime_error("task animation resolver unavailable");
                        auto endpoint = partner ? visual->PartnerAnimation(command->participant.name.substr(8))
                                                : services.animation(command->participant);
                        if (request->released)
                            continue;
                        if (endpoint) {
                            if (!endpoint->entity_generation || !endpoint->submit || !endpoint->release)
                                throw std::runtime_error("incomplete owned animation endpoint");
                            request->endpoint = std::move(endpoint);
                            request->value.receipt.entity_generation = request->endpoint->entity_generation;
                            if (!ClaimAnimationChannel(request, *command))
                                continue;
                            request->animation = request->endpoint->submit(
                                {selected.state, selected.layer, selected.transition, selected.normalized_start,
                                 selected.normalized_end, selected.track, selected.restart});
                            if (!request->animation)
                                throw std::runtime_error("animation endpoint returned no receipt");
                            if (request->animation->entity_generation != request->value.receipt.entity_generation)
                                throw std::runtime_error("animation endpoint returned another entity generation");
                            if (request->released)
                                request->endpoint->release(request->animation->serial);
                        }
                    } else if (const auto* expression = std::get_if<TaskExpressionCommand>(&request->operation)) {
                        auto selected = *expression;
                        if (expression->current_idle && services.current_idle) {
                            const auto idle = services.current_idle();
                            if (!idle)
                                continue;
                            if (idle->pose) {
                                request->delegated_expression = true;
                                Observe(request);
                                continue;
                            }
                            if (!idle->expression.empty()) {
                                selected.state = idle->expression;
                                selected.transition = idle->expression_transition;
                            }
                        }
                        if (!services.animation)
                            throw std::runtime_error("expression body resolver unavailable");
                        auto endpoint = services.animation({"body"});
                        if (request->released)
                            continue;
                        if (endpoint) {
                            if (endpoint->entity_generation != owner || !endpoint->expression ||
                                !endpoint->release_expression)
                                throw std::runtime_error("owned expression endpoint unavailable");
                            request->endpoint = std::move(endpoint);
                            request->value.receipt.participant = {"body"};
                            request->value.receipt.entity_generation = owner;
                            request->animation = request->endpoint->expression(selected.state, selected.transition);
                            if (!request->animation || request->animation->entity_generation != owner)
                                throw std::runtime_error("expression returned a foreign or absent receipt");
                            if (request->released)
                                request->endpoint->release_expression(request->animation->serial);
                        }
                    } else if (const auto* hit = std::get_if<TaskHitCommand>(&request->operation)) {
                        request->submitting = true;
                        try {
                            if (hit->allow_position)
                                request->point_hit = visual->SendSequencePointHit(scope, hit->target, hit->hit);
                            if (!request->point_hit)
                                request->hit = visual->SendSequenceHit(scope, hit->target, hit->hit);
                        } catch (...) {
                            request->submitting = false;
                            throw;
                        }
                        request->submitting = false;
                        if (request->point_hit) {
                            if (!request->point_hit->status || !request->point_hit->cancel)
                                throw std::runtime_error("incomplete position hit delivery");
                            request->value.receipt.entity_generation = owner; // sender-owned effect; no receiver
                            if (request->released)
                                request->point_hit->cancel();
                        }
                        if (request->hit) {
                            if (!request->hit->status || !request->hit->status->entity_generation ||
                                !request->hit->cancel)
                                throw std::runtime_error("incomplete owned hit delivery");
                            request->value.receipt.entity_generation = request->hit->status->entity_generation;
                            if (request->released)
                                request->hit->cancel();
                        }
                    } else if (const auto* pose = std::get_if<TaskPoseCommand>(&request->operation)) {
                        if (!services.pose || services.pose->entity_generation != owner || !services.pose->submit ||
                            !services.pose->release)
                            throw std::runtime_error("owned pose endpoint unavailable");
                        request->pose_endpoint = services.pose;
                        request->value.receipt.entity_generation = owner;
                        if (!ClaimAnimationChannel(request, TaskAnimationCommand{{"body"}}))
                            continue;
                        request->animation = request->pose_endpoint->submit(pose->selection);
                        if (request->animation && request->animation->entity_generation != owner)
                            throw std::runtime_error("pose endpoint returned another entity generation");
                        if (request->released && request->animation)
                            request->pose_endpoint->release(request->animation->serial, true);
                    } else if (const auto* sound = std::get_if<TaskSoundCommand>(&request->operation)) {
                        if (!services.sound)
                            throw std::runtime_error("task sound service unavailable");
                        request->submitting = true;
                        try {
                            request->sound = services.sound(*sound);
                        } catch (...) {
                            request->submitting = false;
                            throw;
                        }
                        request->submitting = false;
                        if (request->sound) {
                            if (!request->sound->entity_generation || !request->sound->service ||
                                !request->sound->cancel || !request->sound->status)
                                throw std::runtime_error("incomplete owned sound endpoint");
                            request->value.receipt.entity_generation = request->sound->entity_generation;
                            if (request->released)
                                request->sound->cancel();
                        }
                    } else if (const auto* movement = std::get_if<TaskMovementCommand>(&request->operation)) {
                        auto motion = movement->movement;
                        if (!motion.instant) {
                            const auto dependency = request->animation_dependency;
                            if (!dependency)
                                throw std::runtime_error("movement animation receipt unavailable");
                            if (dependency->value.state.superseded || !dependency->value.state.error.empty())
                                throw std::runtime_error("movement animation request was superseded or failed");
                            if (!dependency->animation || !dependency->animation->entered) {
                                if (dependency->released)
                                    throw std::runtime_error("movement animation request ended before entry");
                                continue;
                            }
                            const auto* animation = std::get_if<TaskAnimationCommand>(&dependency->operation);
                            if (!animation || animation->participant.name != "body" || animation->layer != 0)
                                throw std::runtime_error("movement requires a base-layer body animation");
                            motion.state = animation->state;
                        }
                        if (!ClaimMovementChannel(request))
                            continue;
                        request->submitting = true;
                        try {
                            request->movement = visual->StartSequenceMovement(scope, motion);
                        } catch (...) {
                            request->submitting = false;
                            throw;
                        }
                        request->submitting = false;
                        if (request->movement) {
                            if (request->movement->entity_generation != owner || !request->movement->service ||
                                !request->movement->cancel || !request->movement->status)
                                throw std::runtime_error("incomplete or foreign owned movement endpoint");
                            request->value.receipt.entity_generation = request->movement->entity_generation;
                            if (request->released)
                                request->movement->cancel();
                        }
                    } else if (const auto* projectiles = std::get_if<VisualProjectilePlan>(&request->operation)) {
                        request->submitting = true;
                        try {
                            request->projectiles = visual->StartSequenceProjectiles(scope, *projectiles);
                        } catch (...) {
                            request->submitting = false;
                            throw;
                        }
                        request->submitting = false;
                        if (request->projectiles) {
                            const auto& endpoint = *request->projectiles;
                            if (endpoint.entity_generation != owner || !endpoint.service || !endpoint.cancel ||
                                !endpoint.status)
                                throw std::runtime_error("incomplete or foreign projectile endpoint");
                            request->value.receipt.entity_generation = endpoint.entity_generation;
                            if (request->released)
                                endpoint.cancel();
                        }
                    } else
                        request->visual =
                            visual->SubmitInstruction(scope, std::get<VisualInstruction>(request->operation));
                }
            } catch (const std::exception& error) {
                request->value.state.error = error.what();
            }
            Observe(request);
        }
        for (auto it = requests.begin(); it != requests.end();) {
            if (it->second->released && it->second->value.state.drained) {
                // A later provider may cancel an earlier snapshot entry after
                // that entry was already observed. Publish its drain before
                // erasing it; otherwise the sequence keeps a stranded receipt.
                TaskRequestState drained;
                drained.drained = true;
                execution->Observe(it->second->value.receipt, drained);
                it = requests.erase(it);
            } else
                ++it;
        }
    }
    void EndScope() {
        if (forgotten)
            return;
        const bool natural = execution->Status().reason == TaskExitReason::Completed && !cancel_scope;
        bool settled = true;
        for (const auto& [id, request] : requests)
            settled =
                settled && (request->released || request->value.state.request_complete || request->value.state.skipped);
        if (execution->ProducersComplete() && settled)
            visual->EndSequence(scope, natural);
        if (execution->Drained() && visual->SequenceDrained(scope)) {
            visual->ForgetSequence(scope);
            forgotten = true;
        }
    }
};
TaskPresentation::TaskPresentation(TaskExecutionToken token, std::uint64_t owner,
                                   std::shared_ptr<const TaskSequence> plan,
                                   std::shared_ptr<VisualProgramPlayer> visual, VisualProgramParameters inputs,
                                   TaskPresentationServices services, bool captured, std::uint64_t context,
                                   bool owns_playback, bool needs_movement_origin) {
    if (!token.value || !owner || !visual || !plan)
        throw std::invalid_argument("task presentation requires owned actor and visual scene");
    auto value = std::make_unique<State>();
    value->token = token;
    value->owner = owner;
    value->visual = std::move(visual);
    value->services = std::move(services);
    value->owns_playback = owns_playback;
    auto* state = value.get();
    TaskSequenceServices calls;
    calls.submit = [state](auto token, auto serial, const auto& node) { return state->Submit(token, serial, node); };
    calls.release = [state](const auto& receipt, auto reason) { state->Release(receipt, reason); };
    calls.condition = value->services.condition;
    calls.yield = value->services.yield;
    calls.targets = [state](const std::string& name) -> std::optional<std::vector<std::string>> {
        const auto collections = state->visual->SequenceParameters(state->scope).target_collections;
        const auto found = collections.find(name);
        if (found == collections.end())
            return std::nullopt;
        return found->second;
    };
    const bool capture_origin = needs_movement_origin || plan->NeedsMovementOrigin();
    value->execution = std::make_unique<TaskSequenceExecution>(token, std::move(plan), std::move(calls));
    value->scope = value->visual->BeginSequence(std::move(inputs), captured, context, owns_playback, capture_origin);
    state_ = std::move(value);
}
TaskPresentation::~TaskPresentation() = default;
void TaskPresentation::Service(double seconds, std::uint64_t frame, double playback_rate) {
    auto& state = *state_;
    if (state.servicing)
        return;
    state.servicing = true;
    struct Guard {
        bool& value;
        ~Guard() {
            value = false;
        }
    } guard{state.servicing};
    if (state.visual->Status().closing)
        state.execution->Stop(TaskExitReason::ActorLost);
    state.Pump();
    state.EndScope();
    state.visual->Service();
    state.Pump();
    state.execution->Advance(seconds, frame, playback_rate);
    state.EndScope();
    // Queue this boundary's leaf work without advancing a second clock tick.
    state.Pump();
    state.visual->Service();
    state.Pump();
    state.EndScope();
}
void TaskPresentation::Stop(TaskExitReason reason) {
    state_->execution->Stop(reason);
    state_->cancel_scope = true;
    state_->EndScope();
}
void TaskPresentation::RequestFinish() {
    state_->execution->RequestFinish();
}
const TaskExecutionStatus& TaskPresentation::Status() const {
    return state_->execution->Status();
}
std::vector<TaskRequestSnapshot> TaskPresentation::Requests() const {
    return state_->execution->Requests();
}
bool TaskPresentation::Drained() const {
    return state_->execution->Drained() && state_->forgotten;
}
bool TaskPresentation::Started() const {
    return state_->started;
}
bool TaskPresentation::YieldedControl() const {
    const auto phase = Status().phase;
    if (phase != TaskExecutionPhase::Draining && phase != TaskExecutionPhase::Finished)
        return false;
    for (const auto& [id, request] : state_->requests)
        if (!request->released && !request->value.state.request_complete && !request->value.state.skipped)
            return false;
    return true;
}
}
