#pragma once
#include "HitRequests.hpp"
#include <hksr/animation/OfficialCatalog.hpp>

namespace hksr::framework {
struct BattleControlSnapshot;
struct HitControlInputs {
    std::optional<std::int32_t> visual_flag;
    bool endurance=false,mute_heavy=false,force_heavy=false;
};
HitControlInputs ResolveHitControl(const BattleControlSnapshot&);
struct HitCatalogInputs {
    std::shared_ptr<const api::CharacterHitInput> character;
    std::shared_ptr<const api::HitAnimationDefaults> weights;
    std::shared_ptr<const api::VisualReactionInput> names;
    std::shared_ptr<const api::ReadyAnimationDefaults> animations;
    std::shared_ptr<const api::VisualConfigSources> visuals;
};
using HitCatalogResolver=std::function<std::optional<HitCatalogInputs>(bool visual)>;
enum class HitKind { None,Additive,Airborne,Heavy,Launch };
// Values belong to the receiving actor. The attacker supplies only the source
// HitAnimation and its separately resolved motion request.
struct HitReceiverState {
    float height=0;
    bool launch_allowed=false,has_launch_motion=false;
    bool endurance=false,mute_heavy=false,force_heavy=false;
    std::optional<int> resilience=0;
    std::optional<float> entity_weight=1.f;
    std::optional<float> repeat_occur_at=-1.f;
    std::optional<bool> in_occur=false;
    std::optional<float> occur_progress=0.f;
    std::uint64_t frame=0;
};
struct ReactionSources {
    const api::VisualReactionInput& defaults;
    const api::VisualReactionInput* phase=nullptr;
    // Already selected by the receiver's active visual-source priority.
    // A present row with empty strings is not the same as an absent row.
    const api::VisualBehaviorInput* visual=nullptr;
};
struct PreviousHit {
    std::string state;
    std::uint64_t frame=0;
};
struct HitDecision {
    HitKind kind=HitKind::None;
    std::string state;
    float weight=0;
    bool repeated_occur=false,throttled=false;
};
// Pure selection: no entity lookup, task dispatch, movement or control-state
// change. Flight/return ownership is handled separately from this decision.
HitDecision SelectHit(const std::string& hit_animation,const HitReceiverState&,
    const ReactionSources&,const api::HitAnimationDefaults&,const std::optional<PreviousHit>& = {});
struct HitPlaybackDefaults {
    std::string visual_layer,additive_layer,override_layer,expression;
    float transition=0,expression_transition=0;
    int blink_source=-1;
};
HitCommand BuildHitCommand(const HitDecision&,const HitPlaybackDefaults&,float start=0);

struct IdleDecision {
    std::string state,expression;
    bool visual=false;
};
// The caller supplies the current row's globally selected expression, just as
// ResolveVisualState does. A character replacement row cannot override it.
IdleDecision SelectIdle(const api::TextInput& default_idle,const api::TextInput& default_expression,
    const api::PhaseVisualConfigSources* phase,const api::VisualBehaviorInput* visual,
    const api::TextInput* visual_expression);
}
