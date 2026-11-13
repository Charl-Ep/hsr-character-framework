#pragma once
#include <hksr/framework/presentation/timing/VisualTimingTypes.hpp>
#include <hksr/framework/presentation/model/VisualModelTypes.hpp>
#include <hksr/framework/presentation/model/VisualRenderingTypes.hpp>
#include <hksr/framework/presentation/effects/VisualLayoutTypes.hpp>
#include <hksr/framework/presentation/audio/VisualBgmTypes.hpp>
#include <hksr/framework/presentation/scene/VisualTargets.hpp>
#include <hksr/framework/presentation/partners/VisualPartnerTypes.hpp>
#include <hksr/presentation/effects/EffectAnimatorParameters.hpp>
#include <hksr/presentation/effects/EffectActions.hpp>
#include <array>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace hksr::framework {
struct VisualCreate {
    std::vector<std::string> effects;
};
struct VisualAssign {
    std::string variable;
    float value = 0;
};
struct VisualProgress {
    std::string effect;
    std::variant<float, std::string> value;
    float seconds = 0;
};
struct VisualRemove {
    std::vector<std::string> effects;
    bool fade = true, immediate = false;
    std::set<std::string> optional; // absent in some authored branches
};
struct VisualRemoveEventEffect {
    std::string path;
    api::EffectRemovalOptions options;
};
struct VisualTrigger {
    std::vector<std::string> effects;
    std::string trigger;
    bool reset = false;
};
struct VisualFollow {
    std::string effect;
    std::vector<std::string> targets;
}; // empty releases this binding
struct VisualFace {
    std::string target;
    float ratio = 1, angle_offset = 0, duration = -1;
};
struct VisualAttachmentVisibility {
    std::vector<std::string> points;
    bool visible = true;
    int delay = 0;
}; // owned body only
struct VisualAttachmentScale {
    std::vector<std::string> points;
    std::array<float, 3> scale{1, 1, 1};
};
struct VisualLayerWeight {
    std::string layer;
    float weight = 0, duration = 0;
};
struct VisualEffectState {
    std::string effect, state;
    float transition = 0;
    bool restart = false;
};
struct VisualEffectParameter {
    std::string effect;
    api::EffectAnimatorParameter parameter;
};
struct VisualContextOperation {
    std::string name;
    bool enter = true;
    bool fade = true;
};
struct VisualTimeline {
    std::string key, path;
    bool play = true;
    int type = 0, wrap_mode = 0;
    bool release_volume = false;
    bool shared_environment = false;
};
using VisualOperation =
    std::variant<VisualCreate, VisualAssign, VisualProgress, VisualRemove, VisualRemoveEventEffect, VisualTrigger,
                 VisualTimeline, VisualFollow, VisualEffectState, VisualContextOperation, VisualEffectParameter,
                 VisualPartnerOperation, VisualFace, VisualAttachmentVisibility, VisualAttachmentScale,
                 VisualLayerWeight, VisualAnimatorSpeed, VisualActorTimeScale, VisualModelOperation,
                 VisualRenderingOperation, VisualEffectLayout, VisualBgmSync>;
struct VisualInstruction {
    VisualOperation operation;
    std::string source;                  // original configuration node, for diagnostics
    std::string choice, value;           // optional explicit preview input, not a gameplay flag
    std::string context;                 // empty uses the current execution's effect owner
    VisualTargetAliases target_bindings; // lexical input names; resources keep their existing owner
};
}
