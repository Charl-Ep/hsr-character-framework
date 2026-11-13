#include <hksr/framework/presentation/playback/ActionPlayback.hpp>
#include "../audio/ActorVoicePlayer.hpp"
#include <stdexcept>

namespace hksr::framework {
void ActionPlayback::ConfigureVoices(std::uint32_t avatar, std::string source, VoiceFactory start, ReadyVoice ready) {
    voice_player_->Configure(avatar, std::move(source), std::move(start));
    ready_voice_ = std::move(ready);
}
void ActionPlayback::CancelVoices() {
    voice_player_->Cancel();
}
void ActionPlayback::PlayVoice(api::VoiceCue cue, const std::string& state, const std::string& source) {
    if (voice_player_->Play(cue, state, source))
        status_.voice_error = voice_player_->Error();
    status_.voice_requests = voice_player_->Count();
}
void ActionPlayback::PlayReadyVoice(const ReadyState& request, int previous_index) {
    if (!voice_player_->Configured() || !ready_voice_ || !request.actions ||
        request.pose >= request.actions->poses.size())
        return;
    const auto& pose = request.actions->poses[request.pose];
    if (previous_index == pose.skill_index)
        return;
    try {
        const api::SkillInput* selected = nullptr;
        for (const auto& skill : request.actions->skills)
            if (skill.source_index == pose.skill_index) {
                if (selected)
                    throw std::runtime_error("ready voice skill source is ambiguous");
                selected = &skill;
            }
        if (!selected || !selected->type.available)
            throw std::runtime_error("ready voice skill type unavailable");
        if (const auto cue = ready_voice_(selected->type.value))
            PlayVoice(*cue, pose.state, request.actions->source);
    } catch (const std::exception& error) {
        status_.voice_error = error.what();
    }
}
void ActionPlayback::ServiceVoices(const api::WorldFrame& frame) {
    if (const auto error = voice_player_->Service(OwnFrame(frame)))
        status_.voice_error = *error;
    status_.voice_requests = voice_player_->Count();
}
}
