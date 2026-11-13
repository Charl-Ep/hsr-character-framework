#include "LegacyScriptFixture.hpp"
// Offline upgrade of retained regression fixtures. Product readers only accept hksr.script.
#include <hksr/framework/tasks/TaskCatalog.hpp>
#include <hksr/framework/presentation/scene/VisualProgramLibrary.hpp>
#include <nlohmann/json.hpp>
#include <map>
#include <stdexcept>

namespace hksr::framework::test {
using FixtureJson = nlohmann::json;
std::string VisualOperation(const std::string& op) {
    static const std::map<std::string, std::string> names = {
        {"create","effect.create"},{"remove","effect.remove"},{"progress","effect.progress"},
        {"trigger","effect.trigger"},{"follow","effect.follow"},{"effect_state","effect.state"},
        {"effect_parameter","effect.parameter"},{"effect_layout","effect.layout"},
        {"remove_event_effect","effect.remove_event"},{"assign","variable.set"},{"face_target","facing.target"},
        {"attachment_visibility","attachment.visibility"},{"attachment_scale","attachment.scale"},
        {"layer_weight","animation.layer_weight"},{"lock_animator_speed","animation.speed"},
        {"actor_time_scale","actor.time_scale"},{"renderer_mask","render.mask"},
        {"outline_targets","render.outline"},{"entity_visible","render.visibility"},
        {"model_change","model.change"},{"model_configuration","model.configuration"},
        {"model_scale","model.scale"},{"model_scale_follow","model.follow_scale"},
        {"character_scale","model.character_scale"},{"emotion_enable","expression.enable"},
        {"face_material","expression.material"},{"context_enter","scope.enter"},{"context_exit","scope.exit"},
        {"partner_create","partner.create"},{"partner_remove","partner.remove"},{"partner_attach","partner.attach"},
        {"partner_animation_follow","partner.animation_follow"},{"partner_visibility_follow","partner.visibility_follow"},
        {"partner_visibility","partner.visibility"},{"partner_idle","partner.idle"},
        {"timeline_play","timeline.play"},{"timeline_stop","timeline.stop"},{"bgm_sync","bgm.sync"}};
    const auto found = names.find(op);
    return found == names.end() ? op : found->second;
}
FixtureJson UpgradeVisual(FixtureJson row) {
    row["op"] = VisualOperation(row.at("op").get<std::string>());
    return row;
}
FixtureJson UpgradeVisualSteps(const FixtureJson& rows) {
    if (!rows.is_array()) throw std::invalid_argument("legacy visual instructions must be an array");
    auto result = FixtureJson::array();
    for (const auto& row : rows) result.push_back(UpgradeVisual(row));
    return result;
}
FixtureJson UpgradeRequirement(FixtureJson row) {
    if (row.at("op") == "context") row["op"] = "state";
    return row;
}
struct FixtureUpgrade {
    const FixtureJson& definitions;
    FixtureJson Steps(const FixtureJson& node) const {
        if (node.at("op") == "serial") {
            auto result = FixtureJson::array();
            const auto children = node.value("children", FixtureJson::array());
            if (!children.is_array()) throw std::invalid_argument("legacy serial children must be an array");
            for (const auto& item : children) result.push_back(Step(item));
            return result;
        }
        return FixtureJson::array({Step(node)});
    }
    FixtureJson Step(FixtureJson row) const {
        const auto op = row.at("op").get<std::string>();
        if (op == "serial") return Steps(row);
        if (op == "visual") {
            auto result = UpgradeVisual(row.at("instruction"));
            result["as"] = row.at("request");
            return result;
        }
        if (row.contains("request")) { row["as"] = row.at("request"); row.erase("request"); }
        if (op == "call_sequence") {
            const auto key = row.at("sequence").get<std::string>();
            if (definitions.contains(key) && definitions.at(key).contains("interruption"))
                throw std::invalid_argument("legacy called function owns an interruption");
            row["op"] = "call"; row["function"] = row.at("sequence"); row.erase("sequence");
        } else if (op == "omitted") row["op"] = "skip";
        else if (op == "parallel") {
            row["branches"] = FixtureJson::array();
            const auto children = row.value("children", FixtureJson::array());
            if (!children.is_array()) throw std::invalid_argument("legacy parallel children must be an array");
            for (const auto& child : children) row["branches"].push_back(Steps(child));
            row.erase("children");
        } else if (op == "branch") {
            row["op"] = "if"; row["condition"] = UpgradeRequirement(row.at("condition"));
            row["then"] = Steps(row.at("yes")); row["else"] = Steps(row.at("no")); row.erase("yes"); row.erase("no");
        } else if (op == "repeat" || op == "call_ability" || op == "for_targets" || op == "on_success") {
            row["run"] = Steps(row.at("child")); row.erase("child");
            if (op == "call_ability") {
                row["op"] = "start"; row["id"] = row.at("ability"); row.erase("ability");
                if (row.contains("wait_for_performance")) { row["join"] = row.at("wait_for_performance"); row.erase("wait_for_performance"); }
            } else if (op == "for_targets") {
                row["op"] = "for_each";
                if (row.contains("empty")) row["empty"] = Steps(row.at("empty"));
            } else if (op == "on_success") { row["for"] = row.at("as"); row.erase("as"); }
        } else if (op == "wait_abilities") row["op"] = "join";
        else if (op == "wait_request") {
            row["op"] = "wait"; row["for"] = row.at("as"); row.erase("as");
            if (row.contains("signal")) { row["until"] = row.at("signal"); row.erase("signal"); }
        } else if (op == "wait_seconds" || op == "wait_frame") {
            row["op"] = "wait"; if (op == "wait_frame") row["frame"] = true;
        } else {
            static const std::map<std::string,std::string> names = {
                {"context_enter","state.enter"},{"context_exit","state.exit"},
                {"context_signal","state.signal"},{"context_value","state.value"},
                {"visual_event","event.emit"},{"attack_record","attack.record"},
                {"attack_finish","attack.finish"},{"attack_complete","attack.complete"},
                {"chunk_begin","attack.chunk_begin"},{"chunk_end","attack.chunk_end"}};
            if (const auto found = names.find(op); found != names.end()) row["op"] = found->second;
        }
        return row;
    }
};
FixtureJson UpgradeLegacyScript(const FixtureJson& legacy) {
    const auto format = legacy.value("format", std::string{});
    if (format == "hksr.script") return legacy;
    const bool catalog = format == "hksr.task-catalog";
    if (!catalog && format != "hksr.visual-program") return legacy;
    if (catalog && legacy.at("domain") != "battle" && legacy.at("domain") != "exploration")
        throw std::invalid_argument("invalid legacy fixture domain");
    const auto& visual = catalog ? legacy.at("visual") : legacy;
    FixtureJson result = {{"format","hksr.script"},{"id",legacy.at("id")},{"applies_to",visual.at("source")},
                          {"scope",visual.at("scope")},{"resources",FixtureJson::object()},
                          {"actions",FixtureJson::object()},{"states",FixtureJson::object()}};
    for (const auto* key : {"effects","partners","phases","projectiles"})
        if (visual.contains(key)) result["resources"][key] = visual.at(key);
    if (visual.contains("target_defaults")) result["defaults"]["targets"] = visual.at("target_defaults");
    if (visual.contains("restore")) result["restore"] = UpgradeVisualSteps(visual.at("restore"));
    if (visual.contains("signals")) {
        result["signals"] = visual.at("signals");
        for (auto& row : result["signals"]) {
            if (row.is_array()) row = UpgradeVisualSteps(row);
            else { row["run"] = UpgradeVisualSteps(row.at("instructions")); row.erase("instructions"); }
        }
    }
    const auto visual_states = visual.value("contexts",FixtureJson::object());
    for (auto it = visual_states.begin(); it != visual_states.end(); ++it) {
        auto state = it.value();
        if (state.contains("phase")) { state["configuration"] = state.at("phase"); state.erase("phase"); }
        for (const auto* key : {"enter","exit","restore"})
            if (state.contains(key)) state[key] = UpgradeVisualSteps(state.at(key));
        result["states"][it.key()] = std::move(state);
    }
    const auto old_sequences = legacy.value("sequences",FixtureJson::object());
    const FixtureUpgrade upgrade{old_sequences};
    if (catalog) {
        for (auto it = old_sequences.begin(); it != old_sequences.end(); ++it)
            result["functions"][it.key()] = upgrade.Steps(it.value().at("entry"));
        const auto old_states = legacy.value("contexts",FixtureJson::object());
        for (auto it = old_states.begin(); it != old_states.end(); ++it) {
            if (!result["states"].contains(it.key())) throw std::invalid_argument("missing fixture visual state");
            result["states"][it.key()].update(it.value());
        }
        if (legacy.contains("initial_contexts")) result["initial_states"] = legacy.at("initial_contexts");
        for (auto it = legacy.at("tasks").begin(); it != legacy.at("tasks").end(); ++it) {
            auto task = it.value();
            if (task.contains("entry")) {
                const auto& sequence = old_sequences.at(task.at("entry").get<std::string>());
                task["run"] = upgrade.Steps(sequence.at("entry"));
                if (sequence.contains("interruption")) task["on_interrupt"] = upgrade.Steps(sequence.at("interruption"));
            }
            task.erase("entry"); task.erase("sources");
            if (task.contains("requires")) for (auto& condition : task["requires"]) condition = UpgradeRequirement(condition);
            if (task.contains("next")) { task["next"]["action"] = task["next"].at("task"); task["next"].erase("task"); }
            result["actions"][it.key()] = std::move(task);
        }
    }
    const auto old_actions = visual.value("actions",FixtureJson::object());
    for (auto it = old_actions.begin(); it != old_actions.end(); ++it) {
        const auto& action = it.value();
        FixtureJson cue = {{"op","animation.cued"},{"state",action.at("state")}};
        if (action.contains("restart")) cue["restart_scope"] = action.at("restart");
        if (action.contains("complete_at")) cue["complete_at"] = action.at("complete_at");
        if (action.contains("enter")) cue["prepare"] = UpgradeVisualSteps(action.at("enter"));
        if (action.contains("lanes")) {
            cue["tracks"] = FixtureJson::array();
            for (const auto& lane : action.at("lanes")) {
                auto track = FixtureJson::array();
                for (const auto& item : lane) track.push_back({{"at",item.at("at")},{"run",UpgradeVisualSteps(item.at("instructions"))}});
                cue["tracks"].push_back(std::move(track));
            }
        }
        if (action.contains("completion_state")) {
            cue["then"]["state"] = action.at("completion_state");
            if (action.contains("completion_at")) cue["then"]["at"] = action.at("completion_at");
            if (action.contains("after_completion")) cue["then"]["run"] = UpgradeVisualSteps(action.at("after_completion"));
        } else if (action.contains("completion_at") || action.contains("after_completion"))
            throw std::invalid_argument("legacy tail has no successor");
        result["actions"][it.key()] = {{"run",FixtureJson::array({std::move(cue)})}};
    }
    return result;
}
TaskCatalog ParseLegacyTaskCatalog(const std::string& text) {
    const auto original = FixtureJson::parse(text);
    if (original.value("format",std::string{}) == "hksr.script") return ParseTaskCatalog(text);
    auto result = TaskCatalogInput(ParseTaskCatalog(UpgradeLegacyScript(original).dump()));
    result.domain = original.at("domain") == "exploration" ? ActionDomain::Exploration : ActionDomain::Battle;
    for (const auto& [key, task] : result.tasks) {
        const auto& source = original.at("tasks").at(key);
        if (source.contains("entry"))
            result.sequences[source.at("entry").get<std::string>()] = result.sequences.at(task.definition.entry);
    }
    for (auto it = result.sequences.begin(); it != result.sequences.end();) {
        if (it->first.compare(0,8,"@action/") == 0) it = result.sequences.erase(it);
        else {
            ++it;
        }
    }
    for (auto& [key, task] : result.tasks) {
        task.definition.entry = original.at("tasks").at(key).value("entry",std::string{});
        const auto sources = original.at("tasks").at(key).value("sources",FixtureJson::array());
        if (!sources.is_array()) throw std::invalid_argument("fixture sources must be an array");
        for (const auto& source : sources) {
            const auto document = source.at("document").get<std::string>();
            const auto field = source.at("field").get<std::string>();
            if (document.empty() || field.empty() || document.find('\0') != std::string::npos ||
                field.find('\0') != std::string::npos)
                throw std::invalid_argument("fixture source location is invalid");
            task.definition.sources.push_back({document,field});
        }
    }
    auto visual = std::make_shared<VisualProgram>(*result.visual);
    visual->id = original.at("visual").at("id"); result.visual = std::move(visual);
    return TaskCatalog(result);
}
VisualProgram ParseLegacyVisualProgram(const std::string& text) {
    const auto original = FixtureJson::parse(text);
    const auto format = original.value("format",std::string{});
    if (format != "hksr.visual-program" && format != "hksr.script")
        throw std::invalid_argument("invalid legacy visual fixture format");
    return ParseVisualProgram(UpgradeLegacyScript(original).dump());
}
}
using hksr::framework::test::ParseLegacyTaskCatalog;
using hksr::framework::test::ParseLegacyVisualProgram;
