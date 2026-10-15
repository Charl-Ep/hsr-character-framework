#pragma once
#include "ReadyActions.hpp"
#include "ActionRetirement.hpp"
#include <variant>
#include <optional>

namespace hksr::framework {
enum class ActionDomain { Exploration, Alternate };
struct NpcMotion { unsigned id=0; };
struct AnimatorState { std::string name; int layer=0; float normalized_time=0; };
// Official ordinary FreeStyle path forwards normalized transition duration
// and normalized start offset to MockAnimator.CrossFade (4.6 static chain).
struct FreeStyleState {
    std::string name; float first=0,second=0;
    std::vector<ActionRetirement> retirements;
    bool controller_observed=false; // named successor read from the actual Animator
};
// Explicit visual-row identity, NOT a mapping inferred from body state names.
// This player controls body phases and the named expression only; gameplay
// flags, material/speed/floating effects are deliberately not applied.
struct VisualState {
    std::string source; api::VisualBehaviorInput value; bool recover=false;
    api::TextInput expression;std::string expression_source;
    api::ConfigValue<float> expression_transition;
    int blink_source=-1;
};
using ActionRequest=std::variant<NpcMotion,AnimatorState,FreeStyleState,VisualState,ReadyState>;
enum class ActionOutcome { Waiting, Returned, StateAbsent, Unavailable, CallFailed, OwnerEnded, Cancelled };
struct ActionReceipt {
    std::uint64_t serial=0;
    ActionRequest request;
    ActionOutcome outcome=ActionOutcome::Waiting;
    api::Observed<int> hash;
    api::Observed<bool> has_state,initialized;
    api::CallResult call;
    std::string detail;
};
struct AutomaticSuccessor {
    std::uint64_t predecessor=0,serial=0;
    std::string state;
    bool returned=false;
    bool observed=false; // official controller reached this state without our playback call
    bool controller_state=false;
};
struct ActionPlaybackStatus {
    std::uint64_t latest_serial=0;
    bool pending=false,closing=false,closed=false;
    std::optional<ActionReceipt> last;
    bool completion_pending=false;
    std::string completion_note;
    std::string visual_note;
    std::string blink_note;
    std::string ready_note;
    std::string effect_note;
    std::optional<AutomaticSuccessor> successor;
    std::string presentation_phase,presentation_note;
};
}
