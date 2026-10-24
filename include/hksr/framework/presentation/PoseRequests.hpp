#pragma once
#include "ActionTypes.hpp"
#include "AnimationRequests.hpp"

namespace hksr::framework {
struct ReadyPoseSelection {std::string source;int skill=-1,variant=-1;};
struct ReactionSelection {std::string source;std::int32_t flag=0;bool recover=false;};
using PoseSelection=std::variant<ReadyPoseSelection,ReactionSelection>;
using PoseCommand=std::variant<ReadyState,VisualState>;
using PoseResolver=std::function<std::optional<PoseCommand>(const PoseSelection&)>;
// Ready completes when its destination (or authored successor) is observed.
// A reaction holds until recovery/replacement; recovery completes at its end.
// Composite poses expose no single normalized progress clock.
struct PoseRequestEndpoint {
    std::uint64_t entity_generation=0;
    std::function<std::shared_ptr<const AnimationRequestStatus>(const PoseSelection&)> submit;
    std::function<void(std::uint64_t,bool cancel)> release;
};
}
