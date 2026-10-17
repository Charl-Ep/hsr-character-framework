#pragma once
#include <hksr/entity/MapNpcInputs.hpp>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace hksr::framework {
struct VisualSpatialAnchor {api::Position3 position;api::Rotation4 rotation;};
using VisualSpatialAnchors=std::map<std::string,VisualSpatialAnchor>;
struct VisualProgramParameters {
    std::map<std::string,std::uint64_t> targets;
    std::map<std::string,std::string> choices;
    VisualSpatialAnchors anchors;
};
// Value-only reconstruction inputs; no native requests, playback clocks or
// event tokens. Context order is entry order, not lexical name order.
struct VisualContextSnapshot {
    std::string name;
    VisualProgramParameters inputs;
    std::map<std::string,float> variables;
};
struct VisualProgramSnapshot {
    std::string program;
    std::optional<VisualContextSnapshot> scene;
    std::vector<VisualContextSnapshot> contexts;
};
void ValidateVisualSnapshot(const VisualProgramSnapshot&);
bool SameVisualSnapshot(const VisualProgramSnapshot&,const VisualProgramSnapshot&);
void RemapVisualTargets(VisualProgramSnapshot&,const std::map<std::uint64_t,std::uint64_t>&);
}
