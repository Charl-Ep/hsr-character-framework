#pragma once
#include <hksr/framework/presentation/scene/VisualProgramState.hpp>
#include <hksr/framework/tasks/TaskControlTypes.hpp>
#include <optional>
#include <string>

namespace hksr::framework {
// Values used to reconstruct an instance, not native requests or playback clocks.
struct CharacterRestoreState {
    std::string presentation_phase;
    std::optional<VisualProgramSnapshot> visual_state;
    std::optional<TaskSelection> task_selection;
    std::optional<TaskVisualReaction> manual_reaction;
    unsigned playback_rate = 1;
};
}
