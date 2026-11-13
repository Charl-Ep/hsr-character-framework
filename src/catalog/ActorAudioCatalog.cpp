#include <hksr/framework/catalog/ActorAudioCatalog.hpp>
#include <algorithm>

namespace hksr::framework {
bool NativeVoiceMatches(const NativeVoiceCue& row, const ActorVoiceCue& input, const std::string& event) {
    return row.source_config == input.source_config && row.state == input.state && row.cue == input.cue &&
           row.event == event;
}
bool UsesNativeVoice(const std::vector<NativeVoiceCue>& rows, const ActorVoiceCue& input, const std::string& event,
                     const std::vector<std::string>& adopted_event_files) {
    return std::any_of(rows.begin(), rows.end(), [&](const auto& row) {
        return NativeVoiceMatches(row, input, event) &&
               std::find(adopted_event_files.begin(), adopted_event_files.end(), row.event_file) !=
                   adopted_event_files.end();
    });
}
std::string NativeVoiceCoverageError(const std::vector<ActorAudioCatalog::NativeCueIssue>& issues,
                                     const ActorVoiceCue& input) {
    for (const auto& issue : issues)
        if ((issue.source_config.empty() || issue.source_config == input.source_config) &&
            (!issue.cue || *issue.cue == input.cue))
            return issue.error;
    return {};
}
}
