#pragma once
#include <hksr/framework/character/SceneAddress.hpp>
#include <hksr/framework/character/SessionStatus.hpp>
#include <hksr/framework/character/ControlValues.hpp>
#include <hksr/framework/assets/CharacterPackage.hpp>
#include <hksr/framework/presentation/model/HandoffStatus.hpp>
#include <hksr/framework/presentation/scene/VisualProgramState.hpp>
#include <hksr/framework/tasks/TaskControlTypes.hpp>
#include <hksr/model/CharacterOutfit.hpp>

namespace hksr::framework {
struct SceneCreation {
    std::uint64_t id;
    CharacterPlacement placement;
};
struct ActionIntentStatus {
    std::uint64_t id = 0, creation = 0;
    ActionIntentPhase phase = ActionIntentPhase::Waiting;
    std::optional<ActionOutcome> outcome;
    std::string detail;
};
struct SceneCharacterStatus {
    std::uint64_t id = 0, creations = 0;
    std::uint64_t revision = 0, active_revision = 0;
    // Observed loaded owner plus adopted private appearance, not GPU/visual
    // acceptance or completion of optional events, effects and actions.
    bool creation_ready = false;
    std::string creation_error;
    bool enabled = true, active = false, deleting = false;
    std::size_t retiring = 0, pending_inputs = 0;
    std::string scene, error;
    std::string battle_idle_state;
    std::string presentation_phase;
    bool visual_phase_override = false;
    VisualBranchState branch_state;
    std::string visual_program;
    std::map<std::string, std::string> program_actions; // action key -> body state
    std::optional<CharacterSessionStatus> session;
    CharacterPackage package;
    CharacterPlacement placement;
    LookIntent look;
    std::optional<HandoffStatus> handoff;
    std::optional<ActionPlaybackStatus> playback;
    std::optional<ControlStatus> controls;
    std::string look_support;
    std::optional<api::CharacterOutfitStatus> outfit;
    std::optional<ActionIntentStatus> action;
    bool transient = false;
    std::string task_catalog;
    std::uint64_t task_catalog_revision = 0;
    std::shared_ptr<const CharacterTaskStatus> tasks;
    std::uint32_t skin_id = 0;
    unsigned playback_rate = 1;
    std::string playback_error;
};
}
