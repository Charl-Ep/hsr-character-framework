#pragma once
#include "Localization.hpp"
#include <hksr/framework/application/ApplicationService.hpp>
#include <hksr/framework/catalog/VisualActionCatalog.hpp>
#include <sstream>
#include <locale>
#include <type_traits>

namespace hksr::framework::ui {
struct CharacterStatusLine {
    std::string text, error;
};
// Values used by presentation, not an animation/loading receipt or a guessed
// copy of gameplay variables. Rendering performs no game calls.
inline CharacterStatusLine CharacterStatus(const ApplicationSnapshot& snapshot, const SceneCharacterStatus& actor,
                                           Language language) {
    CharacterStatusLine out;
    out.error = !actor.error.empty() ? actor.error : actor.creation_error;
    if (out.error.empty())
        out.error = actor.playback_error;
    if (out.error.empty() && !actor.action && actor.handoff)
        out.error = actor.handoff->error;
    if (out.error.empty() && actor.action && actor.action->phase == ActionIntentPhase::Failed)
        out.error = actor.action->outcome == ActionOutcome::StateAbsent
                        ? Translate(UiText::StateMissing, language)
                        : actor.action->detail;
    if (out.error.empty() && !actor.action && actor.playback && actor.playback->last) {
        const auto& last = *actor.playback->last;
        if (last.outcome == ActionOutcome::StateAbsent)
            out.error = Translate(UiText::StateMissing, language);
        else if (last.outcome == ActionOutcome::Unavailable || last.outcome == ActionOutcome::CallFailed)
            out.error = last.detail;
    }
    if (!actor.active || !actor.enabled || !snapshot.scene || snapshot.scene->key != actor.scene)
        return out;
    auto append = [&](const std::string& value) {
        if (!out.text.empty())
            out.text += '\n';
        out.text += value;
    };
    auto format = [](const auto& input) {
        return std::visit(
            [](const auto& value) -> std::string {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, std::string>)
                    return value;
                else if constexpr (std::is_same_v<T, bool>)
                    return value ? "true" : "false";
                else {
                    std::ostringstream text;
                    text.imbue(std::locale::classic());
                    text << value;
                    return text.str();
                }
            },
            input);
    };
    if (actor.tasks) {
        const auto* actions = snapshot.catalog ? snapshot.catalog->Actions(actor.id) : nullptr;
        for (const auto& context : actor.tasks->control.contexts) {
            const auto before = out.text.size();
            if (context.visual_reaction) {
                std::string label;
                if (actions)
                    for (const auto& row : actions->actions.visual_states)
                        if (row.character_config == context.visual_reaction->source && row.value.flag.available &&
                            row.value.flag.value == context.visual_reaction->flag) {
                            auto selected = row;
                            if (context.visual_reaction->occurrence != DeathOccurrence::Configured) {
                                const auto names = actions->death_names.find(row.character_config);
                                if (names != actions->death_names.end())
                                    try {
                                        ApplyDeathOccurrence(selected, context.visual_reaction->occurrence,
                                                             names->second);
                                    } catch (const std::exception&) {
                                    }
                            }
                            label = selected.value.occur.resolved_state.value.empty()
                                        ? selected.value.loop.resolved_state.value
                                        : selected.value.occur.resolved_state.value;
                            break;
                        }
                append(std::string(Translate(UiText::PersistentState, language)) + ": " +
                       (label.empty() ? Translate(UiText::VisualEffect, language) : label));
            }
            if (context.phase && !context.phase->empty())
                append(std::string(Translate(UiText::BranchPhase, language)) + ": " + *context.phase);
            if (context.resilience)
                append(std::string(Translate(UiText::Resilience, language)) + ": " +
                       std::to_string(*context.resilience));
            if (!context.hit_effects.empty())
                append(std::string(Translate(UiText::HitEffectOverrides, language)) + ": " +
                       std::to_string(context.hit_effects.size()));
            for (const auto& [name, value] : context.values) {
                if (context.name == "actor" && (name == "selected" || name == "performance"))
                    continue;
                append((context.name.empty() ? std::string{} : context.name + ".") + name + " = " + format(value));
            }
            if (out.text.size() == before && context.name != "actor")
                append(context.name);
        }
        return out;
    }
    // Legacy visual programs expose genuine branch values without a task
    // controller. Do not replace an empty task state with actor bookkeeping.
    if (!actor.branch_state.phase.empty())
        append(std::string(Translate(UiText::BranchPhase, language)) + ": " + actor.branch_state.phase);
    for (const auto& item : actor.branch_state.values) {
        append((item.scope.empty() ? std::string{} : item.scope + ".") + item.name + " = " + format(item.value));
    }
    return out;
}
}
