#include "HitEffectsCodec.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <cmath>
#include <climits>
namespace hksr::framework::hit_effects_codec {
namespace {
using Json = nlohmann::json;
void Fields(const Json& row, std::initializer_list<const char*> names) {
    if (!row.is_object())
        throw std::invalid_argument("hit effect object expected");
    for (auto it = row.begin(); it != row.end(); ++it) {
        bool known = false;
        for (auto name : names)
            known = known || it.key() == name;
        if (!known)
            throw std::invalid_argument("unknown hit effect field: " + it.key());
    }
}
std::string Name(const Json& row) {
    auto out = row.get<std::string>();
    if (out.empty() || out.find('\0') != std::string::npos)
        throw std::invalid_argument("empty hit effect name");
    return out;
}
std::int32_t Int(const Json& row) {
    if (!row.is_number_integer() || (row.is_number_unsigned() && row.get<std::uint64_t>() > INT32_MAX) ||
        row.get<std::int64_t>() < INT32_MIN || row.get<std::int64_t>() > INT32_MAX)
        throw std::invalid_argument("hit effect integer out of range");
    return row.get<std::int32_t>();
}
}
std::vector<HitEffectOverride> Parse(const Json& rows) {
    std::vector<HitEffectOverride> out;
    for (const auto& value : rows.get<std::vector<Json>>()) {
        Fields(value, {"stacking", "path", "death_path", "priority", "count", "death_count", "offset", "keep_original",
                       "replay_on_mutex"});
        HitEffectOverride effect;
        const auto mode = Name(value.at("stacking"));
        if (mode == "multiple")
            effect.stacking = HitEffectStacking::Multiple;
        else if (mode != "highest")
            throw std::invalid_argument("unknown hit effect stacking");
        if (!value.at("path").is_null())
            effect.path = value.at("path").get<std::string>();
        effect.death_path = value.value("death_path", std::string{});
        if ((effect.path && effect.path->find('\0') != std::string::npos) ||
            effect.death_path.find('\0') != std::string::npos)
            throw std::invalid_argument("invalid hit effect source path");
        effect.priority = Int(value.at("priority"));
        effect.count = Int(value.value("count", Json(1)));
        effect.death_count = Int(value.value("death_count", Json(0)));
        effect.keep_original = value.value("keep_original", false);
        effect.replay_on_mutex = value.value("replay_on_mutex", false);
        if (value.contains("offset")) {
            const auto xyz = value.at("offset").get<std::vector<float>>();
            if (xyz.size() != 3 || !std::isfinite(xyz[0]) || !std::isfinite(xyz[1]) || !std::isfinite(xyz[2]))
                throw std::invalid_argument("invalid hit effect offset");
            effect.offset = api::Position3{xyz[0], xyz[1], xyz[2]};
        }
        out.push_back(std::move(effect));
    }
    return out;
}
Json Serialize(const std::vector<HitEffectOverride>& effects) {
    Json out = Json::array();
    for (const auto& value : effects) {
        Json row = {{"path", value.path ? Json(*value.path) : Json(nullptr)},
                    {"death_path", value.death_path},
                    {"stacking", value.stacking == HitEffectStacking::Multiple ? "multiple" : "highest"},
                    {"priority", value.priority},
                    {"count", value.count},
                    {"death_count", value.death_count},
                    {"version", value.version},
                    {"keep_original", value.keep_original},
                    {"replay_on_mutex", value.replay_on_mutex}};
        if (value.offset)
            row["offset"] = {value.offset->x, value.offset->y, value.offset->z};
        out.push_back(std::move(row));
    }
    return out;
}
}
