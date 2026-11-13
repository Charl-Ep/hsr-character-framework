#include <hksr/framework/layout/SceneLayout.hpp>
#include "TaskSelectionCodec.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace hksr::framework {
namespace {
using Json = nlohmann::json;
void Need(bool value, const char* message) {
    if (!value)
        throw std::invalid_argument(message);
}
int Variant(const Json& value) {
    Need(value.is_number_integer(), "saved variant must be an integer");
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        Need(number <= static_cast<std::uint64_t>((std::numeric_limits<int>::max)()), "saved variant exceeds int32");
        return static_cast<int>(number);
    }
    const auto number = value.get<std::int64_t>();
    Need(number >= -1 && number <= (std::numeric_limits<int>::max)(), "saved variant outside supported range");
    return static_cast<int>(number);
}
Json Position(api::Position3 p) {
    return {{"x", p.x}, {"y", p.y}, {"z", p.z}};
}
api::Position3 Position(const Json& j) {
    return {j.at("x").get<float>(), j.at("y").get<float>(), j.at("z").get<float>()};
}
std::uint64_t Id(const Json& j) {
    const auto text = j.get<std::string>();
    std::size_t end = 0;
    Need(!text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }),
         "invalid layout key");
    const auto id = std::stoull(text, &end);
    Need(end == text.size(), "invalid layout key");
    return id;
}
const char* Domain(ActionDomain d) {
    return d == ActionDomain::Battle ? "battle" : "exploration";
}
ActionDomain Domain(const std::string& d) {
    Need(d == "battle" || d == "exploration", "invalid action domain");
    return d == "battle" ? ActionDomain::Battle : ActionDomain::Exploration;
}
const char* Look(LookMode mode) {
    switch (mode) {
    case LookMode::Automatic:
        return "automatic";
    case LookMode::Disabled:
        return "disabled";
    case LookMode::Player:
        return "player";
    case LookMode::Instance:
        return "instance";
    case LookMode::Point:
        return "point";
    }
    throw std::invalid_argument("invalid gaze mode");
}
LookMode Look(const std::string& text) {
    for (auto mode : {LookMode::Automatic, LookMode::Disabled, LookMode::Player, LookMode::Instance, LookMode::Point})
        if (text == Look(mode))
            return mode;
    throw std::invalid_argument("invalid gaze mode");
}
Json Context(const VisualContextSnapshot& value) {
    Json targets = Json::object(), anchors = Json::object(), positions = Json::object();
    for (const auto& [name, id] : value.inputs.targets)
        targets[name] = id ? Json(std::to_string(id)) : Json(nullptr);
    for (const auto& [name, pose] : value.inputs.anchors) {
        const auto q = pose.rotation;
        anchors[name] = {{"position", Position(pose.position)}, {"rotation", {q.x, q.y, q.z, q.w}}};
    }
    for (const auto& [name, pose] : value.inputs.position_targets) {
        const auto q = pose.rotation;
        positions[name] = {{"position", Position(pose.position)}, {"rotation", {q.x, q.y, q.z, q.w}}};
    }
    Json out = {{"name", value.name},
                {"choices", value.inputs.choices},
                {"variables", value.variables},
                {"targets", targets},
                {"anchors", anchors},
                {"position_targets", positions},
                {"target_aliases", value.inputs.target_aliases},
                {"target_collections", value.inputs.target_collections}};
    if (value.instance)
        out["instance"] = std::to_string(value.instance);
    if (!value.effects.empty())
        out["effects"] = value.effects;
    if (!value.numbers.empty()) {
        out["numbers"] = Json::object();
        for (const auto& [name, number] : value.numbers)
            std::visit([&](const auto n) { out["numbers"][name] = n; }, number);
    }
    if (!value.values.empty()) {
        out["values"] = Json::object();
        for (const auto& [name, item] : value.values)
            std::visit([&](const auto& v) { out["values"][name] = v; }, item);
    }
    if (value.binding)
        out["binding"] = {{"name", value.binding->name},
                          {"category", value.binding->category},
                          {"ability", value.binding->ability},
                          {"receiver", std::to_string(value.binding->receiver)},
                          {"caster", std::to_string(value.binding->caster)}};
    return out;
}
VisualContextSnapshot Context(const Json& value) {
    VisualContextSnapshot out;
    out.name = value.at("name").get<std::string>();
    if (value.contains("effects"))
        out.effects = value.at("effects").get<std::vector<std::string>>();
    if (value.contains("instance"))
        out.instance = Id(value.at("instance"));
    if (value.contains("binding")) {
        const auto& binding = value.at("binding");
        out.binding = VisualContextBinding{
            binding.at("name").get<std::string>(), binding.at("category").get<std::string>(),
            binding.at("ability").get<std::string>(), Id(binding.at("receiver")), Id(binding.at("caster"))};
    }
    out.inputs.choices = value.at("choices").get<std::map<std::string, std::string>>();
    out.inputs.target_aliases = value.value("target_aliases", std::map<std::string, std::string>{});
    out.inputs.target_collections =
        value.value("target_collections", std::map<std::string, std::vector<std::string>>{});
    out.variables = value.at("variables").get<std::map<std::string, float>>();
    if (value.contains("values")) {
        const auto& values = value.at("values");
        Need(values.is_object(), "invalid saved context values");
        for (auto it = values.begin(); it != values.end(); ++it) {
            const auto& item = it.value();
            if (item.is_boolean())
                out.values[it.key()] = item.get<bool>();
            else if (item.is_number_integer()) {
                if (item.is_number_unsigned())
                    Need(item.get<std::uint64_t>() <= INT64_MAX, "saved context integer out of range");
                out.values[it.key()] = item.get<std::int64_t>();
            } else if (item.is_number_float())
                out.values[it.key()] = item.get<double>();
            else {
                Need(item.is_string(), "saved context scalar expected");
                out.values[it.key()] = item.get<std::string>();
            }
        }
    }
    if (value.contains("numbers")) {
        Need(value.at("numbers").is_object(), "invalid saved context numbers");
        for (auto it = value.at("numbers").begin(); it != value.at("numbers").end(); ++it) {
            if (it.value().is_number_integer()) {
                if (it.value().is_number_unsigned())
                    Need(it.value().get<std::uint64_t>() <= INT64_MAX, "saved context integer out of range");
                out.numbers[it.key()] = it.value().get<std::int64_t>();
            } else {
                Need(it.value().is_number_float(), "saved context number expected");
                out.numbers[it.key()] = it.value().get<double>();
            }
        }
    }
    Need(value.at("targets").is_object() && value.at("anchors").is_object(), "invalid visual inputs");
    for (auto it = value.at("targets").begin(); it != value.at("targets").end(); ++it)
        out.inputs.targets[it.key()] = it.value().is_null() ? 0 : Id(it.value());
    for (const auto* field : {"anchors", "position_targets"}) {
        if (!value.contains(field))
            continue;
        Need(value.at(field).is_object(), "invalid visual poses");
        auto& poses = std::string(field) == "anchors" ? out.inputs.anchors : out.inputs.position_targets;
        for (auto it = value.at(field).begin(); it != value.at(field).end(); ++it) {
            const auto& row = it.value();
            const auto& q = row.at("rotation");
            Need(q.is_array() && q.size() == 4, "invalid visual anchor rotation");
            poses[it.key()] = {Position(row.at("position")),
                               {q[0].get<float>(), q[1].get<float>(), q[2].get<float>(), q[3].get<float>()}};
        }
    }
    return out;
}
Json ProgramSnapshot(const VisualProgramSnapshot& state) {
    Json out = {{"program", state.program}, {"contexts", Json::array()}};
    if (state.scene)
        out["scene"] = Context(*state.scene);
    for (const auto& value : state.contexts)
        out["contexts"].push_back(Context(value));
    return out;
}
VisualProgramSnapshot ProgramSnapshot(const Json& json) {
    VisualProgramSnapshot out;
    out.program = json.at("program").get<std::string>();
    Need(json.at("contexts").is_array(), "visual contexts must retain their entry order");
    if (json.contains("scene"))
        out.scene = Context(json.at("scene"));
    for (const auto& value : json.at("contexts"))
        out.contexts.push_back(Context(value));
    return out;
}
}
std::string EncodeSceneLayout(const SceneLayout& layout) {
    ValidateSceneLayout(layout);
    Json out = {
        {"format", "hksr.scene-layout"}, {"version", 1}, {"scene", layout.scene}, {"characters", Json::array()}};
    for (const auto& row : layout.characters) {
        Json source;
        if (const auto* npc = std::get_if<BrowseNpc>(&row.source))
            source = {{"kind", "npc"}, {"npc_id", npc->id}};
        else if (const auto* skin = std::get_if<BrowseSkin>(&row.source))
            source = {{"kind", "skin"}, {"skin_id", skin->skin_id}};
        else {
            const auto& player = std::get<BrowsePlayer>(row.source);
            source = {{"kind", "player"},
                      {"player_id", player.key.id},
                      {"enhanced", player.key.enhanced},
                      {"skin", player.key.skin}};
        }
        const auto q = row.placement.rotation;
        Json value = {{"key", std::to_string(row.key)},
                      {"source", source},
                      {"enabled", row.enabled},
                      {"playback_rate", row.restore.playback_rate},
                      {"presentation_phase", row.restore.presentation_phase},
                      {"position", Position(row.placement.position)},
                      {"rotation", {{"x", q.x}, {"y", q.y}, {"z", q.z}, {"w", q.w}}},
                      {"look",
                       {{"mode", Look(row.look.mode)},
                        {"target", std::to_string(row.look.target)},
                        {"point", Position(row.look.point)},
                        {"max_distance", row.look.max_distance}}}};
        if (row.placement.snap_to_ground)
            value["snap_to_ground"] = *row.placement.snap_to_ground;
        if (row.restore.visual_state)
            value["visual_state"] = ProgramSnapshot(*row.restore.visual_state);
        if (row.restore.manual_reaction)
            value["visual_reaction"] = {{"source", row.restore.manual_reaction->source},
                                        {"flag", row.restore.manual_reaction->flag},
                                        {"occurrence", DeathOccurrenceName(row.restore.manual_reaction->occurrence)}};
        if (!row.task_catalog.empty())
            value["task_catalog"] = row.task_catalog;
        if (row.restore.task_selection) {
            value["task"] = task_codec::Selection(*row.restore.task_selection);
            value["task"]["captured"] = row.restore.task_selection->inputs_captured;
        }
        if (row.action)
            value["action"] = std::visit(
                [](const auto& action) -> Json {
                    using T = std::decay_t<decltype(action)>;
                    if constexpr (std::is_same_v<T, NpcMotion>)
                        return {{"kind", "npc"}, {"id", action.id}};
                    else if constexpr (std::is_same_v<T, BattleIdle>)
                        return {{"kind", "battle_idle"}};
                    else if constexpr (std::is_same_v<T, VisualActionKey>)
                        return {{"kind", "visual_behavior"},
                                {"source", action.character_config},
                                {"flag", action.flag},
                                {"recover", action.recover},
                                {"occurrence", DeathOccurrenceName(action.occurrence)}};
                    else if constexpr (std::is_same_v<T, SavedConfiguredAction>)
                        return {{"kind", "configured"},
                                {"domain", Domain(action.domain)},
                                {"state", action.state},
                                {"controller_observed", action.controller_observed}};
                    else if constexpr (std::is_same_v<T, ProgramAction>)
                        return {{"kind", "program"},
                                {"program", action.program},
                                {"action", action.action},
                                {"choices", action.choices}};
                    else
                        return {{"kind", "ready"},
                                {"source", action.source},
                                {"state", action.state},
                                {"skill", action.skill},
                                {"variant", action.variant}};
                },
                *row.action);
        out["characters"].push_back(std::move(value));
    }
    return out.dump(2);
}
SceneLayout DecodeSceneLayout(const std::string& text) {
    const auto data = Json::parse(text);
    Need(data.at("format") == "hksr.scene-layout" && data.at("version") == 1, "unsupported layout format/version");
    SceneLayout out;
    out.scene = data.at("scene").get<std::string>();
    Need(data.at("characters").is_array(), "layout characters must be an array");
    for (const auto& item : data.at("characters")) {
        SavedCharacter row;
        row.key = Id(item.at("key"));
        row.enabled = item.at("enabled").get<bool>();
        if (const auto rate = item.find("playback_rate"); rate != item.end()) {
            Need(rate->is_number_integer() && *rate >= 1 && *rate <= 2, "playback rate must be 1 or 2");
            row.restore.playback_rate = rate->get<unsigned>();
        }
        row.restore.presentation_phase = item.value("presentation_phase", std::string{});
        if (item.contains("visual_state"))
            row.restore.visual_state = ProgramSnapshot(item.at("visual_state"));
        if (item.contains("visual_reaction")) {
            const auto& reaction = item.at("visual_reaction");
            const auto& flag = reaction.at("flag");
            Need(flag.is_number_integer() && (flag.is_number_unsigned() ? flag.get<std::uint64_t>() <= INT32_MAX
                                                                        : flag.get<std::int64_t>() >= INT32_MIN &&
                                                                              flag.get<std::int64_t>() <= INT32_MAX),
                 "invalid saved visual reaction flag");
            row.restore.manual_reaction =
                TaskVisualReaction{reaction.at("source").get<std::string>(), flag.get<std::int32_t>(),
                                   ParseDeathOccurrence(reaction.value("occurrence", std::string("default")))};
        }
        row.task_catalog = item.value("task_catalog", std::string{});
        if (item.contains("task")) {
            row.restore.task_selection = task_codec::ParseSelection(item.at("task"));
            row.restore.task_selection->inputs_captured = item.at("task").value("captured", false);
        }
        const auto& source = item.at("source");
        auto number = [](const Json& n) {
            Need(n.is_number_unsigned() && n.get<std::uint64_t>() <= UINT32_MAX, "invalid source/action ID");
            return n.get<std::uint32_t>();
        };
        if (source.at("kind") == "npc")
            row.source = BrowseNpc{number(source.at("npc_id"))};
        else if (source.at("kind") == "skin")
            row.source = BrowseSkin{number(source.at("skin_id"))};
        else {
            Need(source.at("kind") == "player", "unsupported layout source");
            BrowsePlayer player{
                {number(source.at("player_id")), number(source.at("enhanced")), number(source.at("skin"))}};
            row.source = player;
        }
        row.placement.position = Position(item.at("position"));
        const auto& q = item.at("rotation");
        row.placement.rotation = {q.at("x").get<float>(), q.at("y").get<float>(), q.at("z").get<float>(),
                                  q.at("w").get<float>()};
        if (item.contains("snap_to_ground"))
            row.placement.snap_to_ground = item.at("snap_to_ground").get<bool>();
        const auto& look = item.at("look");
        row.look = {Look(look.at("mode").get<std::string>()), Id(look.at("target")), Position(look.at("point"))};
        row.look.max_distance = look.value("max_distance", 0.0f);
        if (item.contains("action")) {
            const auto& a = item.at("action");
            if (a.at("kind") == "npc")
                row.action = NpcMotion{number(a.at("id"))};
            else if (a.at("kind") == "battle_idle")
                row.action = BattleIdle{};
            else if (a.at("kind") == "visual_behavior") {
                const auto& flag = a.at("flag");
                Need(flag.is_number_integer() &&
                         (flag.is_number_unsigned()
                              ? flag.get<std::uint64_t>() <= INT32_MAX
                              : flag.get<std::int64_t>() >= INT32_MIN && flag.get<std::int64_t>() <= INT32_MAX),
                     "invalid visual behavior flag");
                row.action = VisualActionKey{a.at("source").get<std::string>(), flag.get<std::int32_t>(),
                                             a.at("recover").get<bool>(),
                                             ParseDeathOccurrence(a.value("occurrence", std::string("default")))};
            } else if (a.at("kind") == "program")
                row.action = ProgramAction{a.at("program").get<std::string>(), a.at("action").get<std::string>(),
                                           a.value("choices", std::map<std::string, std::string>{})};
            else if (a.at("kind") == "configured")
                row.action =
                    SavedConfiguredAction{Domain(a.at("domain").get<std::string>()), a.at("state").get<std::string>(),
                                          a.value("controller_observed", false)};
            else {
                Need(a.at("kind") == "ready", "unsupported saved action");
                row.action = SavedReadyAction{a.at("source").get<std::string>(), a.at("state").get<std::string>(),
                                              a.at("skill").get<std::string>(), Variant(a.at("variant"))};
            }
        }
        out.characters.push_back(std::move(row));
    }
    ValidateSceneLayout(out);
    return out;
}
}
