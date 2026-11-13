#pragma once
#include <hksr/presentation/audio/ActorAudio.hpp>
#include <hksr/framework/presentation/audio/ActorVoice.hpp>
#include <filesystem>
#include <vector>
#include <optional>

namespace hksr::framework {
struct NativeVoiceCue {
    std::string source_config, event_file, state, event;
    api::VoiceCue cue = api::VoiceCue::LightHit;
};
struct ActorAudioCatalog {
    std::vector<api::ActorAudioEvent> events;
    std::vector<NativeVoiceCue> native_cues;
    struct Issue {
        std::string location, event, error;
    };
    std::vector<Issue> issues;
    struct NativeCueIssue {
        std::string source_config;
        std::optional<api::VoiceCue> cue;
        std::string error;
    };
    std::vector<NativeCueIssue> native_cue_issues;
};
ActorAudioCatalog ReadActorAudioCatalog(const std::filesystem::path&);
bool NativeVoiceMatches(const NativeVoiceCue&, const ActorVoiceCue&, const std::string& event);
bool UsesNativeVoice(const std::vector<NativeVoiceCue>&, const ActorVoiceCue&, const std::string& event,
                     const std::vector<std::string>& adopted_event_files);
std::string NativeVoiceCoverageError(const std::vector<ActorAudioCatalog::NativeCueIssue>&, const ActorVoiceCue&);
}
