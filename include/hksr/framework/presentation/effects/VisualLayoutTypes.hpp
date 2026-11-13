#pragma once
#include <hksr/presentation/effects/EffectLayouts.hpp>
#include <string>
namespace hksr::framework {
struct VisualEffectLayout {
    std::string effect, target;
    api::EffectLayoutAction action = api::EffectLayoutAction::Hide;
    bool fade = false;
    api::EffectLayoutPerform perform = api::EffectLayoutPerform::AddImmediately;
};
}
