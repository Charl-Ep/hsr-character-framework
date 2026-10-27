#pragma once
#include "VisualProgramState.hpp"
#include <functional>

namespace hksr::framework {
// A default is local to the owning actor, or references another target input.
using VisualTargetDefault=std::variant<api::Position3,std::string>;
using VisualTargetDefaults=std::map<std::string,VisualTargetDefault>;
struct VisualEntityTarget {std::string binding;};
struct VisualPositionTarget {VisualSpatialAnchor pose;bool local=false;};
using VisualTargetBinding=std::variant<VisualEntityTarget,VisualPositionTarget>;
using VisualTargetBindings=std::map<std::string,VisualTargetBinding>;
void ValidateVisualTargetDefaults(const VisualTargetDefaults&);
VisualTargetBindings ResolveVisualTargetBindings(const VisualTargetDefaults&,
    const VisualProgramParameters&,const std::vector<std::string>& available_entities,const std::vector<std::string>& bound_entities={});
VisualSpatialAnchor TransformVisualTarget(const VisualSpatialAnchor& owner,api::Position3 local);
api::Rotation4 RotateVisualTarget(api::Rotation4,api::Position3 euler_degrees);
VisualProgramParameters VisualTargetInputs(const VisualTargetDefaults&,const VisualProgramParameters&,const std::vector<std::string>& required);
VisualSpatialAnchors CaptureVisualPositionTargets(const VisualTargetBindings&,
    const std::function<VisualSpatialAnchor()>& read_owner);
}
