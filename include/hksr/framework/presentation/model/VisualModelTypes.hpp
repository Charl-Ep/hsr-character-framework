#pragma once
#include <hksr/model/ActorModelPresentation.hpp>
#include <hksr/model/ModelScaleProgram.hpp>
#include <hksr/model/ActorVisualConfiguration.hpp>
#include <string>
#include <variant>

namespace hksr::framework {
struct VisualFaceMaterial {
    api::FaceMaterialTransition transition;
};
struct VisualModelScale {
    float scale = 1;
};
struct VisualModelScaleFollow {
    std::string target;
}; // empty explicitly stops following
struct VisualModelChange {
    std::string key;
    std::optional<std::string> path;
};
struct VisualModelConfiguration {
    api::ActorVisualConfigurationInput input;
};
struct VisualEmotionEnable {
    bool enabled = true;
    float transit_out = 0, transit_in = .2f;
};
struct VisualCharacterScale {
    std::vector<std::string> targets;
    api::ModelScaleInput input;
    std::string target_collection, save_targets, read_targets;
};
struct VisualModelOperation {
    std::variant<VisualFaceMaterial, VisualModelScale, VisualModelScaleFollow, VisualCharacterScale, VisualModelChange,
                 VisualModelConfiguration, VisualEmotionEnable>
        command;
};
}
