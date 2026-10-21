#pragma once
#include <hksr/entity/MapNpcInputs.hpp>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <variant>

namespace hksr::framework {
struct VisualSpatialAnchor {api::Position3 position;api::Rotation4 rotation;};
using VisualSpatialAnchors=std::map<std::string,VisualSpatialAnchor>;
VisualSpatialAnchors ParseVisualSpatialAnchors(const std::string& json);
struct VisualProgramParameters {
    std::map<std::string,std::uint64_t> targets; // zero explicitly leaves an alias unbound
    std::map<std::string,std::string> choices;
    VisualSpatialAnchors anchors;
};
// Current presentation inputs, separate from a persistence recipe or an
// animation receipt. Scope distinguishes values retained by named contexts.
struct VisualBranchValue {std::string scope,name;std::variant<std::string,float> value;};
struct VisualBranchState {std::string phase;std::vector<VisualBranchValue> values;};
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
