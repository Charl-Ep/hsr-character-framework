#pragma once
#include <hksr/framework/presentation/scene/VisualProgramState.hpp>
#include <hksr/framework/presentation/scene/VisualStepResult.hpp>
#include <hksr/entity/EntityLifecycle.hpp>
#include <hksr/model/CharacterSkin.hpp>
#include <hksr/world/WorldPlacement.hpp>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hksr::framework {
class HitInbox;
class MovementPlacementIntent;
class TaskContextRegistry;
struct VisualProgramSignal {
    std::uint64_t generation = 0, intent = 0;
    std::string name;
};
// Shared host input services. Definitions and saved layouts contain neither callback.
struct VisualProgramProviders {
    std::function<VisualProgramParameters(std::uint64_t)> read_parameters;
    std::function<std::vector<VisualProgramSignal>(std::uint64_t)> take_signals;
};
// Owner-thread runtime bindings retained through the actor scene's resource drain.
struct VisualProgramBindings {
    std::uint64_t character = 0;
    std::shared_ptr<TaskContextRegistry> task_contexts;
    std::map<std::string, api::EntityOrigin> targets;
    std::function<api::EntityOrigin(std::uint64_t)> resolve_character;
    std::function<std::shared_ptr<HitInbox>(api::EntityOrigin)> resolve_hit_receiver;
    std::function<std::string(api::EntityOrigin)> resolve_movement_source;
    std::function<VisualStepResult(std::uint64_t)> restore_target;
    VisualProgramProviders providers;
    std::shared_ptr<MovementPlacementIntent> placement_intent;
    std::function<std::optional<api::PlacementSnapshot>()> pending_placement;
    std::shared_ptr<api::CharacterSkin> skin;
};
}
