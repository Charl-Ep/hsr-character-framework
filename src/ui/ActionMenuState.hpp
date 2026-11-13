#pragma once
#include "TaskInputDraft.hpp"

namespace hksr::framework::ui {
enum class ActionView { Exploration, Battle, Preview };

inline bool TaskInputChoiceVisible(const TaskInputChoice& choice, bool technical) {
    return technical || !choice.diagnostic;
}
inline std::size_t TaskInputChoiceCount(const TaskInputDefinition& input, bool technical) {
    return static_cast<std::size_t>(std::count_if(input.choices.begin(), input.choices.end(), [&](const auto& choice) {
        return TaskInputChoiceVisible(choice, technical);
    }));
}

inline bool HasEditableTaskInputs(const CharacterTaskEntry& entry, bool technical) {
    for (const auto& input : entry.inputs) {
        if (input.diagnostic && !technical)
            continue;
        if (!input.choices.empty() && TaskInputChoiceCount(input, technical) <= 1 && input.default_value)
            continue;
        return true;
    }
    return !entry.bindings.targets.empty() || !entry.bindings.anchors.empty() || !entry.bindings.collections.empty() ||
           std::any_of(entry.bindings.choices.begin(), entry.bindings.choices.end(),
                       [](const auto& row) { return row.second.size() > 1; });
}

inline bool ActionCanRequest(const CharacterTaskEntry& entry) {
    if (entry.query.availability == TaskAvailability::Available || CanConfigureTask(entry))
        return true;
    // Before the first battle handoff there is no battle Animator to inspect.
    // Preserve Unknown and let the normal handoff qualify the actual model;
    // do not override a real missing-state result or unrelated requirements.
    return entry.kind == CharacterTaskKind::Reaction && entry.query.availability == TaskAvailability::Unknown &&
           !entry.query.reasons.empty() &&
           std::all_of(entry.query.reasons.begin(), entry.query.reasons.end(), [](const auto& reason) {
               return reason.kind == TaskReasonKind::Unresolved && reason.source == "model.state";
           });
}

inline std::string ActionRowIdentity(const CharacterTaskEntry& entry) {
    if (const auto* command = std::get_if<ControlCharacterTask>(&entry.command); command && command->input.selection)
        return command->input.selection->task.catalog + "/" + command->input.selection->task.name;
    auto identity = std::to_string(static_cast<int>(entry.kind)) + "/" +
                    std::to_string(static_cast<int>(entry.domain)) + "/" + (entry.recovery ? "recover/" : "enter/") +
                    entry.label + "/" + entry.detail;
    for (const auto& source : entry.sources)
        identity += "/" + source.document + "/" + source.field;
    return identity;
}

inline bool ActionVisible(const CharacterTaskEntry& entry, bool npc, ActionView view, bool development,
                          bool show_unavailable) {
    if (view == ActionView::Preview && !development)
        return false;
    if (npc) {
        if (entry.kind != CharacterTaskKind::Npc)
            return false;
    } else {
        const bool authored = entry.kind == CharacterTaskKind::Sequence || entry.kind == CharacterTaskKind::Setting;
        // An explicit unlisted authoring flag excludes internal/test scripts,
        // not the public skill/reaction directory. Program alone is no such flag.
        if (!development && authored && entry.diagnostic)
            return false;
        const bool preview = entry.diagnostic || entry.kind == CharacterTaskKind::Program;
        if (view == ActionView::Preview) {
            if (!preview)
                return false;
        } else if (preview || entry.domain != (view == ActionView::Exploration ? ActionDomain::Exploration
                                                                               : ActionDomain::Battle))
            return false;
    }
    if (ActionCanRequest(entry) || (development && show_unavailable))
        return true;
    for (const auto& variant : entry.variants) {
        auto option = entry;
        option.query = variant.query;
        if (ActionCanRequest(option))
            return true;
    }
    return false;
}

class ActionVariantSettings {
  public:
    std::string Selected(const CharacterTaskEntry& entry, std::uint64_t actor, std::uint64_t creation) const {
        if (entry.variants.empty())
            return {};
        if (const auto saved = values_.find({actor, creation, ActionRowIdentity(entry)}); saved != values_.end())
            for (const auto& variant : entry.variants)
                if (variant.key == saved->second)
                    return variant.key;
        return entry.variants.front().key;
    }
    void Choose(const CharacterTaskEntry& entry, std::uint64_t actor, std::uint64_t creation, const std::string& key) {
        if (std::any_of(entry.variants.begin(), entry.variants.end(),
                        [&](const auto& variant) { return variant.key == key; }))
            values_[{actor, creation, ActionRowIdentity(entry)}] = key;
    }
    CharacterTaskEntry Apply(CharacterTaskEntry entry, std::uint64_t actor, std::uint64_t creation) const {
        const auto key = Selected(entry, actor, creation);
        for (const auto& variant : entry.variants)
            if (variant.key == key) {
                entry.command = variant.command;
                entry.query = variant.query;
                break;
            }
        return entry;
    }

  private:
    std::map<std::tuple<std::uint64_t, std::uint64_t, std::string>, std::string> values_;
};

// Remember explicit user choices for this exact actor birth and task identity.
// These are inputs, not cached execution permission: the backend still checks
// the current context when the action row is clicked.
class TaskInputSettings {
  public:
    CharacterTaskEntry Apply(CharacterTaskEntry entry) const {
        auto* command = std::get_if<ControlCharacterTask>(&entry.command);
        if (!command || !command->input.selection)
            return entry;
        if (const auto saved = values_.find(Key(*command)); saved != values_.end()) {
            command->input.selection->query = saved->second.query;
            command->input.selection->visual = saved->second.visual;
            command->input.selection->inputs_captured = false;
        }
        return entry;
    }
    void Remember(const ControlCharacterTask& command) {
        if (command.input.selection)
            values_[Key(command)] = *command.input.selection;
    }

  private:
    using Identity = std::tuple<std::uint64_t, std::uint64_t, TaskKey>;
    static Identity Key(const ControlCharacterTask& command) {
        return {command.id, command.creation, command.input.selection->task};
    }
    std::map<Identity, TaskSelection> values_;
};
}
