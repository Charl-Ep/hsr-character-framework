#include <hksr/framework/presentation/scene/VisualProgram.hpp>
#include "VisualCodec.hpp"
#include "VisualModelCodec.hpp"
#include "VisualLayoutCodec.hpp"
#include "VisualBgmCodec.hpp"
#include <hksr/framework/presentation/scene/VisualProgramLibrary.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace hksr::framework {
namespace {
using Json = nlohmann::json;
void Need(bool condition, const std::string& message) {
    if (!condition)
        throw std::invalid_argument(message);
}
void Fields(const Json& row, std::initializer_list<const char*> names) {
    Need(row.is_object(), "visual entry must be an object");
    for (auto i = row.begin(); i != row.end(); ++i) {
        bool known = false;
        for (const auto* name : names)
            known = known || i.key() == name;
        Need(known, "unknown visual field: " + i.key());
    }
}
std::string Name(const Json& value) {
    const auto text = value.get<std::string>();
    Need(!text.empty() && text.find('\0') == std::string::npos, "invalid visual name");
    return text;
}
float Number(const Json& value, bool nonnegative = false) {
    Need(value.is_number(), "visual number expected");
    const auto n = value.get<float>();
    Need(std::isfinite(n) && (!nonnegative || n >= 0), "invalid visual number");
    return n;
}
api::Position3 Vector(const Json& value) {
    Need(value.is_array() && value.size() == 3, "visual vector must have three values");
    return {Number(value[0]), Number(value[1]), Number(value[2])};
}
std::vector<std::string> Names(const Json& value) {
    Need(value.is_array() && !value.empty(), "visual effect list must be nonempty");
    std::vector<std::string> out;
    std::set<std::string> unique;
    for (const auto& item : value) {
        auto name = Name(item);
        Need(unique.insert(name).second, "duplicate effect key: " + name);
        out.push_back(std::move(name));
    }
    return out;
}
api::VisibilityFollow FollowFlag(const Json& row) {
    const auto name = row.value("visibility", std::string("none"));
    if (name == "none")
        return api::VisibilityFollow::None;
    if (name == "visible")
        return api::VisibilityFollow::Visible;
    if (name == "invisible")
        return api::VisibilityFollow::Invisible;
    if (name == "follow")
        return api::VisibilityFollow::Follow;
    if (name == "transmit_visible")
        return api::VisibilityFollow::TransmitVisible;
    if (name == "transmit_invisible")
        return api::VisibilityFollow::TransmitInvisible;
    if (name == "transmit")
        return api::VisibilityFollow::Transmit;
    throw std::invalid_argument("unknown partner visibility follow: " + name);
}
std::vector<VisualInstruction> Instructions(const Json& rows, const VisualProgram& program,
                                          const std::string& path, bool request_fields) {
    Need(rows.is_array(), "visual instructions must be an array");
    std::vector<VisualInstruction> out;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const auto& row = rows[index];
        Need(request_fields || (!row.contains("as") && !row.contains("wait")),
             "request fields require a flow step");
        Fields(row, {"op",
                     "as",
                     "wait",
                     "source",
                     "when",
                     "effects",
                     "variable",
                     "value",
                     "effect",
                     "seconds",
                     "fade",
                     "immediate",
                     "trigger",
                     "reset",
                     "key",
                     "path",
                     "type",
                     "wrap_mode",
                     "release_volume",
                     "optional",
                     "targets",
                     "state",
                     "transition",
                     "restart",
                     "context",
                     "name",
                     "selection",
                     "partner",
                     "target",
                     "self_point",
                     "target_point",
                     "disable_when_invisible",
                     "use_target_scale",
                     "visibility",
                     "visible",
                     "mute_collider",
                     "mute_triggers",
                     "mute_audio",
                     "normalized_start",
                     "normalized_transition",
                     "normalized_end",
                     "ratio",
                     "angle_offset",
                     "duration",
                     "points",
                     "delay",
                     "scale",
                     "layer",
                     "weight",
                     "speed",
                     "active",
                     "factor",
                     "fade_in",
                     "fade_out",
                     "finite_only",
                     "recipient",
                     "cheek",
                     "shy",
                     "shadow",
                     "target_collection",
                     "save_targets",
                     "read_targets",
                     "outline",
                     "multiplier",
                     "equal_ratio_by_min",
                     "node",
                     "mask",
                     "enabled",
                     "add",
                     "remove",
                     "command",
                     "perform",
                     "shared_environment",
                     "bpm",
                     "chapters",
                     "radius",
                     "somato",
                     "effect_adaptations",
                     "graph_emotion",
                     "event_files",
                     "transit_out",
                     "transit_in",
                     "unbind"});
        VisualInstruction instruction;
        instruction.source = row.contains("source") ? Name(row.at("source"))
                                                   : request_fields ? path : path + "/" + std::to_string(index);
        if (row.contains("context") && !row.at("context").is_null()) {
            instruction.context = Name(row.at("context"));
            Need(program.contexts.count(instruction.context) != 0, "unknown visual context: " + instruction.context);
        }
        if (row.contains("when")) {
            const auto& when = row.at("when");
            Fields(when, {"choice", "is"});
            instruction.choice = Name(when.at("choice"));
            instruction.value = Name(when.at("is"));
        }
        const auto op = Name(row.at("op"));
        Need(op == "effect.remove" || !row.contains("optional"), "optional effect keys require remove");
        if (const auto layout = visual_layout_codec::Parse(row)) {
            Need(program.effects.count(layout->effect) != 0, "unknown owned layout effect key");
            if (!layout->target.empty())
                Need(program.effects.count(layout->target) != 0, "unknown owned layout transfer target key");
            instruction.operation = *layout;
        } else if (const auto bgm = visual_bgm_codec::Parse(row))
            instruction.operation = *bgm;
        else if (const auto model = visual_model_codec::Parse(row))
            instruction.operation = *model;
        else if (op.compare(0, 8, "partner.") == 0) {
            VisualPartnerOperation partner;
            partner.partner = Name(row.at("partner"));
            Need(program.partners.count(partner.partner) != 0, "unknown partner input: " + partner.partner);
            if (op == "partner.create")
                partner.command = VisualPartnerCreate{};
            else if (op == "partner.remove")
                partner.command = VisualPartnerRemove{};
            else if (op == "partner.attach")
                partner.command = VisualPartnerAttach{Name(row.at("target")),
                                                      Name(row.at("self_point")),
                                                      Name(row.at("target_point")),
                                                      row.value("disable_when_invisible", false),
                                                      row.value("use_target_scale", false),
                                                      FollowFlag(row)};
            else if (op == "partner.animation_follow" || op == "partner.visibility_follow") {
                const auto target = row.at("target").is_null() ? std::string{} : Name(row.at("target"));
                if (op == "partner.animation_follow")
                    partner.command = VisualPartnerAnimationFollow{target};
                else
                    partner.command = VisualPartnerVisibilityFollow{target, FollowFlag(row)};
            } else if (op == "partner.visibility")
                partner.command = api::PartnerVisibility{
                    Name(row.at("key")), row.at("visible").get<bool>(), row.value("mute_collider", false),
                    row.value("mute_triggers", false), row.value("mute_audio", false)};
            else if (op == "partner.idle")
                partner.command =
                    api::PartnerIdleAnimation{Name(row.at("state")), Number(row.value("normalized_start", Json(0))),
                                              Number(row.value("normalized_transition", Json(0)), true),
                                              Number(row.value("normalized_end", Json(1)))};
            else
                throw std::invalid_argument("unknown partner operation: " + op);
            instruction.operation = std::move(partner);
        } else if (op == "render.mask" || op == "render.outline" || op == "render.visibility") {
            VisualRenderingOperation render;
            if (row.contains("recipient"))
                render.recipient = Name(row.at("recipient"));
            if (op == "render.mask") {
                Need(row.at("mask").is_number_unsigned() || row.at("mask").is_number_integer(),
                     "renderer mask must be an integer");
                const auto mask = row.at("mask").get<std::int64_t>();
                Need(mask > 0 && mask <= std::numeric_limits<std::uint32_t>::max(), "renderer mask out of range");
                VisualRendererMask command;
                command.mask = static_cast<std::uint32_t>(mask);
                command.enabled = row.at("enabled").get<bool>();
                if (row.contains("node") && !row.at("node").is_null())
                    command.node = Name(row.at("node"));
                render.command = std::move(command);
            } else if (op == "render.outline") {
                VisualOutlineTargets command;
                command.reset = row.value("reset", false);
                auto names = [&](const char* key) {
                    std::vector<std::string> out;
                    if (!row.contains(key) || row.at(key).is_null())
                        return out;
                    Need(row.at(key).is_array(), "outline names must be an array");
                    if (!row.at(key).empty())
                        out = Names(row.at(key));
                    return out;
                };
                command.add = names("add");
                command.remove = names("remove");
                Need(command.reset || !command.add.empty() || !command.remove.empty(), "empty outline operation");
                render.command = std::move(command);
            } else
                render.command = VisualEntityVisible{Name(row.at("key")), row.at("visible").get<bool>()};
            instruction.operation = std::move(render);
        } else if (op == "animation.layer_weight") {
            instruction.operation = VisualLayerWeight{Name(row.at("layer")), Number(row.at("weight")),
                                                      Number(row.value("duration", Json(0)), true)};
        } else if (op == "animation.speed") {
            Need(row.contains("speed") != row.contains("reset"), "animator speed requires speed or reset");
            if (row.contains("reset"))
                Need(row.at("reset").is_boolean() && row.at("reset").get<bool>(), "animator reset must be true");
            instruction.operation = VisualAnimatorSpeed{row.contains("speed") ? Number(row.at("speed"), true) : -1};
        } else if (op == "actor.time_scale") {
            VisualActorTimeScale command;
            command.active = row.value("active", true);
            command.finite_only = row.value("finite_only", false);
            if (row.contains("recipient"))
                command.recipient = Name(row.at("recipient"));
            if (row.contains("key"))
                command.key = Name(row.at("key"));
            Need(command.active ? (!command.finite_only && !command.key.empty())
                                : (command.finite_only != row.contains("key")),
                 "actor clock requires one explicit key or an inactive finite-only selector");
            command.fade = row.value("fade", false);
            if (command.active) {
                command.input.factor = Number(row.at("factor"), true);
                command.input.delay = Number(row.value("delay", Json(0)), true);
                command.input.fade_in = Number(row.value("fade_in", Json(0)), true);
                command.input.fade_out = Number(row.value("fade_out", Json(0)), true);
                if (row.contains("duration")) {
                    command.input.duration = Number(row.at("duration"), true);
                    Need(*command.input.duration > 0, "finite actor clock duration must be positive");
                }
            } else
                Need(!row.contains("factor") && !row.contains("duration") && !row.contains("fade_in") &&
                         !row.contains("fade_out") && !row.contains("delay"),
                     "clock removal has no creation parameters");
            instruction.operation = std::move(command);
        } else if (op == "attachment.scale") {
            const auto& scale = row.at("scale");
            Need(scale.is_array() && scale.size() == 3, "attachment scale requires three values");
            instruction.operation = VisualAttachmentScale{
                Names(row.at("points")),
                {Number(scale.at(0), true), Number(scale.at(1), true), Number(scale.at(2), true)}};
        } else if (op == "attachment.visibility") {
            const auto delay = row.value("delay", std::int64_t{0});
            Need(delay >= 0 && delay <= std::numeric_limits<int>::max(), "attachment visibility delay out of range");
            instruction.operation = VisualAttachmentVisibility{Names(row.at("points")), row.at("visible").get<bool>(),
                                                               static_cast<int>(delay)};
        } else if (op == "facing.target") {
            instruction.operation =
                VisualFace{Name(row.at("target")), Number(row.value("ratio", Json(1))),
                           Number(row.value("angle_offset", Json(0))), Number(row.value("duration", Json(-1)))};
        } else if (op == "scope.enter" || op == "scope.exit") {
            const auto name = Name(row.at("name"));
            Need(program.contexts.count(name) != 0, "unknown visual context: " + name);
            instruction.operation = VisualContextOperation{name, op == "scope.enter", row.value("fade", true)};
        } else if (op == "effect.create") {
            auto names = Names(row.at("effects"));
            for (const auto& key : names)
                Need(program.effects.count(key) != 0, "unknown effect input: " + key);
            instruction.operation = VisualCreate{std::move(names)};
        } else if (op == "variable.set")
            instruction.operation = VisualAssign{Name(row.at("variable")), Number(row.at("value"))};
        else if (op == "effect.progress") {
            VisualProgress progress;
            progress.effect = Name(row.at("effect"));
            progress.seconds = Number(row.value("seconds", Json(0)), true);
            if (row.at("value").is_object()) {
                Fields(row.at("value"), {"variable"});
                progress.value = Name(row.at("value").at("variable"));
            } else
                progress.value = Number(row.at("value"));
            instruction.operation = std::move(progress);
        } else if (op == "effect.remove_event") {
            instruction.operation = VisualRemoveEventEffect{
                Name(row.at("path")),
                {row.value("fade", true), row.value("immediate", false), row.value("unbind", false)}};
        } else if (op == "effect.remove") {
            VisualRemove remove{Names(row.at("effects")), row.value("fade", true), row.value("immediate", false), {}};
            if (row.contains("optional"))
                for (const auto& key : Names(row.at("optional"))) {
                    Need(std::find(remove.effects.begin(), remove.effects.end(), key) != remove.effects.end(),
                         "optional key not in removal: " + key);
                    remove.optional.insert(key);
                }
            instruction.operation = std::move(remove);
        } else if (op == "effect.trigger")
            instruction.operation =
                VisualTrigger{Names(row.at("effects")), Name(row.at("trigger")), row.value("reset", false)};
        else if (op == "effect.follow") {
            const auto& targets = row.at("targets");
            Need(targets.is_array(), "follow targets must be an array");
            instruction.operation =
                VisualFollow{Name(row.at("effect")), targets.empty() ? std::vector<std::string>{} : Names(targets)};
        } else if (op == "effect.state")
            instruction.operation =
                VisualEffectState{Name(row.at("effect")), Name(row.at("state")),
                                  Number(row.value("transition", Json(0)), true), row.value("restart", false)};
        else if (op == "effect.parameter") {
            VisualEffectParameter parameter;
            parameter.effect = Name(row.at("effect"));
            parameter.parameter.name = Name(row.at("name"));
            const auto type = Name(row.at("type"));
            if (type == "float")
                parameter.parameter.value = Number(row.at("value"));
            else if (type == "int") {
                const auto& value = row.at("value");
                Need(value.is_number_integer(), "effect int parameter requires an integer");
                if (value.is_number_unsigned())
                    Need(value.get<std::uint64_t>() <= static_cast<std::uint64_t>(std::numeric_limits<int>::max()),
                         "effect int parameter out of range");
                else
                    Need(value.get<std::int64_t>() >= std::numeric_limits<int>::min() &&
                             value.get<std::int64_t>() <= std::numeric_limits<int>::max(),
                         "effect int parameter out of range");
                parameter.parameter.value = value.get<int>();
            } else if (type == "bool") {
                Need(row.at("value").is_boolean(), "effect bool parameter requires a boolean");
                parameter.parameter.value = row.at("value").get<bool>();
            } else if (type == "trigger") {
                Need(!row.contains("value"), "effect trigger parameter has no value");
                parameter.parameter.value = api::EffectParameterTrigger{};
            } else
                throw std::invalid_argument("unknown effect parameter type: " + type);
            const auto selection = row.value("selection", std::string("first_active"));
            if (selection == "first_matching_parameter")
                parameter.parameter.selection = api::EffectAnimatorSelection::FirstMatchingParameter;
            else
                Need(selection == "first_active", "unknown effect Animator selection: " + selection);
            instruction.operation = std::move(parameter);
        } else if (op == "timeline.play" || op == "timeline.stop") {
            VisualTimeline timeline;
            timeline.key = Name(row.at("key"));
            timeline.play = op == "timeline.play";
            if (timeline.play)
                timeline.path = Name(row.at("path"));
            timeline.type = row.value("type", 0);
            timeline.wrap_mode = row.value("wrap_mode", 0);
            timeline.release_volume = row.value("release_volume", false);
            timeline.shared_environment = row.value("shared_environment", false);
            instruction.operation = std::move(timeline);
        } else
            throw std::invalid_argument("unknown visual operation: " + op);
        if (!instruction.context.empty())
            Need(!std::holds_alternative<VisualContextOperation>(instruction.operation) &&
                     !std::holds_alternative<VisualAssign>(instruction.operation),
                 "context selection applies to effect, partner and Timeline operations");
        out.push_back(std::move(instruction));
    }
    return out;
}
void ContextInstructions(const std::vector<VisualInstruction>& steps) {
    for (const auto& step : steps) {
        Need(step.context.empty(), "context instructions operate on their own lifetime");
        Need(!std::holds_alternative<VisualContextOperation>(step.operation),
             "nested context lifecycle is unsupported");
    }
}
}
std::vector<VisualInstruction> visual_codec::Instructions(const Json& json, const VisualProgram& program,
                                                         const std::string& path, bool request_fields) {
    return hksr::framework::Instructions(json, program, path, request_fields);
}
VisualProgram visual_codec::Resources(const Json& json, std::string id, std::string source_path, std::string event_scope) {
    VisualProgram out;
    out.id = std::move(id);
    out.source = std::move(source_path);
    out.scope = std::move(event_scope);
    const auto effects = json.value("effects", Json::object());
    Need(effects.is_object(), "visual effects must be an object");
    for (auto it = effects.begin(); it != effects.end(); ++it) {
        const auto& row = it.value();
        Fields(row, {"path", "target", "anchor", "position_offset", "rotation_offset", "scale", "attach_to_target",
                     "attach_point", "fade_in", "simulate_immediately", "unique_name", "resident", "allow_position",
                     "mutex", "enable_replay"});
        VisualEffectInput input;
        input.path = Name(row.at("path"));
        Need(row.contains("target") != row.contains("anchor"), "effect needs exactly one target or world anchor");
        if (row.contains("anchor")) {
            input.anchor = Name(row.at("anchor"));
            Need(!row.contains("position_offset") && !row.contains("rotation_offset") &&
                     !row.contains("attach_to_target") && !row.contains("attach_point"),
                 "world-anchored effect cannot use entity-relative inputs");
        } else
            input.target = Name(row.at("target"));
        if (row.contains("position_offset"))
            input.options.position_offset = Vector(row.at("position_offset"));
        if (row.contains("rotation_offset"))
            input.options.rotation_offset = Vector(row.at("rotation_offset"));
        if (row.contains("scale"))
            input.options.scale = Vector(row.at("scale"));
        if (row.contains("unique_name"))
            input.options.unique_name = Name(row.at("unique_name"));
        if (row.contains("attach_point"))
            input.options.attach_point = Name(row.at("attach_point"));
        input.options.attach_to_target = row.value("attach_to_target", false);
        input.options.fade_in = row.value("fade_in", true);
        input.options.simulate_immediately = row.value("simulate_immediately", false);
        input.options.resident = row.value("resident", false);
        input.submission.enable_replay = row.value("enable_replay", false);
        input.allow_position = row.value("allow_position", false);
        if (row.contains("mutex")) {
            const auto& mutex = row.at("mutex");
            Fields(mutex, {"interval", "maximum", "scope"});
            api::EffectMutexRule rule;
            rule.interval = Number(mutex.at("interval"));
            Need(mutex.at("maximum").is_number_integer(), "effect mutex maximum must be an integer");
            const auto maximum = mutex.at("maximum").get<std::int64_t>();
            Need(maximum >= 0 && maximum <= std::numeric_limits<std::int32_t>::max(), "invalid effect mutex maximum");
            rule.maximum = static_cast<std::int32_t>(maximum);
            const auto scope = Name(mutex.at("scope"));
            Need(scope == "global" || scope == "entity", "invalid effect mutex scope");
            rule.scope = scope == "global" ? api::EffectMutexScope::Global : api::EffectMutexScope::Entity;
            input.mutex = rule;
        }
        Need(!input.allow_position || (!input.target.empty() && !input.options.attach_to_target),
             "position target cannot provide an entity attachment");
        out.effects.emplace(Name(it.key()), std::move(input));
    }
    if (json.contains("partners")) {
        Need(json.at("partners").is_object(), "visual partners must be an object");
        std::set<std::string> members;
        for (auto it = json.at("partners").begin(); it != json.at("partners").end(); ++it) {
            const auto& row = it.value();
            Fields(row, {"model", "config", "member"});
            VisualPartnerInput value{Name(row.at("model")), Name(row.at("config")), Name(row.at("member"))};
            Need(members.insert(value.member).second, "duplicate partner member name: " + value.member);
            out.partners.emplace(Name(it.key()), std::move(value));
        }
    }
    for (const auto* collection : {"phases", "projectiles"}) {
        if (!json.contains(collection))
            continue;
        auto& destination = std::string(collection) == "phases" ? out.phases : out.projectiles;
        Need(json.at(collection).is_object(), "visual phases must be an object");
        for (auto it = json.at(collection).begin(); it != json.at(collection).end(); ++it) {
            const auto& row = it.value();
            Fields(row, {"root", "path"});
            api::DefinitionSource source;
            const auto& root = row.at("root");
            Fields(root, {"kind", "name"});
            const auto kind = Name(root.at("kind"));
            source.name = Name(root.at("name"));
            if (kind == "ability")
                source.root = api::DefinitionRoot::Ability;
            else if (kind == "global_modifier")
                source.root = api::DefinitionRoot::GlobalModifier;
            else if (kind == "global_template")
                source.root = api::DefinitionRoot::GlobalTemplate;
            else
                throw std::invalid_argument("unknown definition root kind: " + kind);
            const auto& path = row.at("path");
            Need(path.is_array() && !path.empty(), "definition field path must be nonempty");
            for (const auto& edge : path) {
                Fields(edge, {"owner", "field", "index", "key"});
                api::ConfigStep step;
                step.declaring_type = Name(edge.at("owner"));
                step.field = Name(edge.at("field"));
                Need(!edge.contains("index") || !edge.contains("key"),
                     "definition edge cannot have both index and key");
                if (edge.contains("index")) {
                    Need(edge.at("index").is_number_integer(), "definition index must be an integer");
                    const auto index = edge.at("index").get<std::int64_t>();
                    Need(index >= 0 && index <= INT32_MAX, "definition index is out of range");
                    step.selection = api::ConfigSelection::Index;
                    step.index = static_cast<std::int32_t>(index);
                } else if (edge.contains("key")) {
                    step.selection = api::ConfigSelection::StringKey;
                    step.key = Name(edge.at("key"));
                }
                source.path.push_back(std::move(step));
            }
            destination.emplace(Name(it.key()), std::move(source));
        }
    }
    return out;
}
VisualProgramContext visual_codec::Context(const Json& row, const VisualProgram& program, const std::string& path) {
    VisualProgramContext result;
    if (row.contains("configuration")) {
        result.phase = Name(row.at("configuration"));
        Need(program.phases.count(result.phase) != 0, "unknown phase configuration: " + result.phase);
    }
    if (row.contains("event_scope")) result.event_scope = Name(row.at("event_scope"));
    result.enter = Instructions(row.value("enter", Json::array()), program, path + "/enter");
    result.exit = Instructions(row.value("exit", Json::array()), program, path + "/exit");
    ContextInstructions(result.enter);
    ContextInstructions(result.exit);
    if (row.contains("restore")) {
        result.restore = Instructions(row.at("restore"), program, path + "/restore");
        ContextInstructions(*result.restore);
    }
    if (row.contains("restore_effects")) {
        Need(result.restore.has_value() && row.at("restore_effects").is_array(),
             "restore_effects requires a restore recipe");
        for (const auto& item : row.at("restore_effects")) {
            auto key = Name(item);
            Need(program.effects.count(key) != 0, "unknown persistent effect: " + key);
            Need(result.restore_effects.insert(key).second, "duplicate persistent effect: " + key);
        }
        for (const auto& instruction : *result.restore)
            if (const auto* create = std::get_if<VisualCreate>(&instruction.operation))
                for (const auto& key : create->effects)
                    Need(!result.restore_effects.count(key), "effect also created by restore recipe: " + key);
    }
    return result;
}
VisualProgramAction visual_codec::CuedAction(const Json& row, const VisualProgram& program, const std::string& path) {
    Fields(row, {"op", "state", "restart_scope", "prepare", "tracks", "complete_at", "then"});
    Need(row.at("op") == "animation.cued", "cued animation expected");
    VisualProgramAction result;
    result.state = Name(row.at("state"));
    result.restart = row.value("restart_scope", false);
    result.enter = Instructions(row.value("prepare", Json::array()), program, path + "/prepare");
    result.complete_at = Number(row.value("complete_at", Json(1)), true);
    if (row.contains("tracks")) {
        Need(row.at("tracks").is_array(), "animation tracks must be an array");
        for (std::size_t index = 0; index < row.at("tracks").size(); ++index) {
            const auto& track = row.at("tracks")[index];
            Need(track.is_array(), "animation track must be an array");
            std::vector<VisualCue> lane;
            for (std::size_t cue_index = 0; cue_index < track.size(); ++cue_index) {
                const auto& cue = track[cue_index];
                Fields(cue, {"at", "run"});
                const auto at = Number(cue.at("at"), true);
                Need(lane.empty() || at > lane.back().at, "animation cue times must increase");
                Need(at <= result.complete_at, "cue follows animation completion");
                lane.push_back({at, Instructions(cue.at("run"), program,
                                                path + "/tracks/" + std::to_string(index) + "/" + std::to_string(cue_index) + "/run")});
            }
            result.lanes.push_back(std::move(lane));
        }
    }
    if (row.contains("then")) {
        const auto& next = row.at("then");
        Fields(next, {"state", "at", "run"});
        result.completion_state = Name(next.at("state"));
        result.completion_at = Number(next.value("at", Json(0)), true);
        result.after_completion = Instructions(next.value("run", Json::array()), program, path + "/then/run");
    }
    return result;
}
VisualProgram ParseVisualProgram(const std::string& text) {
    return *ParseScript(text)->program;
}
VisualSpatialAnchors ParseVisualSpatialAnchors(const std::string& text) {
    const auto json = Json::parse(text);
    Need(json.is_object(), "visual anchors must be an object");
    VisualSpatialAnchors out;
    for (auto it = json.begin(); it != json.end(); ++it) {
        const auto& row = it.value();
        Fields(row, {"position", "rotation"});
        VisualSpatialAnchor anchor;
        anchor.position = Vector(row.at("position"));
        if (row.contains("rotation")) {
            const auto& r = row.at("rotation");
            Need(r.is_array() && r.size() == 4, "anchor rotation must be a quaternion");
            anchor.rotation = {Number(r[0]), Number(r[1]), Number(r[2]), Number(r[3])};
            const auto& q = anchor.rotation;
            Need(double(q.x) * q.x + double(q.y) * q.y + double(q.z) * q.z + double(q.w) * q.w > 0,
                 "anchor rotation must be nonzero");
        }
        out.emplace(Name(it.key()), anchor);
    }
    return out;
}
}
