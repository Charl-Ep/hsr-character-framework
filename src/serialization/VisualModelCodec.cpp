#include "VisualModelCodec.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <set>

namespace hksr::framework::visual_model_codec {
namespace {
using Json = nlohmann::json;
void Fields(const Json& row, std::initializer_list<const char*> allowed) {
    for (auto it = row.begin(); it != row.end(); ++it) {
        bool valid = row.contains("op") && (it.key() == "as" || it.key() == "wait");
        for (const auto* name : allowed)
            valid = valid || it.key() == name;
        if (!valid)
            throw std::invalid_argument("unknown model visual field: " + it.key());
    }
}
float Number(const Json& value) {
    if (!value.is_number())
        throw std::invalid_argument("model visual number required");
    const auto number = value.get<double>();
    if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max())
        throw std::invalid_argument("invalid model visual number");
    return static_cast<float>(number);
}
std::string Text(const Json& value, bool empty = false) {
    const auto text = value.get<std::string>();
    if ((!empty && text.empty()) || text.find('\0') != std::string::npos)
        throw std::invalid_argument("invalid model visual text");
    return text;
}
api::Position3 Vector(const Json& value) {
    if (!value.is_array() || value.size() != 3)
        throw std::invalid_argument("model visual vector requires three values");
    return {Number(value[0]), Number(value[1]), Number(value[2])};
}
}
std::optional<VisualModelOperation> Parse(const Json& row) {
    const auto op = row.at("op").get<std::string>();
    if (op == "model.change") {
        Fields(row, {"op", "source", "when", "context", "key", "path"});
        VisualModelChange change;
        change.key = Text(row.at("key"));
        if (!row.at("path").is_null())
            change.path = Text(row.at("path"));
        return VisualModelOperation{std::move(change)};
    }
    if (op == "expression.enable") {
        Fields(row, {"op", "source", "when", "context", "enabled", "transit_out", "transit_in"});
        VisualEmotionEnable value{row.at("enabled").get<bool>(), Number(row.value("transit_out", Json(0))),
                                  Number(row.value("transit_in", Json(.2)))};
        if (value.transit_out < 0 || value.transit_in < 0)
            throw std::invalid_argument("negative emotion transition time");
        return VisualModelOperation{value};
    }
    if (op == "model.configuration") {
        Fields(row, {"op", "source", "when", "context", "reset", "radius", "somato", "effect_adaptations",
                     "graph_emotion", "event_files"});
        api::ActorVisualConfigurationInput value;
        value.reset = row.value("reset", false);
        if (row.contains("radius")) {
            value.radius = Number(row.at("radius"));
            if (*value.radius < 0)
                throw std::invalid_argument("negative visual radius");
        }
        if (row.contains("somato")) {
            if (!row.at("somato").is_number_integer())
                throw std::invalid_argument("somato requires an integer");
            value.somato = row.at("somato").get<std::int32_t>();
            if (*value.somato < 0)
                throw std::invalid_argument("negative somato type");
        }
        if (row.contains("graph_emotion"))
            value.graph_emotion = row.at("graph_emotion").is_null() ? std::string{} : Text(row.at("graph_emotion"));
        if (row.contains("event_files")) {
            const auto& paths = row.at("event_files");
            if (!paths.is_array())
                throw std::invalid_argument("event_files requires an array");
            value.event_files.emplace();
            for (const auto& path : paths)
                value.event_files->push_back(Text(path));
        }
        if (row.contains("effect_adaptations")) {
            const auto& entries = row.at("effect_adaptations");
            if (!entries.is_array())
                throw std::invalid_argument("effect_adaptations requires an array");
            value.effect_adaptations.emplace();
            for (const auto& entry : entries) {
                Fields(entry, {"path", "alias", "attach_point", "position", "rotation", "scale"});
                api::EffectAdaptionEntry e;
                e.path = Text(entry.at("path"));
                if (entry.contains("alias") && !entry.at("alias").is_null())
                    e.alias = Text(entry.at("alias"), true);
                e.attach_point = Text(entry.at("attach_point"), true);
                e.position = Vector(entry.at("position"));
                e.rotation = Vector(entry.at("rotation"));
                e.scale = Number(entry.at("scale"));
                value.effect_adaptations->push_back(std::move(e));
            }
        }
        if (value.reset && (value.radius || value.somato || value.effect_adaptations || value.graph_emotion))
            throw std::invalid_argument(
                "model configuration reset cannot include overrides except explicit event files");
        return VisualModelOperation{VisualModelConfiguration{std::move(value)}};
    }
    if (op == "expression.material") {
        Fields(row, {"op", "source", "when", "context", "cheek", "shy", "shadow", "duration"});
        api::FaceMaterialTransition value{{Number(row.at("cheek")), Number(row.at("shy")), Number(row.at("shadow"))},
                                          Number(row.value("duration", Json(0)))};
        if (value.duration < 0)
            throw std::invalid_argument("face material duration must not be negative");
        return VisualModelOperation{VisualFaceMaterial{value}};
    }
    if (op == "model.scale") {
        Fields(row, {"op", "source", "when", "context", "scale"});
        return VisualModelOperation{VisualModelScale{Number(row.at("scale"))}};
    }
    if (op == "model.follow_scale") {
        Fields(row, {"op", "source", "when", "context", "target"});
        if (row.at("target").is_null())
            return VisualModelOperation{VisualModelScaleFollow{}};
        const auto target = row.at("target").get<std::string>();
        if (target.empty() || target.find('\0') != std::string::npos)
            throw std::invalid_argument("invalid model scale follow target");
        return VisualModelOperation{VisualModelScaleFollow{target}};
    }
    if (op == "model.character_scale") {
        Fields(row, {"op", "source", "when", "context", "targets", "target_collection", "read_targets", "save_targets",
                     "scale", "outline", "multiplier", "equal_ratio_by_min"});
        VisualCharacterScale value;
        value.input.base_scale = Number(row.at("scale"));
        value.input.outline_scale = Number(row.at("outline"));
        const auto name = [](const Json& item) {
            const auto text = item.get<std::string>();
            if (text.empty() || text.find('\0') != std::string::npos)
                throw std::invalid_argument("invalid character scale collection/target");
            return text;
        };
        unsigned selectors = 0;
        if (row.contains("targets")) {
            ++selectors;
            if (!row.at("targets").is_array())
                throw std::invalid_argument("character scale targets must be an array");
            for (const auto& target : row.at("targets"))
                value.targets.push_back(name(target));
        }
        if (row.contains("target_collection")) {
            ++selectors;
            value.target_collection = name(row.at("target_collection"));
        }
        if (row.contains("read_targets")) {
            ++selectors;
            value.read_targets = name(row.at("read_targets"));
        }
        if (selectors != 1)
            throw std::invalid_argument("character scale requires one explicit target selector");
        if (row.contains("save_targets"))
            value.save_targets = name(row.at("save_targets"));
        value.input.equal_ratio_by_min = row.value("equal_ratio_by_min", false);
        if (row.contains("multiplier") && !row.at("multiplier").is_null()) {
            const auto& rows = row.at("multiplier");
            if (!rows.is_array())
                throw std::invalid_argument("character scale multiplier must be ordered pairs");
            std::set<float> heights;
            for (const auto& pair : rows) {
                if (!pair.is_array() || pair.size() != 2)
                    throw std::invalid_argument("character scale multiplier pair required");
                const auto height = Number(pair[0]);
                if (!heights.insert(height).second)
                    throw std::invalid_argument("duplicate character scale multiplier height");
                value.input.multiplier.push_back({height, Number(pair[1])});
            }
        }
        return VisualModelOperation{std::move(value)};
    }
    return {};
}
}
