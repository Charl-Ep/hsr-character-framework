#pragma once
#include <hksr/framework/presentation/reactions/DeathOccurrence.hpp>
#include <cstdint>
#include <string>
#include <variant>

namespace hksr::framework {
struct NormalBodyAccepted {};
struct ReactionEntered {
    std::string source;
    std::int32_t flag = 0;
    DeathOccurrence occurrence = DeathOccurrence::Configured;
};
struct ReactionRecovered {
    std::string source;
    std::int32_t flag = 0;
};
// A successfully accepted playback operation, not a queued intent or a
// persistent script-context contribution. Consumers apply it at most once.
using AcceptedPlaybackEffect = std::variant<NormalBodyAccepted, ReactionEntered, ReactionRecovered>;
}
