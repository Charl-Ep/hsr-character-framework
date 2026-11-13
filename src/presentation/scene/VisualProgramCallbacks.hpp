#pragma once
#include <hksr/framework/presentation/effects/VisualEventRoutes.hpp>

namespace hksr::framework::detail {
struct VisualProgramCallbacks {
    api::AnimationSequenceExecutions::Observe sequence;
    api::AnimationEventConstruction::Before before_construction;
    api::AnimationEventConstruction::Tasks tasks;
    api::AnimationEventConstruction::After after_construction;
    api::EffectSubmissions::Before before_effect;
    api::EffectSubmissions::After after_effect;
};
inline VisualProgramCallbacks ConnectVisualProgramCallbacks(VisualEventRoutes& routes,
                                                            std::function<std::string(void*)> read_text,
                                                            api::EffectSubmissions::Before unrouted_effect = {},
                                                            api::EffectSubmissions::After effect_returned = {}) {
    return {[&routes](const auto& value) { routes.Observe(value); },
            [&routes, read_text = std::move(read_text)](const auto& source) {
                if (routes.Interested(source.origin) && source.file)
                    routes.Before(source, read_text(source.file));
            },
            [&routes](const auto& source, const auto& task) { routes.Built(source, task); },
            [&routes](const auto& source, bool) { routes.After(source); },
            [&routes, unrouted_effect = std::move(unrouted_effect)](const auto& value) {
                if (!routes.Submitted(value) && unrouted_effect)
                    unrouted_effect(value);
            },
            [&routes, effect_returned = std::move(effect_returned)](const auto& value, bool returned) {
                routes.SubmissionReturned(value, returned);
                if (effect_returned)
                    effect_returned(value, returned);
            }};
}
}
