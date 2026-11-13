#include <hksr/framework/catalog/ActorAudioCatalog.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <set>
#include <map>
#include <algorithm>
#include <stdexcept>

namespace hksr::framework {
ActorAudioCatalog ReadActorAudioCatalog(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("cannot open actor audio catalog: " + path.string());
    const auto data = nlohmann::json::parse(input);
    if (data.at("format") != "hksr.actor-audio-events" || !data.at("events").is_array())
        throw std::invalid_argument("invalid actor audio catalog");
    auto id = [](const auto& value) {
        if (!value.is_number_integer() || value.template get<std::int64_t>() <= 0 ||
            value.template get<std::uint64_t>() > UINT32_MAX)
            throw std::invalid_argument("audio node identifiers must be nonzero uint32");
        return value.template get<std::uint32_t>();
    };
    ActorAudioCatalog out;
    std::map<std::string, std::size_t> names;
    std::set<std::string> conflicts;
    std::size_t index = 0;
    for (const auto& row : data.at("events")) {
        api::ActorAudioEvent event;
        const auto location = "events[" + std::to_string(index++) + "]";
        try {
            event.event = row.at("event").get<std::string>();
            if (event.event.empty() || event.event.find('\0') != std::string::npos)
                throw std::invalid_argument("invalid actor audio event name");
            event.normal_node = id(row.at("normal").at("node_id"));
            event.doubled_node = id(row.at("doubled").at("node_id"));
            if (conflicts.count(event.event))
                continue;
            const auto found = names.find(event.event);
            if (found != names.end()) {
                auto& previous = out.events[found->second];
                if (previous.normal_node != event.normal_node || previous.doubled_node != event.doubled_node) {
                    conflicts.insert(event.event);
                    previous.event.clear();
                    out.issues.push_back({location, event.event, "conflicting actor audio event mappings removed"});
                }
            } else {
                names.emplace(event.event, out.events.size());
                out.events.push_back(std::move(event));
            }
        } catch (const std::exception& error) {
            out.issues.push_back({location, event.event, error.what()});
        }
    }
    out.events.erase(
        std::remove_if(out.events.begin(), out.events.end(), [](const auto& event) { return event.event.empty(); }),
        out.events.end());
    auto text = [](const auto& row, const char* name) -> std::string {
        if (!row.is_object())
            return {};
        const auto value = row.find(name);
        if (value == row.end() || !value->is_string())
            return {};
        const auto result = value->template get<std::string>();
        return result.find('\0') == std::string::npos ? result : std::string{};
    };
    auto cue = [](const std::string& kind) -> std::optional<api::VoiceCue> {
        if (kind == "light_hit")
            return api::VoiceCue::LightHit;
        if (kind == "ultra_ready")
            return api::VoiceCue::UltraReady;
        return {};
    };
    if (data.contains("native_cues") && !data.at("native_cues").is_array()) {
        out.native_cue_issues.push_back({{}, {}, "native voice coverage is not an array"});
    } else if (data.contains("native_cues")) {
        index = 0;
        for (const auto& row : data.at("native_cues")) {
            const auto location = "native_cues[" + std::to_string(index++) + "]";
            NativeVoiceCue entry;
            entry.source_config = text(row, "source_config");
            const auto kind = cue(text(row, "cue"));
            try {
                entry.event_file = text(row, "event_file");
                entry.state = text(row, "state");
                entry.event = text(row, "event");
                if (!kind)
                    throw std::invalid_argument("unknown native voice cue");
                entry.cue = *kind;
                for (const auto* value : {&entry.source_config, &entry.event_file, &entry.state, &entry.event})
                    if (value->empty())
                        throw std::invalid_argument("native voice cue requires source, file, state and event");
                out.native_cues.push_back(std::move(entry));
            } catch (const std::exception& error) {
                out.native_cue_issues.push_back({entry.source_config, kind, location + ": " + error.what()});
            }
        }
    }
    return out;
}
}
