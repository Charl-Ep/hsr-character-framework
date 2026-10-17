#pragma once
#include "SessionStatus.hpp"
#include "ControlValues.hpp"
#include "../assets/CharacterPackage.hpp"
#include "../presentation/HandoffStatus.hpp"
#include <hksr/model/CharacterOutfit.hpp>

namespace hksr::framework {
// Host supplies the framework persistence key and observed incarnation.
// SceneIdentity can produce these from the SDK's rooted map observations;
// this is not an official cross-version persistent identity guarantee.
struct SceneAddress { std::string key; std::uint64_t incarnation=0; };
struct PlacedAction { ActionDomain domain; ActionRequest request; };
struct CharacterPlacement { api::Position3 position; api::Rotation4 rotation; std::optional<bool> snap_to_ground; };
struct SceneCreation { std::uint64_t id; CharacterPlacement placement; };
enum class ActionIntentPhase {Waiting, Dispatching, Returned, Failed, Cancelled};
struct ActionIntentStatus {
    std::uint64_t id=0,creation=0;
    ActionIntentPhase phase=ActionIntentPhase::Waiting;
    std::optional<ActionOutcome> outcome;
    std::string detail;
};
struct SceneCharacterStatus {
    std::uint64_t id=0,creations=0;
    std::uint64_t revision=0,active_revision=0;
    // Observed loaded owner plus adopted private appearance, not GPU/visual
    // acceptance or completion of optional events, effects and actions.
    bool creation_ready=false;
    std::string creation_error;
    bool enabled=true,active=false,deleting=false;
    std::size_t retiring=0,pending_inputs=0;
    std::string scene,error;
    std::string battle_idle_state;
    std::string presentation_phase;
    std::string visual_program;
    std::map<std::string,std::string> program_actions; // action key -> body state
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
};
}
