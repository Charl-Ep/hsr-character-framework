#pragma once
#include <hksr/control/ActorControlSession.hpp>
#include <optional>

namespace hksr::framework {
struct CharacterPlacement {
    api::Position3 position;
    api::Rotation4 rotation;
    std::optional<bool> snap_to_ground;
};
enum class LookMode { Automatic, Disabled, Player, Instance, Point };
struct LookIntent {
    LookMode mode = LookMode::Automatic;
    std::uint64_t target = 0;
    api::Position3 point;
    float max_distance = 0;
};
struct ControlStatus {
    api::PlacementSnapshot live;
    LookIntent look;
    bool placement_pending = false;
    api::PlacementPhase placement_phase = api::PlacementPhase::Idle;
    api::GazePhase gaze_phase = api::GazePhase::Automatic;
    std::string placement_error, gaze_error;
};
const char* PlacementPhaseName(api::PlacementPhase);
const char* GazePhaseName(api::GazePhase);
}
