#pragma once
#include <hksr/presentation/events/AnimationEventDefinitions.hpp>
#include <map>

namespace hksr::framework {
// Authored source selection. State membership is matched as a complete set.
// scope selects a program-owned context; key groups receipts inside it.
struct VisualEventRule {
    std::string file;
    api::AnimationEventPart part=api::AnimationEventPart::TimePoint;
    std::vector<std::string> states;
    std::string behavior,key,scope;
    // Exact original TriggerEffect.EffectPath -> program-local key.
    std::map<std::string,std::string> effect_keys;
};
}
