#pragma once
#include "VisualEffectContext.hpp"
#include <functional>
#include <deque>
#include <optional>
#include <set>
#include <variant>

namespace hksr::framework {
struct VisualEffectInput {
    std::string path,target;
    api::EntityEffectOptions options;
    std::string anchor; // named world pose, mutually exclusive with target
};
struct VisualSpatialAnchor {api::Position3 position;api::Rotation4 rotation;};
using VisualSpatialAnchors=std::map<std::string,VisualSpatialAnchor>;
VisualSpatialAnchors ParseVisualSpatialAnchors(const std::string& json);
struct VisualCreate {std::vector<std::string> effects;};
struct VisualAssign {std::string variable;float value=0;};
struct VisualProgress {std::string effect;std::variant<float,std::string> value;float seconds=0;};
struct VisualRemove {
    std::vector<std::string> effects;bool fade=true,immediate=false;
    std::set<std::string> optional; // absent in some authored branches
};
struct VisualTrigger {std::vector<std::string> effects;std::string trigger;bool reset=false;};
struct VisualFollow {std::string effect;std::vector<std::string> targets;}; // empty releases this binding
struct VisualEffectState {std::string effect,state;float transition=0;bool restart=false;};
struct VisualContextOperation {std::string name;bool enter=true;bool fade=true;};
struct VisualTimeline {std::string key,path;bool play=true;int type=0,wrap_mode=0;bool release_volume=false;};
using VisualOperation=std::variant<VisualCreate,VisualAssign,VisualProgress,VisualRemove,VisualTrigger,VisualTimeline,VisualFollow,VisualEffectState,VisualContextOperation>;
struct VisualInstruction {
    VisualOperation operation;
    std::string source; // original configuration node, for diagnostics
    std::string choice,value; // optional explicit preview input, not a gameplay flag
    std::string context; // empty uses the current execution's effect owner
};
struct VisualProgramContext {std::vector<VisualInstruction> enter,exit;std::string event_scope,phase;};
struct VisualProgramSignalDefinition {std::string context;std::vector<VisualInstruction> instructions;};
struct VisualCue {float at=0;std::vector<VisualInstruction> instructions;};
struct VisualProgramAction {
    std::string state;
    bool restart=false;
    std::vector<VisualInstruction> enter;
    // Independent lanes advance from the same observed body clock. Pending
    // resource control in one lane does not stop unrelated visual work.
    std::vector<std::vector<VisualCue>> lanes;
    std::string completion_state;
    float complete_at=1;
    float completion_at=0; // observed successor normalized progress, not elapsed time
    std::vector<VisualInstruction> after_completion;
};
struct VisualProgram {
    std::string id,source,scope;
    std::map<std::string,VisualEffectInput> effects;
    std::map<std::string,VisualProgramAction> actions;
    std::map<std::string,VisualProgramSignalDefinition> signals;
    std::map<std::string,VisualProgramContext> contexts;
    std::map<std::string,api::ModifierPhaseSource> phases;
};
VisualProgram ParseVisualProgram(const std::string& json);

enum class VisualStepState { Done, Pending, Failed };
struct VisualStepResult {VisualStepState state=VisualStepState::Failed;std::string detail;};
struct VisualProgramServices {
    std::shared_ptr<const api::PresentationEffects> effects;
    // Resolves the authored target explicitly, roots text and both entities
    // through StartEntity. Never substitutes the caster for an absent target.
    std::function<VisualEffectId(VisualEffectContext&,const std::string&,const VisualEffectInput&)> create;
    std::function<VisualStepResult(VisualEffectContext&,VisualEffectId,const VisualTrigger&)> trigger;
    std::function<VisualStepResult(VisualEffectContext&,VisualEffectId,const VisualFollow&)> follow;
    std::function<VisualStepResult(VisualEffectContext&,VisualEffectId,const VisualEffectState&)> effect_state;
    std::function<VisualStepResult(VisualEffectContext&,const VisualRemove&)> remove;
    // Scope zero belongs to the action; named lifetimes use their context token.
    std::function<VisualStepResult(std::uint64_t,const VisualTimeline&)> timeline;
    std::function<void(std::shared_ptr<VisualEffectContext>)> attach;
    std::function<void()> detach;
    std::function<void(std::uint64_t)> cancel_timelines;
    std::function<bool()> timelines_drained;
    std::function<bool(std::shared_ptr<VisualEffectContext>,const std::string&,api::EffectRemovalOptions)> remove_in_flight;
    std::map<std::string,std::string> choices;
    // Called once when an action selection is consumed, before any instruction.
    // Returns current defaults; explicit Select choices take precedence.
    std::function<std::map<std::string,std::string>()> select_inputs;
    // Capture resolved input values for a named lifetime before it can submit.
    // Does not attach native animation-event producers.
    std::function<void(const std::string&,std::shared_ptr<VisualEffectContext>)> context_started;
    std::function<void(const std::string&)> context_stopped;
    std::function<std::shared_ptr<const api::PhaseVisualConfigSources>(const std::string&)> read_phase;
};
struct VisualProgramStatus {
    std::uint64_t intent=0,playback=0,model=0;
    std::string action,state;
    std::uint64_t last_signal=0,completed_signal=0;
    std::string signal;
    bool prepared=false,playing=false,finished=false,closing=false,completing=false,completed=false;
    std::vector<std::string> issues;
    std::vector<std::string> waiting;
    std::vector<std::string> notes;
};
// Engine-thread, per-actor scene. It owns effects across body actions, but does
// not call an Animator or infer gameplay choices. The caller waits for Prepared,
// binds an actual playback/model receipt, then supplies that receipt's progress.
class VisualProgramPlayer {
public:
    VisualProgramPlayer(std::shared_ptr<const VisualProgram>,VisualProgramServices);
    std::uint64_t Select(const std::string& action,std::map<std::string,std::string> choices={});
    // The token is the current action intent for action signals, or the named
    // ContextToken for context signals. Unknown names throw; stale tokens
    // return zero. Signals never play a body state.
    std::uint64_t Signal(std::uint64_t intent,const std::string& name);
    void Service(bool advance=true); // false collects lifetimes without advancing instructions
    bool Played(std::uint64_t intent,std::uint64_t playback,std::uint64_t model);
    bool SuccessorPlayed(std::uint64_t intent,std::uint64_t playback,std::uint64_t model);
    void Observe(std::uint64_t playback,std::uint64_t model,const std::string& state,float progress);
    void Leave(bool fade=true); // end this scene; a later restart action may enter again
    void InterruptAction(bool fade=true); // keep explicitly entered contexts
    void End(bool fade=true); // no engine calls; Service drains on owner thread
    std::optional<std::string> ActionForState(const std::string&) const;
    const VisualProgramAction* CurrentAction() const {return action_;}
    const VisualProgramStatus& Status() const {return status_;}
    std::shared_ptr<VisualEffectContext> Context() const {return context_;}
    std::uint64_t ContextToken(const std::string&) const;
    std::map<std::string,std::uint64_t> ActiveContexts() const;
    // Last-entered phase owner wins; removing it restores the prior live owner.
    std::shared_ptr<const api::PhaseVisualConfigSources> CurrentPhase() const;
    bool OwnsPhases() const {return !program_->phases.empty();}
    bool Drained() const;
private:
    struct Cursor {
        std::size_t cue=0,instruction=0;
        std::set<VisualEffectId> completed;
        std::optional<std::vector<VisualEffectId>> receipts;
        std::size_t created=0;
        std::shared_ptr<VisualEffectContext> context;
        void Next(){++instruction;completed.clear();receipts.reset();created=0;context.reset();}
    };
    enum class ContextPhase { Entering,Active,Exiting };
    struct Lifetime {
        std::uint64_t token=0;
        std::string name;
        std::shared_ptr<VisualEffectContext> effects;
        ContextPhase phase=ContextPhase::Entering;
        Cursor enter,exit;
        std::map<std::string,float> variables;
        std::map<std::string,std::string> choices;
        bool fade=true;
        std::shared_ptr<const api::PhaseVisualConfigSources> phase_data;
    };
    struct Execution {std::uint64_t intent=0;std::shared_ptr<Lifetime> lifetime;};
    struct Selection {std::uint64_t intent;std::string action;std::map<std::string,std::string> choices;};
    struct SignalExecution {std::uint64_t serial;Execution execution;std::string name;Cursor cursor;};
    bool Current(const Execution&) const;
    bool Instructions(const std::vector<VisualInstruction>&,Cursor&,const Execution&);
    VisualStepResult Execute(const VisualInstruction&,Cursor&,const Execution&);
    std::shared_ptr<VisualEffectContext> ExecutionContext(const VisualInstruction&,const Execution&) const;
    VisualStepResult ChangeContext(const VisualContextOperation&);
    void ServiceContexts(bool advance);
    void DriveContext(std::shared_ptr<Lifetime>);
    void EndContexts(bool fade);
    void DiscardActionSignals();
    void ServiceSignals();
    void Issue(const VisualInstruction&,const std::string&);
    void Retire(bool fade);
    void Finish();
    std::shared_ptr<const VisualProgram> program_;
    VisualProgramServices services_;
    std::shared_ptr<VisualEffectContext> context_;
    std::vector<std::shared_ptr<VisualEffectContext>> retired_;
    std::map<std::string,std::shared_ptr<Lifetime>> contexts_;
    std::vector<std::shared_ptr<VisualEffectContext>> retired_contexts_;
    std::map<std::string,float> variables_;
    std::map<std::string,std::string> choices_;
    std::optional<Selection> pending_;
    std::deque<std::shared_ptr<SignalExecution>> signals_;
    const VisualProgramAction* action_=nullptr;
    VisualProgramStatus status_;
    Cursor enter_,completion_;
    std::vector<Cursor> lanes_;
    std::uint64_t next_=0;
    std::uint64_t next_signal_=0;
    std::uint64_t next_context_=0;
    std::optional<float> progress_;
    bool servicing_=false,end_pending_=false,end_fade_=true,timelines_pending_=false,interrupt_pending_=false;
};
}
