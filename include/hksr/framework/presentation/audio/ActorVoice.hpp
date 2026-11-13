#pragma once
#include <hksr/entity/EntityLifecycle.hpp>
#include <hksr/presentation/audio/VoiceCue.hpp>
#include <string>

namespace hksr::framework {
struct ActorVoiceCue {
    api::EntityOrigin actor;
    std::uint32_t avatar_id = 0;
    api::VoiceCue cue = api::VoiceCue::LightHit;
    std::string source_config, state;
};
}
