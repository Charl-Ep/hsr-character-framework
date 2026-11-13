#pragma once
#include <hksr/framework/presentation/scene/VisualProgram.hpp>
#include <hksr/framework/presentation/effects/VisualEffectContext.hpp>
#include <hksr/framework/presentation/scene/VisualStepResult.hpp>
#include <hksr/framework/presentation/playback/AnimationRequests.hpp>
#include <hksr/framework/presentation/reactions/HitInbox.hpp>
#include <hksr/framework/presentation/effects/SpatialHitEffects.hpp>
#include <hksr/framework/presentation/movement/VisualMovement.hpp>
#include <hksr/framework/presentation/projectiles/VisualProjectiles.hpp>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace hksr::framework {
class TaskContextRegistry;
struct VisualProgramServices {
    std::uint64_t character = 0; // stable Framework ID, not an engine generation
    std::shared_ptr<TaskContextRegistry> task_contexts;
    // Capture participant generations once; the returned check performs no
    // native calls and never retargets a recreated character implicitly.
    std::function<std::function<bool()>(const VisualContextBinding&)> bind_participants;
    std::shared_ptr<const api::PresentationEffects> effects;
    // Resolves the authored target explicitly, roots text and both entities
    // through StartEntity. Never substitutes the caster for an absent target.
    std::function<VisualEffectAdmission(VisualEffectContext&, const std::string&, const VisualEffectInput&)> create;
    std::function<VisualStepResult(VisualEffectContext&, VisualEffectId, const VisualTrigger&)> trigger;
    std::function<VisualStepResult(VisualEffectContext&, VisualEffectId, const VisualFollow&)> follow;
    std::function<VisualStepResult(VisualEffectContext&, VisualEffectId, const VisualEffectState&)> effect_state;
    std::function<VisualStepResult(VisualEffectContext&, VisualEffectId, VisualEffectId, const VisualEffectLayout&)>
        layout;
    std::function<VisualStepResult(std::shared_ptr<VisualEffectContext>, const VisualBgmSync&)> bgm_sync;
    std::function<VisualStepResult(VisualEffectContext&, const VisualRemove&)> remove;
    std::function<VisualStepResult(const VisualRemoveEventEffect&)> remove_event_effect;
    // Scope zero belongs to the action; named lifetimes use their context token.
    std::function<VisualStepResult(std::uint64_t, const VisualTimeline&)> timeline;
    std::function<void(std::shared_ptr<VisualEffectContext>)> attach;
    std::function<void()> detach;
    std::function<void(std::uint64_t)> cancel_timelines;
    std::function<bool()> timelines_drained;
    std::function<bool(std::uint64_t)> timeline_scope_drained;
    std::function<void(std::uint64_t)> forget_timelines;
    std::function<bool(std::shared_ptr<VisualEffectContext>, const std::string&, api::EffectRemovalOptions)>
        remove_in_flight;
    std::map<std::string, std::string> choices;
    // Called once when an action selection is consumed, before any instruction.
    // Returns current defaults; explicit Select choices take precedence.
    std::function<VisualProgramParameters()> select_inputs;
    // Value-only owner/generation lookup; no engine reads or resource creation.
    std::function<std::vector<std::string>(const VisualProgramParameters&)> available_targets;
    std::vector<std::string> bound_targets; // native-only bindings also block fallback if their owner ends
    // Capture resolved input values for a named lifetime before it can submit.
    // Does not attach native animation-event producers.
    std::function<void(const VisualContextKey&, std::uint64_t, std::shared_ptr<VisualEffectContext>)> context_started;
    std::function<void(const VisualContextKey&, std::uint64_t)> context_stopped;
    std::function<std::shared_ptr<const api::PhaseVisualConfigSources>(const std::string&)> read_phase;
    // Called before producers attach. Flags identify restoration and a prepared
    // plan's need to capture its pre-animation movement origin.
    std::function<void(std::shared_ptr<VisualEffectContext>, const VisualProgramParameters&, bool, bool)> prepare_inputs;
    std::function<VisualProgramParameters(const VisualEffectContext&)> capture_inputs;
    std::function<VisualProgramParameters(const VisualEffectContext&)> scope_inputs;
    std::function<VisualStepResult(const VisualProgramParameters&)> restore_inputs_ready;
    std::function<VisualStepResult(std::shared_ptr<VisualEffectContext>, const VisualPartnerOperation&)> partner;
    std::function<void(std::shared_ptr<VisualEffectContext>)> end_partners;
    std::function<bool()> partners_drained;
    std::function<bool(std::shared_ptr<VisualEffectContext>)> partner_scope_drained;
    std::function<std::optional<AnimationRequestEndpoint>(const std::string&)> partner_animation;
    std::function<std::optional<MovementRequestEndpoint>(VisualEffectContext&, const VisualMovement&)> movement;
    std::function<ProjectileRequestEndpoint(std::shared_ptr<VisualEffectContext>, const VisualProjectilePlan&)>
        projectiles;
    std::function<VisualStepResult(VisualEffectContext&, const VisualFace&)> face;
    std::function<VisualStepResult(std::shared_ptr<VisualEffectContext>, const VisualAttachmentVisibility&)>
        attachment_visibility;
    std::function<VisualStepResult(std::shared_ptr<VisualEffectContext>, const VisualAttachmentScale&)>
        attachment_scale;
    std::function<VisualStepResult(std::shared_ptr<VisualEffectContext>, const VisualLayerWeight&)> layer_weight;
    std::function<VisualStepResult(std::shared_ptr<VisualEffectContext>, const VisualAnimatorSpeed&)> animator_speed;
    std::function<VisualStepResult(std::shared_ptr<VisualEffectContext>, const VisualActorTimeScale&)> actor_time_scale;
    std::function<VisualStepResult(std::shared_ptr<VisualEffectContext>, const VisualRenderingOperation&)> rendering;
    std::function<VisualStepResult(std::shared_ptr<VisualEffectContext>, const VisualModelOperation&)> model;
    std::function<std::optional<HitDelivery>(VisualEffectContext&, const std::string&, const HitInput&)> hit;
    std::function<std::optional<SpatialHitEndpoint>(std::shared_ptr<VisualEffectContext>, const std::string&,
                                                    const HitInput&)>
        point_hit;
};
struct VisualInstructionStatus {
    bool complete = false, cancelled = false;
    std::vector<std::string> issues;
    bool skipped = false;
    std::string skip_reason;
};
struct VisualProgramStatus {
    std::uint64_t intent = 0, playback = 0, model = 0;
    std::string action, state;
    std::uint64_t last_signal = 0, completed_signal = 0;
    std::string signal;
    bool prepared = false, playing = false, finished = false, closing = false, completing = false, completed = false;
    std::vector<std::string> issues;
    std::vector<std::string> waiting;
    std::vector<std::string> notes;
};
// Engine-thread, per-actor scene. It owns effects across body actions, but does
// not call an Animator or infer gameplay choices. The caller waits for Prepared,
// binds an actual playback/model receipt, then supplies that receipt's progress.
class VisualProgramPlayer {
  public:
    std::optional<HitDelivery> SendSequenceHit(std::uint64_t, const std::string& target, const HitInput&);
    VisualProgramPlayer(std::shared_ptr<const VisualProgram>, VisualProgramServices);
    std::uint64_t Select(const std::string& action, std::map<std::string, std::string> choices = {});
    // The token is the current action intent for action signals, or the named
    // ContextToken for context signals. Unknown names throw; stale tokens
    // return zero. Signals never play a body state.
    std::uint64_t Signal(std::uint64_t intent, const std::string& name);
    void Service(bool advance = true); // false collects lifetimes without advancing instructions
    bool Played(std::uint64_t intent, std::uint64_t playback, std::uint64_t model);
    bool SuccessorPlayed(std::uint64_t intent, std::uint64_t playback, std::uint64_t model);
    void Observe(std::uint64_t playback, std::uint64_t model, const std::string& state, float progress);
    void Leave(bool fade = true);           // end this scene; a later restart action may enter again
    void InterruptAction(bool fade = true); // keep explicitly entered contexts
    void End(bool fade = true);             // no engine calls; Service drains on owner thread
    const VisualProgramAction& ResolveAction(const std::string& program, const std::string& action) const;
    const VisualProgramAction* CurrentAction() const {
        return action_;
    }
    const VisualProgramStatus& Status() const {
        return status_;
    }
    std::shared_ptr<VisualEffectContext> Context() const {
        return context_;
    }
    std::uint64_t Character() const {
        return services_.character;
    }
    // Only the owned model-change operation records this success. Consumers
    // may migrate their private native binding, never their public receipt.
    void OwnedModelChanged() {
        ++owned_model_revision_;
    }
    std::uint64_t OwnedModelRevision() const {
        return owned_model_revision_;
    }
    std::shared_ptr<TaskContextRegistry> TaskContexts() const {
        return services_.task_contexts;
    }
    void DeclareStateContexts(std::set<std::string>); // metadata only; does not enter the scene
    void DeclareMovementContexts(std::set<std::string> contexts) {
        movement_contexts_ = std::move(contexts);
    }
    std::function<bool()> BindTaskParticipants(const VisualContextBinding&) const;
    std::uint64_t ContextToken(const std::string&) const;
    std::uint64_t InstanceToken(const VisualContextKey&) const;
    std::map<VisualContextKey, std::uint64_t> ActiveContexts() const;
    std::map<VisualContextKey, std::uint64_t> ReadyContexts() const; // excludes entry/exit in flight
    // Last-entered phase owner wins; removing it restores the prior live owner.
    std::shared_ptr<const api::PhaseVisualConfigSources> CurrentPhase() const;
    bool OwnsPhases() const {
        return !program_->phases.empty();
    }
    VisualProgramSnapshot Capture() const;
    VisualBranchState BranchState() const;
    void Restore(VisualProgramSnapshot); // data-only, before scene entry
    bool Restoring() const {
        return restore_.has_value();
    }
    bool Drained() const;
    VisualProgramParameters Parameters(VisualProgramParameters overrides = {}) const;
    std::vector<std::string> AvailableTargets(const VisualProgramParameters&) const;
    std::vector<std::string> AvailablePositionTargets(const VisualProgramParameters&) const;
    VisualStepResult RestoreInputsReady(const VisualProgramParameters& inputs) const {
        return services_.restore_inputs_ready ? services_.restore_inputs_ready(inputs)
                                              : VisualStepResult{VisualStepState::Done, {}};
    }
    // Task sequences share the same instruction executor and native resource
    // owners as authored programs, without inventing a body action/clock.
    std::uint64_t BeginSequence(VisualProgramParameters, bool captured = false, std::uint64_t context = 0,
                                bool owns_playback = true, bool needs_movement_origin = false);
    VisualProgramParameters SequenceParameters(std::uint64_t) const;
    // Persistent task callbacks borrow this resource owner. Entry, withdrawal,
    // exit execution and native drain remain separate operations.
    std::uint64_t OpenTaskContext(const VisualContextKey&, VisualProgramParameters,
                                  std::optional<VisualContextBinding> = {});
    bool ActivateTaskContext(const VisualContextKey&, std::uint64_t);
    void WithdrawTaskContext(const VisualContextKey&, std::uint64_t);
    void ReleaseTaskContext(const VisualContextKey&, std::uint64_t);
    std::optional<VisualContextBinding> TaskContextBinding(std::uint64_t) const;
    void ReplaceTaskContextInputs(const VisualContextKey&, std::uint64_t, VisualProgramParameters,
                                  std::optional<VisualContextBinding>);
    bool TaskContextDrained(std::uint64_t) const;
    VisualProgramParameters TaskContextParameters(std::uint64_t) const;
    const ContextNumbers& TaskContextNumbers(std::uint64_t) const;
    void SetTaskContextNumber(std::uint64_t, const std::string&, ContextNumber);
    const ContextValues& TaskContextValues(std::uint64_t) const;
    void SetTaskContextValue(std::uint64_t, const std::string&, ContextValue);
    std::shared_ptr<const VisualInstructionStatus> SubmitInstruction(std::uint64_t, const VisualInstruction&);
    void CancelInstruction(std::uint64_t, const std::shared_ptr<const VisualInstructionStatus>&);
    void EndSequence(std::uint64_t, bool natural);
    bool SequenceDrained(std::uint64_t) const;
    std::string SequenceError(std::uint64_t) const;
    void ForgetSequence(std::uint64_t); // only after drain
    std::optional<AnimationRequestEndpoint> PartnerAnimation(const std::string& key) const;
    std::optional<MovementRequestEndpoint> StartSequenceMovement(std::uint64_t, const VisualMovement&);
    std::optional<ProjectileRequestEndpoint> StartSequenceProjectiles(std::uint64_t, const VisualProjectilePlan&);
    std::optional<SpatialHitEndpoint> SendSequencePointHit(std::uint64_t, const std::string&, const HitInput&);

