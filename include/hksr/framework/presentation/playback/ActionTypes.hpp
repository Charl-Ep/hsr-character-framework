#pragma once
#include <hksr/framework/presentation/playback/ReadyActions.hpp>
#include <hksr/framework/presentation/playback/ActionRetirement.hpp>
#include <hksr/framework/presentation/reactions/DeathOccurrence.hpp>
#include <hksr/framework/presentation/reactions/PlaybackEffects.hpp>
#include <variant>
#include <optional>
#include <map>

namespace hksr::framework {
enum class ActionDomain { Exploration, Battle };
struct NpcMotion {
    unsigned id = 0;
};
struct BattleIdle {}; // resolve against the live phase after model/context restoration
struct AnimatorState {
    std::string name;
    int layer = 0;
    float normalized_time = 0;
    bool resolve_layer = false;
};
// Official ordinary FreeStyle path forwards normalized transition duration
// and normalized start offset to MockAnimator.CrossFade (4.6 static chain).
struct FreeStyleState {
    std::string name;
    float normalized_transition_duration = 0, normalized_start = 0;
    std::vector<ActionRetirement> retirements;
    bool controller_observed = false; // named successor read from the actual Animator
    bool resolve_layer = false;       // catalog reference without a declared Animator layer
};
// Authored presentation identity. Its body state is resolved from the bound
// program, never inferred from an Animator state name.
struct ProgramAction {
    std::string program, action;
    std::map<std::string, std::string> choices;
};
// Explicit visual-row identity, NOT a mapping inferred from body state names.
// This player controls body phases and the named expression only; gameplay
// flags, material/speed/floating effects are deliberately not applied.
struct VisualState {
    std::string source;
    api::VisualBehaviorInput value;
    bool recover = false;
    api::TextInput expression;
    std::string expression_source;
    api::ConfigValue<float> expression_transition;
    int blink_source = -1;
    std::string character_config; // resolution context; global rows are not character identities
    DeathOccurrence occurrence = DeathOccurrence::Configured;
    std::optional<api::VisualReactionInput>
        death_defaults; // prepared pure-value fallback, independent of incoming-hit services
};
std::optional<VisualState> ResolveVisualState(const api::VisualConfigSources&, std::int32_t flag, bool recover = false);
using ActionRequest =
    std::variant<NpcMotion, AnimatorState, FreeStyleState, VisualState, ReadyState, ProgramAction, BattleIdle>;
struct PlacedAction {
    ActionDomain domain;
    ActionRequest request;
};
enum class ActionOutcome { Waiting, Returned, StateAbsent, Unavailable, CallFailed, OwnerEnded, Cancelled };
enum class ActionIntentPhase { Waiting, Dispatching, Returned, Failed, Cancelled };
// The request identity belongs to the owner supplying this observation. A
// settled successor updates saved intent; it must never be submitted again.
struct ActionExecutionStatus {
    std::uint64_t request = 0;
    ActionIntentPhase phase = ActionIntentPhase::Waiting;
    std::optional<ActionOutcome> outcome;
    std::string detail;
    std::optional<PlacedAction> settled_successor;
};
struct ActionReceipt {
    std::uint64_t serial = 0;
    ActionRequest request;
    ActionOutcome outcome = ActionOutcome::Waiting;
    api::Observed<int> hash;
    api::Observed<bool> has_state, initialized;
    api::CallResult call;
    std::string detail;
    bool task_owned = false;
    std::optional<AcceptedPlaybackEffect> accepted_effect;
};
struct AutomaticSuccessor {
    std::uint64_t predecessor = 0, serial = 0;
    std::string state;
    bool returned = false;
    bool observed = false; // official controller reached this state without our playback call
    bool controller_state = false;
};
struct ActionPlaybackStatus {
    std::uint64_t latest_serial = 0;
    bool pending = false, closing = false, closed = false;
    std::optional<ActionReceipt> last;
    bool completion_pending = false;
    std::string completion_note;
    std::string visual_note;
    std::string blink_note;
    std::string ready_note;
    std::string effect_note;
    std::optional<AutomaticSuccessor> successor;
    std::string presentation_phase, presentation_note;
    std::string voice_error;
    std::size_t voice_requests = 0;
};
}
