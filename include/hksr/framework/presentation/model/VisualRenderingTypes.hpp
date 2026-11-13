#pragma once
#include <cstdint>
#include <string>
#include <variant>
#include <vector>
namespace hksr::framework {
struct VisualRendererMask {
    std::string node;
    std::uint32_t mask = 0;
    bool enabled = true;
};
struct VisualOutlineTargets {
    bool reset = false;
    std::vector<std::string> add, remove;
};
struct VisualEntityVisible {
    std::string key;
    bool visible = true;
};
struct VisualRenderingOperation {
    std::variant<VisualRendererMask, VisualOutlineTargets, VisualEntityVisible> command;
    std::string recipient = "Caster";
};
}