  private:
    struct Cursor {
        std::size_t cue = 0, instruction = 0;
        std::set<VisualEffectId> completed;
        std::set<VisualEffectId> skipped;
        std::optional<std::vector<VisualEffectId>> receipts;
        VisualEffectId layout_target = 0;
        std::size_t created = 0;
        std::shared_ptr<VisualEffectContext> context;
        void Next() {
            ++instruction;
            completed.clear();
            skipped.clear();
            receipts.reset();
            layout_target = 0;
            created = 0;
            context.reset();
        }
    };
    enum class ContextPhase { Entering, Active, Exiting };
    struct Lifetime {
        std::uint64_t token = 0;
        std::string name;
        VisualContextKey key;
        std::optional<VisualContextBinding> binding;
        std::function<bool()> participants;
        std::shared_ptr<VisualEffectContext> effects;
        ContextPhase phase = ContextPhase::Entering;
        Cursor enter, exit;
        std::map<std::string, float> variables;
        ContextNumbers numbers;
        ContextValues values;
        std::map<std::string, std::string> choices;
        bool fade = true;
        bool restoring = false;
        std::vector<VisualInstruction> restore_steps;
        bool task_owned = false;
        bool state_only = false;
        bool needs_movement_origin = false;
        VisualProgramParameters inputs;
        std::shared_ptr<const api::PhaseVisualConfigSources> phase_data;
    };
    struct Execution {
        std::uint64_t intent = 0;
        std::shared_ptr<Lifetime> lifetime;
        std::vector<std::string>* issues = nullptr;
    };
    struct InstructionRequest {
        VisualInstruction input;
        Cursor cursor;
        std::shared_ptr<VisualInstructionStatus> status;
    };
    struct InstructionScope {
        std::uint64_t token = 0;
        std::shared_ptr<Lifetime> lifetime;
        std::vector<std::shared_ptr<InstructionRequest>> requests;
        bool prepared = false, ending = false, natural = false, released = false, drained = false, captured = false;
        std::string error;
        bool borrowed = false;
        bool owns_playback = true;
    };
    std::map<std::uint64_t, std::shared_ptr<InstructionScope>> instruction_scopes_;
    std::uint64_t attached_sequence_ = 0;
    void ServiceSequences(bool advance);
    void EndSequences(bool all = true);
    struct Selection {
        std::uint64_t intent;
        std::string action;
        std::map<std::string, std::string> choices;
    };
    struct SignalExecution {
        std::uint64_t serial;
        Execution execution;
        std::string name;
        Cursor cursor;
    };
    bool Current(const Execution&) const;
    bool Instructions(const std::vector<VisualInstruction>&, Cursor&, const Execution&);
    VisualStepResult Execute(const VisualInstruction&, Cursor&, const Execution&);
    std::shared_ptr<VisualEffectContext> ExecutionContext(const VisualInstruction&, const Execution&) const;
    VisualStepResult ChangeContext(const VisualContextOperation&, const Execution* source = nullptr,
                                   const VisualTargetAliases& bindings = {});
    std::shared_ptr<Lifetime> StartContext(const VisualContextKey&, const VisualContextSnapshot* restore = nullptr,
                                           const VisualProgramParameters* inputs = nullptr,
                                           std::optional<VisualContextBinding> binding = {});
    bool ServiceRestore();
    void ServiceContexts(bool advance);
    void DriveContext(std::shared_ptr<Lifetime>);
    void EndContexts(bool fade);
    void DiscardActionSignals();
    void ServiceSignals();
    void Issue(const VisualInstruction&, const std::string&, const Execution& execution = {});
    void Retire(bool fade);
    void EndPartners(const std::shared_ptr<VisualEffectContext>&);
    void Finish();
    std::shared_ptr<const VisualProgram> program_;
    VisualProgramServices services_;
    std::shared_ptr<VisualEffectContext> context_;
    std::vector<std::shared_ptr<VisualEffectContext>> retired_;
    std::map<VisualContextKey, std::shared_ptr<Lifetime>> contexts_;
    std::set<std::string> state_contexts_;
    std::set<std::string> movement_contexts_;
    std::vector<std::shared_ptr<VisualEffectContext>> retired_contexts_;
    std::map<std::uint64_t, std::shared_ptr<Lifetime>> retired_task_contexts_;
    std::map<std::string, float> variables_;
    std::map<std::string, std::string> choices_;
    VisualProgramParameters selected_inputs_;
    bool selected_inputs_restored_ = false;
    std::optional<VisualProgramParameters> restored_inputs_;
    std::optional<Selection> pending_;
    struct Restoration {
        VisualProgramSnapshot value;
        bool scene_started = false;
        Cursor scene;
    };
    std::optional<Restoration> restore_;
    std::deque<std::shared_ptr<SignalExecution>> signals_;
    const VisualProgramAction* action_ = nullptr;
    VisualProgramStatus status_;
    Cursor enter_, completion_;
    std::vector<Cursor> lanes_;
    std::uint64_t next_ = 0;
    std::uint64_t owned_model_revision_ = 0;
    std::uint64_t next_signal_ = 0;
    std::uint64_t next_context_ = 0;
    std::optional<float> progress_;
    bool servicing_ = false, end_pending_ = false, end_fade_ = true, timelines_pending_ = false,
         partners_pending_ = false, interrupt_pending_ = false;
};
}
