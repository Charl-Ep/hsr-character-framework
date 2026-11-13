#include <hksr/framework/tasks/TaskCatalog.hpp>
#include "VisualCodec.hpp"
#include "HitEffectsCodec.hpp"
#include "AnimationParameterCodec.hpp"
#include "ScriptComposition.hpp"
#include <hksr/framework/presentation/scene/VisualProgramLibrary.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace hksr::framework {
namespace {
using Json = nlohmann::json;
void Need(bool ok, const std::string& detail) {
    if (!ok)
        throw std::invalid_argument(detail);
}
void Fields(const Json& row, std::initializer_list<const char*> names) {
    Need(row.is_object(), "task object expected");
    for (auto it = row.begin(); it != row.end(); ++it) {
        bool known = false;
        for (const auto* name : names)
            known = known || it.key() == name;
        Need(known, "unknown task field: " + it.key());
    }
}
void StepFields(const Json& row, std::initializer_list<const char*> names) {
    Need(row.is_object(), "script step must be an object");
    for (auto it = row.begin(); it != row.end(); ++it) {
        bool known = it.key() == "wait";
        for (const auto* name : names)
            known = known || it.key() == name;
        Need(known, "unknown script step field: " + it.key());
    }
}
std::string Name(const Json& value) {
    const auto name = value.get<std::string>();
    Need(!name.empty() && name.find('\0') == std::string::npos, "invalid task name");
    return name;
}
std::int64_t Integer(const Json& value) {
    Need(value.is_number_integer(), "task integer expected");
    if (value.is_number_unsigned())
        Need(value.get<std::uint64_t>() <= INT64_MAX, "task integer out of range");
    return value.get<std::int64_t>();
}
double Number(const Json& value) {
    Need(value.is_number(), "task number expected");
    const auto number = value.get<double>();
    Need(std::isfinite(number), "nonfinite task number");
    return number;
}
float Float(const Json& value) {
    const auto number = Number(value);
    Need(std::abs(number) <= std::numeric_limits<float>::max(), "movement number out of range");
    return static_cast<float>(number);
}
VisualMovement Movement(const Json& row) {
    VisualMovement move;
    const auto clock = row.value("clock", std::string("animation"));
    if (clock == "animation")
        move.clock = api::MovementClock::Animation;
    else if (clock == "speed")
        move.clock = api::MovementClock::DistanceSpeed;
    else if (clock == "instant")
        move.instant = true;
    else
        throw std::invalid_argument("unknown movement clock: " + clock);
    move.force_start = row.value("force_start", false);
    Need(row.at("segments").is_array(), "movement segments must be an array");
    for (const auto& part : row.at("segments")) {
        Fields(part, {"destination", "name", "offset", "preserve_height", "stop_distance", "start", "end", "speed",
                      "exit_time", "sample", "target_geometry"});
        if (move.instant)
            Need(!part.contains("start") && !part.contains("end") && !part.contains("speed") &&
                     !part.contains("exit_time"),
                 "instant movement has no timing fields");
        VisualMovementSegment segment;
        const auto destination = Name(part.at("destination"));
        if (destination == "origin")
            segment.destination = MovementDestination::Origin;
        else if (destination == "anchor")
            segment.destination = MovementDestination::Anchor;
        else if (destination == "target")
            segment.destination = MovementDestination::Target;
        else
            throw std::invalid_argument("unknown movement destination: " + destination);
        if (part.contains("name"))
            segment.name = Name(part.at("name"));
        if (part.contains("offset")) {
            const auto& offset = part.at("offset");
            Need(offset.is_array() && offset.size() == 3, "movement offset requires [x,y,z]");
            segment.offset = {Float(offset[0]), Float(offset[1]), Float(offset[2])};
        }
        segment.preserve_height = part.value("preserve_height", true);
        segment.stop_distance = Float(part.value("stop_distance", Json(0)));
        segment.target_geometry = part.value("target_geometry", false);
        segment.start = Float(part.value("start", Json(0)));
        segment.end = Float(part.value("end", Json(1)));
        segment.speed = Float(part.value("speed", Json(1)));
        segment.exit_time = Float(part.value("exit_time", Json(-1)));
        if (part.contains("sample")) {
            const auto& value = part.at("sample");
            Fields(value, {"percentage", "distance", "direction", "behind_target", "include_radii"});
            VisualMovementSample sample;
            sample.percentage = Float(value.at("percentage"));
            sample.distance = Float(value.at("distance"));
            sample.behind_target = value.value("behind_target", false);
            sample.include_radii = value.value("include_radii", false);
            if (value.contains("direction")) {
                const auto& direction = value.at("direction");
                Need(direction.is_array() && direction.size() == 3, "movement direction requires [x,y,z]");
                sample.direction = {Float(direction[0]), Float(direction[1]), Float(direction[2])};
            }
            segment.sample = sample;
        }
        move.segments.push_back(std::move(segment));
    }
    return move;
}
TaskValue Value(const Json& value) {
    if (value.is_boolean())
        return value.get<bool>();
    if (value.is_number_integer())
        return Integer(value);
    if (value.is_number_float())
        return Number(value);
    if (value.is_string())
        return value.get<std::string>();
    throw std::invalid_argument("task value must be bool, integer, number or text");
}
TaskValues Values(const Json& json) {
    Need(json.is_object(), "task values must be an object");
    TaskValues result;
    for (auto it = json.begin(); it != json.end(); ++it)
        result.emplace(Name(it.key()), Value(it.value()));
    return result;
}
void LocalInstructions(const std::vector<VisualInstruction>& instructions) {
    for (const auto& instruction : instructions) {
        Need(instruction.context.empty(), "restoration and scoped signals use their own resource lifetime");
        Need(!std::holds_alternative<VisualContextOperation>(instruction.operation),
             "nested resource lifecycle is unsupported in restoration and scoped signals");
    }
}
ContextNumber ContextNumeric(const Json& value) {
    if (value.is_number_integer())
        return Integer(value);
    Need(value.is_number_float(), "context number expected");
    return Number(value);
}
TaskRequirement Requirement(const Json& row, const std::string& catalog) {
    const auto op = Name(row.at("op"));
    if (op == "playback") {
        Fields(row, {"op", "name", "is", "values"});
        const auto name = Name(row.at("name"));
        Need(name == "selected" || name == "performance", "unknown playback property");
        Need(int(row.contains("is")) + int(row.contains("values")) == 1,
             "playback condition requires is or values");
        const TaskValueContext context{"@playback", catalog};
        if (row.contains("is"))
            return TaskValueEquals{name, Value(row.at("is")), context};
        Need(row.at("values").is_array(), "playback values must be an array");
        TaskValueOneOf result{name, {}, context};
        for (const auto& value : row.at("values"))
            result.values.push_back(Value(value));
        return result;
    }
    const auto context = [&]() -> std::optional<TaskValueContext> {
        return row.contains("context") ? std::optional<TaskValueContext>{{Name(row.at("context")), catalog}}
                                       : std::nullopt;
    };
    if (op == "value") {
        Fields(row, {"op", "name", "is", "context"});
        return TaskValueEquals{Name(row.at("name")), Value(row.at("is")), context()};
    }
    if (op == "one_of") {
        Fields(row, {"op", "name", "values", "context"});
        Need(row.at("values").is_array(), "condition values must be an array");
        TaskValueOneOf result{Name(row.at("name")), {}, context()};
        for (const auto& value : row.at("values"))
            result.values.push_back(Value(value));
        return result;
    }
    if (op == "number") {
        Fields(row, {"op", "name", "compare", "value", "context"});
        const auto compare = Name(row.at("compare"));
        const std::map<std::string, ContextNumberComparison> operations = {
            {"eq", ContextNumberComparison::Equal},   {"ne", ContextNumberComparison::NotEqual},
            {"lt", ContextNumberComparison::Less},    {"le", ContextNumberComparison::LessEqual},
            {"gt", ContextNumberComparison::Greater}, {"ge", ContextNumberComparison::GreaterEqual}};
        Need(operations.count(compare) != 0, "unknown number comparison");
        return TaskNumberCompare{Name(row.at("name")), ContextNumeric(row.at("value")), operations.at(compare),
                                 context()};
    }
    if (op == "phase") {
        Fields(row, {"op", "is"});
        return TaskPhaseEquals{Name(row.at("is"))};
    }
    if (op == "participant") {
        Fields(row, {"op", "name"});
        return TaskNeedsParticipant{{Name(row.at("name"))}};
    }
    if (op == "target") {
        Fields(row, {"op", "name", "allow_position"});
        return TaskNeedsTarget{Name(row.at("name")), row.value("allow_position", false)};
    }
    if (op == "anchor") {
        Fields(row, {"op", "name"});
        return TaskNeedsAnchor{Name(row.at("name"))};
    }
    if (op == "choice") {
        Fields(row, {"op", "name", "is"});
        return TaskChoiceEquals{Name(row.at("name")), Name(row.at("is"))};
    }
    if (op == "state") {
        Fields(row, {"op", "name", "present"});
        return TaskContextPresent{Name(row.at("name")), row.value("present", true), catalog};
    }
    if (op == "unresolved") {
        Fields(row, {"op", "source", "detail"});
        return TaskUnresolvedRequirement{Name(row.at("source")), Name(row.at("detail"))};
    }
    throw std::invalid_argument("unknown task predicate: " + op);
}
TaskKey Key(const Json& json, const std::string& catalog) {
    return {catalog, Name(json)};
}
TaskInputDefinition Input(const std::string& name, const Json& row) {
    Fields(row, {"type", "default", "label", "description", "choices", "diagnostic"});
    TaskInputDefinition input;
    input.name = Name(name);
    input.diagnostic = row.value("diagnostic", false);
    const auto type = Name(row.at("type"));
    if (type == "bool")
        input.type = TaskValueType::Boolean;
    else if (type == "integer")
        input.type = TaskValueType::Integer;
    else if (type == "number")
        input.type = TaskValueType::Number;
    else if (type == "text")
        input.type = TaskValueType::Text;
    else
        throw std::invalid_argument("unknown task input type: " + type);
    if (row.contains("default"))
        input.default_value = Value(row.at("default"));
    if (row.contains("label"))
        input.label = Name(row.at("label"));
    if (row.contains("description"))
        input.description = Name(row.at("description"));
    if (row.contains("choices")) {
        Need(row.at("choices").is_array() && !row.at("choices").empty(), "task input choices must be a nonempty array");
        for (const auto& choice : row.at("choices")) {
            Fields(choice, {"value", "label", "diagnostic"});
            input.choices.push_back(
                {Value(choice.at("value")), Name(choice.at("label")), choice.value("diagnostic", false)});
        }
    }
    return input;
}
struct SequenceParser {
    const VisualProgram& visual;
    const std::string& catalog;
    const Json& definitions;
    TaskSequenceInput sequence;
    std::map<std::string, TaskNodeIndex> compiled_references;
    std::set<std::string> active_references;
    std::string Request(const Json& row, const std::string& path) const {
        return row.contains("as") ? Name(row.at("as")) : "@" + path;
    }
    TaskWaitSignal Signal(const std::string& signal) const {
        if (signal == "submitted") return TaskWaitSignal::Submitted;
        if (signal == "accepted") return TaskWaitSignal::RequestComplete;
        if (signal == "started") return TaskWaitSignal::Started;
        if (signal == "animation_end") return TaskWaitSignal::AnimationEnd;
        if (signal == "animation_finished") return TaskWaitSignal::AnimationFinished;
        if (signal == "state_left") return TaskWaitSignal::StateLeft;
        if (signal == "performance") return TaskWaitSignal::PerformanceComplete;
        if (signal == "drained") return TaskWaitSignal::Drained;
        if (signal == "progress") return TaskWaitSignal::Progress;
        throw std::invalid_argument("unknown script wait signal: " + signal);
    }
    TaskNodeIndex Node(const Json& row, const std::string& path) {
        try {
            if (row.is_array()) {
                std::vector<TaskNodeIndex> children;
                for (std::size_t i = 0; i < row.size(); ++i)
                    children.push_back(Node(row[i], path + "/" + std::to_string(i)));
                if (children.size() == 1)
                    return children.front();
                const auto index = sequence.nodes.size();
                sequence.nodes.push_back({path, TaskSerial{std::move(children)}});
                return index;
            }
            const auto op = Name(row.at("op"));
            TaskNodeOperation operation;
            if (op == "call") {
                StepFields(row, {"op", "function", "source"});
                const auto name = Name(row.at("function"));
                Need(!active_references.count(name), "cyclic sequence reference: " + name);
                auto found = compiled_references.find(name);
                if (found == compiled_references.end()) {
                    Need(definitions.contains(name), "unknown sequence reference: " + name);
                    const auto& target = definitions.at(name);
                    active_references.insert(name);
                    const auto child = Node(target, "functions/" + name);
                    active_references.erase(name);
                    found = compiled_references.emplace(name, child).first;
                }
                operation = TaskSerial{{found->second}};
            } else if (op == "skip") {
                StepFields(row, {"op", "as", "reason", "source", "unit", "dependencies"});
                TaskOmitted omitted{Request(row, path), Name(row.at("reason"))};
                if (row.contains("unit"))
                    omitted.unit = Name(row.at("unit"));
                if (row.contains("dependencies")) {
                    Need(row.at("dependencies").is_array(), "omitted dependencies must be an array");
                    for (const auto& dependency : row.at("dependencies"))
                        omitted.dependencies.push_back(Name(dependency));
                }
                operation = std::move(omitted);
            } else if (op == "parallel") {
                StepFields(row, {"op", "branches", "source"});
                std::vector<TaskNodeIndex> compiled;
                if (row.contains("branches")) {
                    const auto& children = row.at("branches");
                    Need(children.is_array(), "task children must be an array");
                    for (std::size_t i = 0; i < children.size(); ++i)
                        compiled.push_back(Node(children[i], path + "/branches/" + std::to_string(i)));
                }
                operation = TaskParallel{std::move(compiled)};
            } else if (op == "for_each") {
                StepFields(row, {"op", "bind", "targets", "run", "empty", "collection", "source"});
                TaskForTargets targets;
                if (row.contains("collection"))
                    targets.collection = Name(row.at("collection"));
                targets.bind = Name(row.at("bind"));
                Need(row.at("targets").is_array(), "target inputs must be an array");
                for (const auto& name : row.at("targets"))
                    targets.targets.push_back(Name(name));
                targets.child = Node(row.at("run"), path + "/run");
                if (row.contains("empty"))
                    targets.empty = Node(row.at("empty"), path + "/empty");
                operation = std::move(targets);
            } else if (op == "start") {
                StepFields(row, {"op", "id", "join", "run", "source"});
                operation = TaskAbilityCall{Name(row.at("id")), Node(row.at("run"), path + "/run"),
                                            row.value("join", TaskAbilityCall{}.wait_for_performance)};
            } else if (op == "join") {
                StepFields(row, {"op", "source"});
                operation = TaskWaitAbilities{};
            } else if (op == "animation") {
                StepFields(row, {"op", "as", "participant", "state", "layer", "transition", "start", "end", "track",
                             "restart", "current_idle", "source"});
                const auto layer = Integer(row.value("layer", Json(0)));
                Need(layer >= 0 && layer <= INT_MAX, "animation layer out of range");
                const auto transition = Number(row.value("transition", Json(0)));
                Need(transition >= 0 && transition <= std::numeric_limits<float>::max(),
                     "animation transition out of range");
                TaskAnimationCommand animation{
                    {Name(row.value("participant", Json(TaskAnimationCommand{}.participant.name)))},
                    Name(row.at("state")), static_cast<int>(layer), static_cast<float>(transition),
                    Float(row.value("start", Json(0)))};
                if (row.contains("end"))
                    animation.normalized_end = Float(row.at("end"));
                if (row.contains("restart")) {
                    Need(row.at("restart").is_boolean(), "animation restart must be boolean");
                    animation.restart = row.at("restart").get<bool>();
                }
                if (row.contains("current_idle")) {
                    Need(row.at("current_idle").is_boolean(), "current_idle must be boolean");
                    animation.current_idle = row.at("current_idle").get<bool>();
                }
                if (row.contains("track")) {
                    const auto& value = row.at("track");
                    Fields(value, {"logic", "extra", "release_on", "force", "previous_end"});
                    auto integer = [&](const Json& input) {
                        const auto number = Integer(input);
                        Need(number >= INT_MIN && number <= INT_MAX, "animation track enum out of range");
                        return static_cast<int>(number);
                    };
                    AnimationTrack track;
                    track.logic = integer(value.at("logic"));
                    track.extra = integer(value.value("extra", Json(track.extra)));
                    if (value.contains("release_on")) {
                        Need(value.at("release_on").is_array(), "invalid animation release policy");
                        for (const auto& item : value.at("release_on"))
                            track.release_on.push_back(integer(item));
                    }
                    track.force = value.value("force", track.force);
                    if (value.contains("previous_end")) {
                        track.previous_end = Float(value.at("previous_end"));
                        Need(*track.previous_end >= 0, "previous animation boundary must be nonnegative");
                    }
                    animation.track = std::move(track);
                    Need(animation.normalized_end.has_value(), "animation track requires end");
                }
                operation = TaskSubmit{Request(row, path), std::move(animation)};
            } else if (op == "animation_parameter") {
                operation = TaskSubmit{Request(row, path), animation_parameter_codec::Parse(row)};
            } else if (op == "expression") {
                StepFields(row, {"op", "as", "state", "transition", "current_idle", "source"});
                const auto transition = Float(row.value("transition", Json(.2)));
                Need(transition >= 0, "expression transition must be nonnegative");
                operation =
                    TaskSubmit{Request(row, path), TaskExpressionCommand{Name(row.at("state")), transition,
                                                                              row.value("current_idle", false)}};
            } else if (op == "ready_pose") {
                StepFields(row, {"op", "as", "config", "skill", "variant", "source"});
                const auto skill = Integer(row.at("skill")), variant = Integer(row.value("variant", Json(-1)));
                Need(skill >= 0 && skill <= INT_MAX && variant >= -1 && variant <= INT_MAX,
                     "ready pose index out of range");
                operation =
                    TaskSubmit{Request(row, path),
                               TaskPoseCommand{ReadyPoseSelection{Name(row.at("config")), static_cast<int>(skill),
                                                                  static_cast<int>(variant)}}};
            } else if (op == "reaction") {
                StepFields(row, {"op", "as", "config", "flag", "recover", "occurrence", "source"});
                const auto flag = Integer(row.at("flag"));
                Need(flag >= INT32_MIN && flag <= INT32_MAX, "reaction flag out of range");
                operation =
                    TaskSubmit{Request(row, path),
                               TaskPoseCommand{ReactionSelection{
                                   Name(row.at("config")), static_cast<std::int32_t>(flag), row.value("recover", false),
                                   ParseDeathOccurrence(row.value("occurrence", std::string("default")))}}};
            } else if (op == "hit") {
                StepFields(row, {"op", "as", "target", "animation", "direction", "effect", "source", "allow_position",
                             "events"});
                Need(row.at("animation").is_string(), "hit animation must be a source string");
                const auto animation = row.at("animation").get<std::string>();
                Need(animation.find('\0') == std::string::npos, "invalid hit animation");
                HitInput hit{animation};
                if (row.contains("direction")) {
                    const auto& direction = row.at("direction");
                    Fields(direction, {"horizontal", "vertical"});
                    hit.direction = HitAngles{Float(direction.value("horizontal", Json(0))),
                                              Float(direction.value("vertical", Json(0)))};
                }
                if (row.contains("effect")) {
                    const auto& value = row.at("effect");
                    Fields(value, {"path", "height", "angle", "delay", "minimum_interval", "maximum_count",
                                   "offset_by_scale"});
                    HitEffectInput effect;
                    effect.path = value.at("path").get<std::string>();
                    Need(effect.path.find('\0') == std::string::npos, "invalid hit effect path");
                    effect.height = Float(value.value("height", Json(effect.height)));
                    effect.angle = Float(value.value("angle", Json(effect.angle)));
                    effect.delay = Float(value.value("delay", Json(effect.delay)));
                    effect.minimum_interval = Float(value.value("minimum_interval", Json(effect.minimum_interval)));
                    const auto count = Integer(value.value("maximum_count", Json(effect.maximum_count)));
                    Need(count >= INT32_MIN && count <= INT32_MAX, "hit effect count out of range");
                    effect.maximum_count = static_cast<int>(count);
                    effect.offset_by_scale = value.value("offset_by_scale", effect.offset_by_scale);
                    hit.effect = std::move(effect);
                }
                TaskHitCommand command{Name(row.at("target")), std::move(hit), row.value("allow_position", false)};
                if (row.contains("events")) {
                    Need(row.at("events").is_array(), "hit events must be an ordered array");
                    for (const auto& event : row.at("events"))
                        command.events.push_back(ParseTaskEvent(Name(event)));
                }
                operation = TaskSubmit{Request(row, path), std::move(command)};
            } else if (op == "attack.record" || op == "attack.finish" || op == "attack.complete" ||
                       op == "attack.chunk_begin" || op == "attack.chunk_end") {
                TaskAttackCommand command;
                if (op == "attack.record") {
                    StepFields(row, {"op", "as", "target", "allow_position", "source"});
                    command.boundary = TaskAttackBoundary::Record;
                    command.target = Name(row.at("target"));
                    command.allow_position = row.value("allow_position", false);
                } else {
                    StepFields(row, {"op", "as", "source"});
                    command.boundary = op == "attack.finish"     ? TaskAttackBoundary::Finish
                                       : op == "attack.complete" ? TaskAttackBoundary::Complete
                                       : op == "attack.chunk_begin"     ? TaskAttackBoundary::BeginChunk
                                                                 : TaskAttackBoundary::EndChunk;
                }
                operation = TaskSubmit{Request(row, path), std::move(command)};
            } else if (op == "event.emit") {
                StepFields(row, {"op", "as", "event", "recipient", "sender", "allow_position", "source"});
                TaskEventCommand command;
                command.kind = ParseTaskEvent(Name(row.at("event")));
                command.recipient = Name(row.at("recipient"));
                command.source = Name(row.value("sender", Json("Caster")));
                command.allow_position = row.value("allow_position", false);
                operation = TaskSubmit{Request(row, path), std::move(command)};
            } else if (op == "state.value" || op == "playback.value") {
                StepFields(row, {"op", "as", "name", "variable", "operation", "value", "value_from", "value_input",
                             "source"});
                TaskLifetimeCommand command;
                if (op == "playback.value") {
                    Need(!row.contains("name"), "playback.value has no state name");
                    const auto property = Name(row.at("variable"));
                    Need(property == "selected" || property == "performance", "unknown playback property");
                    command.context = TaskPlaybackContext;
                } else command.context = Name(row.at("name"));
                command.operation = TaskLifetimeOperation::ChangeValue;
                const auto change = Name(row.at("operation"));
                Need(change == "set" || change == "add" || change == "multiply", "unknown number mutation");
                if (op == "playback.value") {
                    Need(change == "set" && !row.contains("value_from"), "playback values require a text assignment");
                    if (row.contains("value")) Need(row.at("value").is_string(), "playback value must be text");
                }
                Need(int(row.contains("value")) + int(row.contains("value_from")) + int(row.contains("value_input")) ==
                         1,
                     "context mutation requires one operand");
                if (row.contains("value_input")) {
                    Need(change == "set", "input context values support set only");
                    command.value_input = Name(row.at("value_input"));
                    command.set_value = TaskContextValueSet{Name(row.at("variable")), false};
                } else if (row.contains("value") &&
                           (change == "set" || row.at("value").is_boolean() || row.at("value").is_string())) {
                    Need(change == "set", "boolean/text context values support set only");
                    command.set_value = TaskContextValueSet{Name(row.at("variable")), Value(row.at("value"))};
                } else {
                    command.change = ContextNumberChange{Name(row.at("variable")),
                                                         change == "set"   ? ContextNumberOperation::Set
                                                         : change == "add" ? ContextNumberOperation::Add
                                                                           : ContextNumberOperation::Multiply};
                    if (row.contains("value"))
                        command.change->operand = ContextNumeric(row.at("value"));
                    else {
                        const auto& reference = row.at("value_from");
                        Fields(reference, {"context", "name"});
                        command.value_from =
                            TaskContextNumberReference{Name(reference.at("context")), Name(reference.at("name"))};
                    }
                }
                operation = TaskSubmit{Request(row, path), std::move(command)};
            } else if (op == "state.enter" || op == "state.exit" || op == "state.signal") {
                StepFields(row, {"op", "as", "name", "event", "source", "all", "only_caster"});
                TaskLifetimeCommand command;
                command.context = Name(row.at("name"));
                command.operation = op == "state.enter"  ? TaskLifetimeOperation::Enter
                                    : op == "state.exit" ? TaskLifetimeOperation::Exit
                                                           : TaskLifetimeOperation::Signal;
                if (op == "state.signal")
                    command.event = Name(row.at("event"));
                else
                    Need(!row.contains("event"), "event requires context_signal");
                command.all = row.value("all", false);
                if (row.contains("only_caster"))
                    command.only_caster = row.at("only_caster").get<bool>();
                Need(command.operation == TaskLifetimeOperation::Exit ||
                         (!row.contains("all") && !row.contains("only_caster")),
                     "removal selectors require context_exit");
                operation = TaskSubmit{Request(row, path), std::move(command)};
            } else if (op == "sound") {
                StepFields(row, {"op", "as", "participant", "event", "source"});
                operation =
                    TaskSubmit{Request(row, path),
                               TaskSoundCommand{
                                   {Name(row.value("participant", Json(TaskSoundCommand{}.participant.name)))},
                                   Name(row.at("event"))}};
            } else if (op == "movement") {
                StepFields(row, {"op", "as", "animation", "clock", "force_start", "segments", "source"});
                auto movement = Movement(row);
                Need(!movement.instant || !row.contains("animation"), "instant movement has no animation dependency");
                const auto animation = movement.instant ? std::string{} : Name(row.at("animation"));
                operation = TaskSubmit{Request(row, path), TaskMovementCommand{animation, std::move(movement)}};
            } else if (op == "projectiles") {
                StepFields(row, {"op", "as", "shots", "source"});
                VisualProjectilePlan plan;
                Need(row.at("shots").is_array(), "projectile shots must be an array");
                for (const auto& input : row.at("shots")) {
                    Fields(input, {"at", "parameters", "target", "values", "ordered"});
                    VisualProjectileShot shot;
                    shot.at = Float(input.value("at", Json(0)));
                    shot.parameters = Name(input.at("parameters"));
                    shot.target = Name(input.at("target"));
                    Need(visual.projectiles.count(shot.parameters) != 0,
                         "unknown projectile parameters: " + shot.parameters);
                    shot.ordered = input.value("ordered", true);
                    if (input.contains("values")) {
                        Need(input.at("values").is_object(), "projectile values must be an object");
                        for (auto it = input.at("values").begin(); it != input.at("values").end(); ++it)
                            shot.values.emplace(Name(it.key()), Float(it.value()));
                    }
                    plan.shots.push_back(std::move(shot));
                }
                ValidateVisualProjectiles(plan);
                operation = TaskSubmit{Request(row, path), std::move(plan)};
            } else if (op == "wait" && row.contains("for")) {
                StepFields(row, {"op", "for", "until", "progress", "source"});
                const auto signal = row.value("until", std::string(row.contains("progress") ? "progress" : "performance"));
                TaskWaitRequest wait;
                wait.request = Name(row.at("for"));
                wait.signal = Signal(signal);
                Need(signal == "progress" || !row.contains("progress"), "progress requires progress wait signal");
                if (signal == "progress")
                    wait.progress = Number(row.at("progress"));
                operation = wait;
            } else if (op == "wait" && row.contains("seconds")) {
                StepFields(row, {"op", "seconds", "source"});
                operation = TaskWaitSeconds{Number(row.at("seconds"))};
            } else if (op == "wait" && row.contains("frame")) {
                StepFields(row, {"op", "frame", "source"});
                Need(row.at("frame").is_boolean() && row.at("frame").get<bool>(), "frame wait requires true");
                operation = TaskWaitFrame{};
            } else if (op == "repeat") {
                StepFields(row, {"op", "run", "count", "until_finish", "source"});
                TaskRepeat repeat;
                repeat.child = Node(row.at("run"), path + "/run");
                if (row.contains("count")) {
                    const auto count = Integer(row.at("count"));
                    Need(count >= 0, "negative repeat count");
                    repeat.count = static_cast<std::uint64_t>(count);
                }
                repeat.until_finish = row.value("until_finish", false);
                operation = repeat;
            } else if (op == "on_success") {
                StepFields(row, {"op", "for", "run", "source"});
                operation = TaskOnSuccess{Name(row.at("for")), Node(row.at("run"), path + "/run")};
            } else if (op == "switch") {
                Fields(row, {"op", "name", "cases", "default", "source"});
                const auto name = Name(row.at("name"));
                const auto& cases = row.at("cases");
                Need(cases.is_array(), "switch cases must be an array");
                auto fallback = Node(row.value("default", Json::array()), path + "/default");
                std::set<TaskValue> values;
                for (std::size_t i = cases.size(); i > 0; --i) {
                    const auto& item = cases[i - 1];
                    Fields(item, {"is", "run", "label"});
                    const auto value = Value(item.at("is"));
                    Need(values.insert(value).second, "duplicate switch case");
                    if (item.contains("label")) Name(item.at("label"));
                    const auto item_path = path + "/cases/" + std::to_string(i - 1);
                    const auto branch = Node(item.at("run"), item_path + "/run");
                    const auto index = sequence.nodes.size();
                    sequence.nodes.push_back({item_path, TaskBranch{TaskValueEquals{name, value, {}, true}, branch, fallback}});
                    fallback = index;
                }
                return fallback;
            } else if (op == "if") {
                StepFields(row, {"op", "condition", "then", "elif", "else", "source"});
                auto fallback = Node(row.value("else", Json::array()), path + "/else");
                const auto alternatives = row.value("elif", Json::array());
                Need(alternatives.is_array(), "elif must be an array");
                for (std::size_t i = alternatives.size(); i > 0; --i) {
                    const auto& item = alternatives[i - 1];
                    Fields(item, {"condition", "then", "source"});
                    const auto item_path = path + "/elif/" + std::to_string(i - 1);
                    const auto branch = Node(item.at("then"), item_path + "/then");
                    const auto index = sequence.nodes.size();
                    sequence.nodes.push_back({item.value("source", item_path),
                        TaskBranch{Requirement(item.at("condition"), catalog), branch, fallback}});
                    fallback = index;
                }
                operation = TaskBranch{Requirement(row.at("condition"), catalog), Node(row.at("then"), path + "/then"),
                                       fallback};
            } else {
                Need(op != "animation.cued", "animation.cued must be the only operation in an action run");
                auto steps = visual_codec::Instructions(Json::array({row}), visual, path, true);
                operation = TaskSubmit{Request(row, path), std::move(steps.front())};
            }
            const auto index = sequence.nodes.size();
            sequence.nodes.push_back({row.contains("source") ? Name(row.at("source")) : path, std::move(operation)});
            if (row.contains("wait")) {
                const auto* submission = std::get_if<TaskSubmit>(&sequence.nodes.back().operation);
                Need(submission != nullptr, "wait suffix requires a submitted operation");
                const auto signal = Signal(Name(row.at("wait")));
                Need(signal != TaskWaitSignal::Progress, "progress waits require an explicit wait step");
                const auto wait_index = sequence.nodes.size();
                sequence.nodes.push_back({path + "/wait", TaskWaitRequest{submission->request, signal}});
                const auto serial = sequence.nodes.size();
                sequence.nodes.push_back({path, TaskSerial{{index, wait_index}}});
                return serial;
            }
            return index;
        } catch (const std::exception& error) {
            throw std::invalid_argument(path + ": " + error.what());
        }
    }
};
}
namespace {
TaskCatalog CompileScript(const Json& json) {
    Fields(json, {"format", "id", "applies_to", "default", "defaults", "resources", "states", "actions",
                  "functions", "initial_states", "scope", "restore", "signals"});
    Need(json.at("format") == "hksr.script", "unsupported script format");
    TaskCatalogInput out;
    out.id = Name(json.at("id"));
    out.domain = ActionDomain::Battle;
    const auto resources = json.value("resources", Json::object());
    Fields(resources, {"effects", "partners", "phases", "projectiles"});
    auto visual = std::make_shared<VisualProgram>(visual_codec::Resources(
        resources, out.id, Name(json.at("applies_to")), Name(json.value("scope", Json(out.id)))));
    out.visual = visual;
    const auto states = json.value("states", Json::object());
    Need(states.is_object(), "script states must be an object");
    for (auto it = states.begin(); it != states.end(); ++it) {
        Need(it.key() != TaskPlaybackContext, "playback state is reserved");
        visual->contexts.emplace(Name(it.key()), VisualProgramContext{});
    }
    std::set<std::string> scopes{visual->scope};
    for (auto it = states.begin(); it != states.end(); ++it) {
        auto context = visual_codec::Context(it.value(), *visual, "states/" + it.key());
        if (it.value().value("state_only", false) && !context.restore)
            context.restore = std::vector<VisualInstruction>{};
        if (!context.event_scope.empty())
            Need(scopes.insert(context.event_scope).second, "event scope has multiple state owners");
        visual->contexts.at(it.key()) = std::move(context);
    }
    if (json.contains("restore")) {
        visual->restore = visual_codec::Instructions(json.at("restore"), *visual, "restore");
        LocalInstructions(*visual->restore);
    }
    if (json.contains("signals")) {
        Need(json.at("signals").is_object(), "script signals must be an object");
        for (auto it = json.at("signals").begin(); it != json.at("signals").end(); ++it) {
            VisualProgramSignalDefinition signal;
            if (it.value().is_array())
                signal.instructions = visual_codec::Instructions(it.value(), *visual, "signals/" + it.key());
            else {
                Fields(it.value(), {"context", "run"});
                signal.context = Name(it.value().at("context"));
                Need(visual->contexts.count(signal.context) != 0, "unknown signal state");
                signal.instructions = visual_codec::Instructions(it.value().at("run"), *visual,
                                                                  "signals/" + it.key() + "/run");
                LocalInstructions(signal.instructions);
            }
            visual->signals.emplace(Name(it.key()), std::move(signal));
        }
    }
    if (json.contains("defaults")) {
        const auto& defaults = json.at("defaults");
        Fields(defaults, {"targets", "choices", "anchors"});
        const auto targets = defaults.value("targets", Json::object());
        Need(targets.is_object(), "default target definitions must be an object");
        for (auto it = targets.begin(); it != targets.end(); ++it) {
            Fields(it.value(), {"alias", "local_position"});
            Need(it.value().contains("alias") != it.value().contains("local_position"),
                 "default target requires alias or local_position");
            if (it.value().contains("alias"))
                visual->target_defaults.emplace(Name(it.key()), Name(it.value().at("alias")));
            else {
                const auto& position = it.value().at("local_position");
                Need(position.is_array() && position.size() == 3, "default position requires [x,y,z]");
                visual->target_defaults.emplace(Name(it.key()), api::Position3{Float(position[0]), Float(position[1]), Float(position[2])});
            }
        }
        ValidateVisualTargetDefaults(visual->target_defaults);
    }
    const auto functions = json.value("functions", Json::object());
    Need(functions.is_object(), "script functions must be an object");
    for (auto it = functions.begin(); it != functions.end(); ++it) {
        Need(it.value().is_array(), "script function must be a step array");
        SequenceParser parser{*visual, out.id, functions, {}};
        parser.active_references.insert(it.key());
        parser.sequence.entry = parser.Node(it.value(), "functions/" + it.key());
        out.sequences.emplace(Name(it.key()), std::make_shared<const TaskSequence>(std::move(parser.sequence)));
    }
    const auto& tasks = json.at("actions");

    Need(tasks.is_object(), "tasks must be an object");
    for (auto it = tasks.begin(); it != tasks.end(); ++it) {
        const auto& row = it.value();
        Fields(row, {"inputs", "requires", "tags", "run", "on_interrupt", "next", "bindings", "presentation", "mode", "on",
                     "trigger", "runtime"});
        if (row.contains("run") && row.at("run").is_array() && row.at("run").size() == 1 &&
            row.at("run")[0].is_object() && row.at("run")[0].value("op", std::string{}) == "animation.cued") {
            Fields(row, {"run"});
            visual->actions.emplace(Name(it.key()), visual_codec::CuedAction(row.at("run")[0], *visual,
                                                                            "actions/" + it.key() + "/run/0"));
            continue;
        }
        TaskPlan plan;
        auto& definition = plan.definition;
        definition.key = Key(it.key(), out.id);
        if (row.contains("mode")) {
            const auto mode = Name(row.at("mode"));
            Need(mode == "replace" || mode == "input", "unknown task execution mode");
            definition.mode = mode == "input" ? TaskExecutionMode::Input : TaskExecutionMode::Replace;
        }
        if (row.contains("trigger")) {
            const auto trigger = Name(row.at("trigger"));
            Need(trigger == "user" || trigger == "internal" || trigger == "event", "unknown task trigger");
            definition.trigger = trigger == "internal" ? TaskTrigger::Internal
                                 : trigger == "event"  ? TaskTrigger::Event
                                                       : TaskTrigger::User;
        }
        if (row.contains("on")) {
            const auto& hooks = row.at("on");
            Fields(hooks, {"accepted", "started", "completed", "interrupted", "replaced", "failed"});
            const std::map<std::string, std::vector<TaskStateWrite>*> events = {
                {"accepted", &definition.on.accepted},   {"started", &definition.on.started},
                {"completed", &definition.on.completed}, {"interrupted", &definition.on.interrupted},
                {"replaced", &definition.on.replaced},   {"failed", &definition.on.failed}};
            for (const auto& [event, writes] : events)
                if (hooks.contains(event)) {
                    Need(hooks.at(event).is_array(), "task state hook must be an array");
                    for (const auto& write : hooks.at(event)) {
                        Fields(write, {"context", "values"});
                        writes->push_back({Name(write.at("context")), Values(write.at("values"))});
                    }
                }
        }
        if (row.contains("runtime")) {
            const auto& runtime = row.at("runtime");
            Fields(runtime, {"selected", "policy"});
            TaskRuntimeTracking tracking;
            if (runtime.contains("selected")) tracking.selected = runtime.at("selected").get<std::string>();
            const auto policy = runtime.value("policy", std::string("performance"));
            if (policy == "ready") tracking.policy = TaskRuntimePolicy::Ready;
            else if (policy == "performance") tracking.policy = TaskRuntimePolicy::Performance;
            else if (policy == "continuous") tracking.policy = TaskRuntimePolicy::Continuous;
            else if (policy == "setting") tracking.policy = TaskRuntimePolicy::Setting;
            else throw std::invalid_argument("unknown action runtime policy: " + policy);
            definition.runtime = std::move(tracking);
        }
        if (row.contains("run")) {
            Need(row.at("run").is_array(), "action run must be a step array");
            SequenceParser parser{*visual, out.id, functions, {}};
            parser.sequence.entry = parser.Node(row.at("run"), "actions/" + it.key() + "/run");
            if (row.contains("on_interrupt")) {
                Need(row.at("on_interrupt").is_array(), "on_interrupt must be a step array");
                parser.sequence.interruption = parser.Node(row.at("on_interrupt"), "actions/" + it.key() + "/on_interrupt");
            }
            definition.entry = "@action/" + it.key();
            Need(out.sequences.emplace(definition.entry,
                 std::make_shared<const TaskSequence>(std::move(parser.sequence))).second, "action sequence collision");
        } else Need(!row.contains("on_interrupt"), "on_interrupt requires run");
        if (row.contains("presentation")) {
            const auto& value = row.at("presentation");
            Fields(value, {"label", "omissions", "listed", "kind", "contexts", "ready"});
            TaskPresentationInfo info;
            if (value.contains("ready")) {
                const auto& ready = value.at("ready");
                Fields(ready, {"config", "skill", "variant"});
                const auto skill = Integer(ready.at("skill")), variant = Integer(ready.value("variant", Json(-1)));
                Need(skill >= 0 && skill <= INT_MAX && variant >= -1 && variant <= INT_MAX,
                     "invalid script ready binding index");
                info.ready =
                    TaskReadyBinding{Name(ready.at("config")), static_cast<int>(skill), static_cast<int>(variant)};
            }
            const auto kind = value.value("kind", std::string("action"));
            Need(kind == "action" || kind == "setting" || kind == "hit_response", "unknown task presentation kind");
            info.setting = kind == "setting" || kind == "hit_response";
            if (value.contains("contexts"))
                for (const auto& context : value.at("contexts")) {
                    const auto name = Name(context);
                    Need(states.contains(name),
                         "unknown setting context: " + name);
                    info.contexts.push_back(name);
                }
            Need(!info.setting || (definition.mode == TaskExecutionMode::Input && !info.contexts.empty()),
                 "settings require a non-interrupting input and owned contexts");
            if (value.contains("label"))
                info.label = Name(value.at("label"));
            if (value.contains("omissions")) {
                Need(value.at("omissions").is_array(), "presentation omissions must be an array");
                for (const auto& omission : value.at("omissions"))
                    info.omissions.push_back(Name(omission));
            }
            info.listed = value.value("listed", true);
            definition.presentation = std::move(info);
        }
        if (row.contains("inputs")) {
            Need(row.at("inputs").is_object(), "task inputs must be an object");
            for (auto input = row.at("inputs").begin(); input != row.at("inputs").end(); ++input)
                definition.inputs.push_back(Input(input.key(), input.value()));
        }
        if (row.contains("requires")) {
            Need(row.at("requires").is_array(), "task prerequisites must be an array");
            for (const auto& condition : row.at("requires"))
                definition.requirements.push_back(Requirement(condition, out.id));
        }
        if (row.contains("bindings")) {
            const auto& bindings = row.at("bindings");
            Fields(bindings, {"targets", "anchors", "choices"});
            for (const auto* key : {"targets", "anchors"})
                if (bindings.contains(key)) {
                    Need(bindings.at(key).is_array(), "task binding names must be an array");
                    auto& names =
                        std::string(key) == "targets" ? definition.bindings.targets : definition.bindings.anchors;
                    for (const auto& name : bindings.at(key))
                        names.push_back(Name(name));
                }
            if (bindings.contains("choices")) {
                const auto& choices = bindings.at("choices");
                Need(choices.is_object(), "task choices must be an object");
                for (auto choice = choices.begin(); choice != choices.end(); ++choice) {
                    Need(choice.value().is_array(), "task choice options must be an array");
                    auto& options = definition.bindings.choices[Name(choice.key())];
                    for (const auto& value : choice.value())
                        options.push_back(Name(value));
                }
            }
        }
        if (row.contains("tags")) {
            Need(row.at("tags").is_array(), "task tags must be an array");
            for (const auto& tag : row.at("tags"))
                definition.tags.push_back(Name(tag));
        }
        if (row.contains("next")) {
            const auto& next = row.at("next");
            if (next.is_string()) plan.next = TaskContinuation{Key(next, out.id), {}};
            else {
                Fields(next, {"action", "inputs"});
                plan.next = TaskContinuation{Key(next.at("action"), out.id), Values(next.value("inputs", Json::object()))};
            }
        }
        out.tasks.emplace(it.key(), std::move(plan));
    }
    {
        for (auto it = states.begin(); it != states.end(); ++it) {
            const auto& row = it.value();
            Fields(row, {"values", "phase", "callbacks", "instances", "numbers", "hit_effects", "resilience",
                         "visual_reaction", "state_only", "configuration", "enter", "exit", "restore", "restore_effects", "event_scope"});
            TaskContextDefinition definition;
            if (row.contains("visual_reaction")) {
                const auto& reaction = row.at("visual_reaction");
                Fields(reaction, {"source", "flag", "occurrence"});
                const auto flag = Integer(reaction.at("flag"));
                Need(flag >= INT32_MIN && flag <= INT32_MAX, "invalid visual reaction flag");
                definition.visual_reaction =
                    TaskVisualReaction{Name(reaction.at("source")), static_cast<std::int32_t>(flag),
                                       ParseDeathOccurrence(reaction.value("occurrence", std::string("default")))};
            }
            definition.state_only = row.value("state_only", false);
            if (row.contains("resilience")) {
                const auto value = Integer(row.at("resilience"));
                Need(value >= 0 && value <= 4, "resilience override must be S through D");
                definition.resilience = static_cast<int>(value);
            }
            if (row.contains("hit_effects"))
                definition.hit_effects = hit_effects_codec::Parse(row.at("hit_effects"));
            if (row.contains("numbers")) {
                Need(row.at("numbers").is_object(), "context numbers must be an object");
                for (auto number = row.at("numbers").begin(); number != row.at("numbers").end(); ++number) {
                    const auto& data = number.value();
                    Fields(data, {"initial", "min", "max"});
                    ContextNumberRule rule;
                    rule.initial = ContextNumeric(data.at("initial"));
                    if (data.contains("min"))
                        rule.minimum = ContextNumeric(data.at("min"));
                    if (data.contains("max"))
                        rule.maximum = ContextNumeric(data.at("max"));
                    definition.numbers.emplace(Name(number.key()), std::move(rule));
                }
            }
            if (row.contains("instances")) {
                const auto& item = row.at("instances");
                Fields(item, {"name", "category", "receiver", "caster", "ability", "match", "repeat"});
                auto& policy = definition.instances.emplace();
                policy.name = Name(item.at("name"));
                if (item.contains("category"))
                    policy.category = Name(item.at("category"));
                if (item.contains("receiver"))
                    policy.receiver = Name(item.at("receiver"));
                if (item.contains("caster"))
                    policy.caster = Name(item.at("caster"));
                if (item.contains("ability"))
                    policy.ability = Name(item.at("ability"));
                const auto match = item.value("match", std::string("receiver"));
                Need(match == "receiver" || match == "receiver_caster" || match == "receiver_caster_ability",
                     "unknown context instance match");
                policy.match = match == "receiver"          ? TaskInstanceMatch::Receiver
                               : match == "receiver_caster" ? TaskInstanceMatch::ReceiverCaster
                                                            : TaskInstanceMatch::ReceiverCasterAbility;
                const auto repeat = item.value("repeat", std::string("keep"));
                Need(repeat == "keep" || repeat == "replace_inputs" || repeat == "new",
                     "unknown context instance repetition");
                policy.repeat = repeat == "keep"             ? TaskInstanceRepeat::Keep
                                : repeat == "replace_inputs" ? TaskInstanceRepeat::ReplaceInputs
                                                             : TaskInstanceRepeat::NewInstance;
            }
            if (row.contains("callbacks")) {
                const auto& callbacks = row.at("callbacks");
                Fields(callbacks, {"enter", "exit", "signals", "reapply", "activation", "events", "restore"});
                definition.callbacks.emplace();
                const auto activation = callbacks.value("activation", std::string("after_enter"));
                Need(activation == "after_enter" || activation == "before_callbacks",
                     "unknown context activation timing");
                definition.callbacks->activate_before_callbacks = activation == "before_callbacks";
                if (callbacks.contains("enter"))
                    definition.callbacks->enter = Name(callbacks.at("enter"));
                if (callbacks.contains("exit"))
                    definition.callbacks->exit = Name(callbacks.at("exit"));
                if (callbacks.contains("reapply"))
                    definition.callbacks->reapply = Name(callbacks.at("reapply"));
                if (callbacks.contains("restore"))
                    definition.callbacks->restore = Name(callbacks.at("restore"));
                if (callbacks.contains("signals")) {
                    Need(callbacks.at("signals").is_object(), "context callbacks must be an object");
                    for (auto signal = callbacks.at("signals").begin(); signal != callbacks.at("signals").end();
                         ++signal)
                        definition.callbacks->signals.emplace(Name(signal.key()), Name(signal.value()));
                }
                if (callbacks.contains("events")) {
                    Need(callbacks.at("events").is_object(), "context events must be an object");
                    for (auto event = callbacks.at("events").begin(); event != callbacks.at("events").end(); ++event)
                        definition.events.emplace(ParseTaskEvent(event.key()), Name(event.value()));
                }
            }
            definition.values = Values(row.value("values", Json::object()));
            if (row.contains("phase"))
                definition.phase = Name(row.at("phase"));
            out.contexts.emplace(Name(it.key()), std::move(definition));
        }
    }
    if (json.contains("initial_states"))
        for (const auto& name : json.at("initial_states").get<std::vector<Json>>())
            out.initial_contexts.push_back(Name(name));
    for (auto& [name, plan] : out.tasks) {
        const auto sequence = out.sequences.find(plan.definition.entry);
        if (sequence == out.sequences.end())
            continue;
        auto& names = plan.definition.bindings.collections;
        for (const auto& node : sequence->second->nodes) {
            std::string collection;
            if (const auto* targets = std::get_if<TaskForTargets>(&node.operation))
                collection = targets->collection;
            if (const auto* submit = std::get_if<TaskSubmit>(&node.operation))
                if (const auto* instruction = std::get_if<VisualInstruction>(&submit->operation))
                    if (const auto* model = std::get_if<VisualModelOperation>(&instruction->operation))
                        if (const auto* scale = std::get_if<VisualCharacterScale>(&model->command))
                            collection = scale->target_collection;
            if (!collection.empty() && std::find(names.begin(), names.end(), collection) == names.end())
                names.push_back(std::move(collection));
        }
    }
    return TaskCatalog(std::move(out));
}
}
std::shared_ptr<const VisualProgramInput> ParseScript(const std::string& text, ScriptImportReader imports) {
    const auto json = script_codec::Compose(Json::parse(text), imports);
    auto result = std::make_shared<VisualProgramInput>();
    result->tasks = std::make_shared<const TaskCatalog>(CompileScript(json));
    result->program = result->tasks->visual;
    if (json.contains("default")) result->default_selection = json.at("default").get<bool>();
    if (json.contains("defaults")) {
        const auto& defaults = json.at("defaults");
        result->choices = defaults.value("choices", Json::object()).get<std::map<std::string, std::string>>();
        result->anchors = ParseVisualSpatialAnchors(defaults.value("anchors", Json::object()).dump());
    }
    return result;
}
TaskCatalog ParseTaskCatalog(const std::string& text) {
    return *ParseScript(text)->tasks;
}
}
