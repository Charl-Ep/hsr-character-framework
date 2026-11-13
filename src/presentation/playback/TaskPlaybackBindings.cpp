#include <hksr/framework/presentation/playback/TaskPlaybackBindings.hpp>
#include <hksr/framework/presentation/playback/ActionPlayback.hpp>
#include <stdexcept>

namespace hksr::framework {
TaskAnimationEndpoint BindTaskAnimation(ActionPlayback& player) {
    return {player.Owner().generation, [&player](const auto& command) { return player.QueueAnimation(command); },
            [&player](auto serial) { player.ReleaseAnimation(serial); },
            [&player](const auto& state, float transition) { return player.QueueExpression(state, transition); },
            [&player](auto serial) { player.ReleaseExpression(serial); }};
}
PoseRequestEndpoint BindTaskPose(ActionPlayback& player, PoseResolver resolve) {
    return {player.Owner().generation,
            [&player, resolve = std::move(resolve)](const auto& selection) {
                if (!resolve)
                    throw std::runtime_error("pose source resolver unavailable");
                const auto command = resolve(selection);
                return command ? player.QueuePose(*command) : std::shared_ptr<const AnimationRequestStatus>{};
            },
            [&player](auto serial, bool cancel) { player.ReleasePose(serial, cancel); }};
}
TaskSoundEndpoint BindTaskSound(std::uint64_t generation, std::unique_ptr<api::OwnedSoundRequest> request) {
    if (!generation || !request)
        throw std::invalid_argument("sound endpoint requires owned request");
    auto shared = std::shared_ptr<api::OwnedSoundRequest>(std::move(request));
    return {generation, [shared] { shared->Service(); }, [shared] { shared->Cancel(); },
            [shared] { return shared->Status(); }};
}
}
