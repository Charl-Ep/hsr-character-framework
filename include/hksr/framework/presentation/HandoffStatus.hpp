#pragma once
#include "ActionTypes.hpp"
#include <hksr/model/ModelTransition.hpp>

namespace hksr::framework {
using HandoffPhase=api::ModelTransitionPhase;
struct HandoffStatus {
    std::uint64_t requested=0,servicing=0;
    // Playback output belongs to servicing, never to a newer queued request
    // or the internal initialization/default-state playback.
    std::uint64_t playback_serial=0;
    std::optional<ActionReceipt> output;
    HandoffPhase phase=HandoffPhase::Ready;
    std::optional<ActionDomain> ready_domain=ActionDomain::Exploration;
    std::string error;
    std::string initialization_note;
    std::string event_value_note;
    api::AnimatorStateObservation initial_state;
    bool closed=false;
};
}
