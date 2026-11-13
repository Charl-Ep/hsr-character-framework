#pragma once
#include <hksr/framework/presentation/playback/ActionTypes.hpp>

namespace hksr::framework::detail {
template <class T> bool SameDisplayInput(const T& a, const T& b) {
    return a.available == b.available && (!a.available || a.value == b.value);
}
inline bool SameVisualPhase(const api::VisualPhaseInput& a, const api::VisualPhaseInput& b) {
    return SameDisplayInput(a.resolved_state, b.resolved_state) && SameDisplayInput(a.transition, b.transition);
}
inline bool SameVisualBody(const VisualState& a, const VisualState& b, bool recover) {
    if (a.character_config != b.character_config)
        return false;
    if (recover)
        return SameVisualPhase(a.value.recover, b.value.recover);
    return SameVisualPhase(a.value.occur, b.value.occur) && SameVisualPhase(a.value.loop, b.value.loop) &&
           SameDisplayInput(a.expression, b.expression) &&
           SameDisplayInput(a.expression_transition, b.expression_transition) && a.blink_source == b.blink_source;
}
inline bool SameVisualHitPolicy(const VisualState& a, const VisualState& b) {
    const auto& x = a.value;
    const auto& y = b.value;
    const auto& r = x.reactions;
    const auto& s = y.reactions;
    // This exception is already consumed by ReactionPolicy::SelectHit; it is
    // not a new interpretation or invocation of native modifier flags.
    const auto overrides_mute = [](const auto& row) {
        return row.force_hit_h.available && row.force_hit_h.value && row.flag.available && row.flag.value == 133;
    };
    return SameDisplayInput(x.force_hit_h, y.force_hit_h) && SameDisplayInput(x.repeat_occur_at, y.repeat_occur_at) &&
           SameDisplayInput(x.occur.state, y.occur.state) && overrides_mute(x) == overrides_mute(y) &&
           r.present == s.present && SameDisplayInput(r.hit, s.hit) && SameDisplayInput(r.hit_h, s.hit_h) &&
           SameDisplayInput(r.fly_hit, s.fly_hit) && SameDisplayInput(r.hit_fly, s.hit_fly) &&
           SameDisplayInput(r.hit_fly_recover, s.hit_fly_recover) && SameDisplayInput(r.die, s.die) &&
           SameDisplayInput(r.die_hit, s.die_hit) && SameDisplayInput(r.die_fly, s.die_fly) &&
           SameDisplayInput(r.die_limbo, s.die_limbo) && SameDisplayInput(r.force_hit_h, s.force_hit_h) &&
           SameDisplayInput(r.hit_weight, s.hit_weight);
}
}
