#pragma once
#include <hksr/animation/ActorTiming.hpp>
#include <string>
namespace hksr::framework {
struct VisualAnimatorSpeed {
    float speed = -1;
};
struct VisualActorTimeScale {
    std::string key;
    api::ActorTimeScaleInput input;
    bool active = true, fade = false, finite_only = false;
    std::string recipient = "Caster";
};
}
