#pragma once
#include "Localization.hpp"
#include <hksr/framework/character/CharacterControls.hpp>

namespace hksr::framework::ui {
inline std::string GazeStatusText(const ControlStatus& status,Language language) {
    switch(status.gaze_phase) {
    case api::GazePhase::OutOfRange:return Translate(UiText::GazeOutOfRange,language);
    case api::GazePhase::TargetUnavailable:return Translate(UiText::GazeWaiting,language);
    case api::GazePhase::Failed:return status.gaze_error; // raw engine diagnostic
    default:return {};
    }
}
}
