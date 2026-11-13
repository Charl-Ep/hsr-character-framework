#pragma once
#include <hksr/framework/tasks/TaskPresentation.hpp>

namespace hksr::framework {
class ActionPlayback;
// Composition adapters. The playback owner must outlive the task's drain.
TaskAnimationEndpoint BindTaskAnimation(ActionPlayback&);
PoseRequestEndpoint BindTaskPose(ActionPlayback&, PoseResolver);
TaskSoundEndpoint BindTaskSound(std::uint64_t, std::unique_ptr<api::OwnedSoundRequest>);
}
