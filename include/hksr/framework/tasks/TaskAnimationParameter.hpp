#pragma once
#include <hksr/framework/tasks/TaskModel.hpp>
#include <hksr/animation/AnimationParameter.hpp>

namespace hksr::framework {
// An instantaneous typed input, not ownership of an animation channel.
struct TaskAnimationParameterCommand {
    TaskParticipant participant;
    api::AnimationParameter parameter;
    api::AnimationParameterPropagation propagation = api::AnimationParameterPropagation::BodyOnly;
};
}
