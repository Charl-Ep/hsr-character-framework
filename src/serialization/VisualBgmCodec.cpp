#include "VisualBgmCodec.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace hksr::framework::visual_bgm_codec {
namespace {
using Json = nlohmann::json;
void Fields(const Json& row, std::initializer_list<const char*> allowed) {
    if (!row.is_object())
        throw std::invalid_argument("BGM sync object required");
    for (auto it = row.begin(); it != row.end(); ++it) {
        bool found = row.contains("op") && (it.key() == "as" || it.key() == "wait");
        for (const auto* name : allowed)
            found = found || it.key() == name;
        if (!found)
            throw std::invalid_argument("unknown BGM sync field: " + it.key());
    }
}
float Number(const Json& value) {
    if (!value.is_number())
        throw std::invalid_argument("BGM sync number required");
    const auto x = value.get<double>();
    if (!std::isfinite(x) || std::abs(x) > std::numeric_limits<float>::max())
        throw std::invalid_argument("invalid BGM sync number");
    return static_cast<float>(x);
}
std::string Text(const Json& value, bool empty = false) {
    const auto x = value.get<std::string>();
    if ((!empty && x.empty()) || x.find('\0') != std::string::npos)
        throw std::invalid_argument("invalid BGM sync text");
    return x;
}
std::vector<api::BgmSyncPhase> Phases(const Json& rows) {
    if (!rows.is_array())
        throw std::invalid_argument("BGM sync phases must be an array");
    std::vector<api::BgmSyncPhase> result;
    for (const auto& row : rows) {
        Fields(row, {"start", "blend", "offset"});
        result.push_back({Number(row.at("start")), Number(row.at("blend")), Number(row.at("offset"))});
    }
    return result;
}
}
std::optional<VisualBgmSync> Parse(const Json& row) {
    if (row.at("op") != "bgm.sync")
        return {};
    VisualBgmSync result;
    result.active = row.value("active", true);
    if (!result.active) {
        Fields(row, {"op", "source", "when", "context", "active"});
        return result;
    }
    Fields(row, {"op", "source", "when", "context", "active", "state", "bpm", "chapters"});
    result.config.state = Text(row.at("state"));
    result.config.bpm = Number(row.at("bpm"));
    if (result.config.bpm <= 0)
        throw std::invalid_argument("BGM sync BPM must be positive");
    const auto& chapters = row.at("chapters");
    if (!chapters.is_array() || chapters.empty())
        throw std::invalid_argument("BGM sync chapters must be nonempty");
    for (const auto& row_chapter : chapters) {
        Fields(row_chapter, {"cue", "phases", "double_speed_phases"});
        api::BgmSyncChapter chapter;
        chapter.cue = Text(row_chapter.at("cue"), true);
        chapter.phases = Phases(row_chapter.at("phases"));
        if (row_chapter.contains("double_speed_phases") && !row_chapter.at("double_speed_phases").is_null())
            chapter.double_speed_phases = Phases(row_chapter.at("double_speed_phases"));
        result.config.chapters.push_back(std::move(chapter));
    }
    return result;
}
}
