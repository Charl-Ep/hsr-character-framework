#include "../../../src/presentation/scene/VisualProgramCallbacks.hpp"
#include <stdexcept>
#include <vector>

namespace {
void Check(bool value) {
    if (!value)
        throw std::runtime_error("visual program callback assembly test failed");
}
}
void TestVisualProgramObservers() {
    using namespace hksr::api;
    using namespace hksr::framework;
    int actor, world, manager, parameters;
    EffectSubmission submission;
    submission.serial = 23;
    submission.origin = {{reinterpret_cast<GameEntity*>(&actor), 1}, reinterpret_cast<GameWorld*>(&world)};
    submission.manager = &manager;
    submission.request = {17, 0, &parameters};
    VisualEventRoutes routes([](const auto&) { return AnimationEventDefinitions{}; },
                             [](const auto&, const auto&) { return AnimationSequenceTracking::Unavailable; });
    unsigned before = 0;
    std::vector<bool> returned;
    auto same_submission = [&](const EffectSubmission& value) {
        Check(value.serial == submission.serial && value.origin.token.entity == submission.origin.token.entity &&
              value.origin.token.generation == submission.origin.token.generation &&
              value.origin.world == submission.origin.world && value.manager == submission.manager &&
              value.request.id == submission.request.id && value.request.parameters == submission.request.parameters);
    };
    auto callbacks = detail::ConnectVisualProgramCallbacks(
        routes, [](void*) { return std::string{}; },
        [&](const auto& value) {
            same_submission(value);
            ++before;
        },
        [&](const auto& value, bool original_returned) {
            same_submission(value);
            returned.push_back(original_returned);
        });
    Check(callbacks.sequence && callbacks.before_construction && callbacks.tasks && callbacks.after_construction &&
          callbacks.before_effect && callbacks.after_effect);
    callbacks.before_effect(submission);
    callbacks.after_effect(submission, true);
    callbacks.before_effect(submission);
    callbacks.after_effect(submission, false);
    Check(before == 2 && returned == std::vector<bool>({true, false}));
    routes.Clear();
}
