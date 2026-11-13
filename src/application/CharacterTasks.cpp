#include <hksr/framework/application/CharacterTasks.hpp>
#include <hksr/framework/tasks/TaskCatalog.hpp>
#include "ReactionDisplayComparison.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace hksr::framework {
bool HasTaskInputs(const CharacterTaskEntry& entry) {
    return !entry.inputs.empty() || !entry.bindings.targets.empty() || !entry.bindings.anchors.empty() ||
           !entry.bindings.choices.empty() || !entry.bindings.collections.empty();
}
bool CanConfigureTask(const CharacterTaskEntry& entry) {
    if ((entry.kind != CharacterTaskKind::Sequence && entry.kind != CharacterTaskKind::Setting) ||
        !HasTaskInputs(entry))
        return false;
    if (entry.query.availability == TaskAvailability::Available)
        return true;
    if (entry.query.reasons.empty())
        return false;
    return std::all_of(entry.query.reasons.begin(), entry.query.reasons.end(), [&](const auto& reason) {
        if (reason.kind == TaskReasonKind::MissingInput || reason.kind == TaskReasonKind::InputType)
            return true;
        if (reason.kind != TaskReasonKind::Condition)
            return false;
        return entry.bindings.choices.count(reason.source) ||
               std::any_of(entry.inputs.begin(), entry.inputs.end(),
                           [&](const auto& input) { return input.name == reason.source; });
    });
}
std::vector<CharacterTaskEntry> CharacterTasks(const ApplicationSnapshot& snapshot, std::uint64_t id) {
    const SceneCharacterStatus* actor = nullptr;
    for (const auto& row : snapshot.characters)
        if (row.id == id) {
            actor = &row;
            break;
        }
    if (!actor)
        throw std::out_of_range("character not found");
    std::vector<CharacterTaskEntry> result;
    const auto* catalog = snapshot.catalog ? snapshot.catalog->Actions(id) : nullptr;
    auto add = [&](CharacterTaskKind kind, ActionDomain domain, std::string label,
                   CharacterCommand command) -> CharacterTaskEntry& {
        CharacterTaskEntry entry;
        entry.kind = kind;
        entry.domain = domain;
        entry.label = std::move(label);
        entry.command = std::move(command);
        if (!actor->active || !actor->enabled || actor->deleting) {
            entry.query.availability = TaskAvailability::Unavailable;
            entry.query.reasons.push_back({TaskReasonKind::Participant, "body", "character is not active"});
        }
        result.push_back(std::move(entry));
        return result.back();
    };
    auto reading = [&](CharacterTaskEntry& entry) {
        if (catalog && !catalog->actions.Ready(entry.domain)) {
            entry.query.availability = TaskAvailability::Unavailable;
            entry.query.reasons.push_back({TaskReasonKind::Unresolved, "catalog", "action data is still being read"});
        }
    };
    if (const auto found = snapshot.npc_actions.find(id);
        found != snapshot.npc_actions.end() && found->second->creation == actor->creations)
        for (const auto& motion : found->second->source.motions)
            add(CharacterTaskKind::Npc, ActionDomain::Exploration,
                motion.name.available ? motion.name.value : motion.indexed_name,
                SelectCharacterAction{id, {ActionDomain::Exploration, NpcMotion{motion.id}}});
    const bool scripted_idle =
        actor->tasks && actor->tasks->domain == ActionDomain::Battle &&
        std::any_of(actor->tasks->definitions.begin(), actor->tasks->definitions.end(), [](const auto& row) {
            const auto& definition = row.second;
            return definition.trigger == TaskTrigger::User && definition.mode == TaskExecutionMode::Replace &&
                   definition.presentation && definition.presentation->listed &&
                   std::find(definition.tags.begin(), definition.tags.end(), "script-idle") != definition.tags.end();
        });
    if (!actor->battle_idle_state.empty() && !scripted_idle)
        add(CharacterTaskKind::Idle, ActionDomain::Battle, actor->battle_idle_state, IdleCharacter{id});
    if (catalog) {
        std::set<std::string> composed;
        if (!actor->battle_idle_state.empty())
            composed.insert(actor->battle_idle_state);
        if (actor->tasks)
            for (const auto& [name, definition] : actor->tasks->definitions) {
                (void)name;
                if (definition.presentation && definition.presentation->listed && !definition.presentation->setting)
                    composed.insert(definition.presentation->label);
            }
        for (const auto& set : catalog->actions.ready_actions)
            for (const auto& pose : set->poses)
                composed.insert(pose.state);
        for (const auto& visual : catalog->actions.visual_states)
            for (const auto* phase : {&visual.value.occur, &visual.value.loop, &visual.value.recover})
                if (phase->resolved_state.available && !phase->resolved_state.value.empty())
                    composed.insert(phase->resolved_state.value);
        for (const auto& set : catalog->actions.ready_actions)
            for (std::size_t i = 0; i < set->poses.size(); ++i) {
                const auto& pose = set->poses[i];
                const bool scripted =
                    actor->tasks &&
                    std::any_of(actor->tasks->definitions.begin(), actor->tasks->definitions.end(),
                                [&](const auto& row) {
                                    const auto& definition = row.second;
                                    if (definition.trigger == TaskTrigger::Internal ||
                                        definition.mode != TaskExecutionMode::Replace || !definition.presentation ||
                                        !definition.presentation->listed || !definition.presentation->ready)
                                        return false;
                                    const auto& ready = *definition.presentation->ready;
                                    return ready.source == set->source && ready.skill == pose.skill_index &&
                                           ready.variant == pose.variant_index;
                                });
                if (scripted)
                    continue;
                auto& entry = add(CharacterTaskKind::Ready, ActionDomain::Battle, pose.label,
                                  SelectCatalogAction{id, {catalog->revision, ReadyActionKey{set->source, i}}});
                entry.detail = pose.state;
                entry.sources.push_back({set->source, pose.skill_name});
                reading(entry);
            }
        for (const auto& proposal : catalog->actions.proposals) {
            auto& entry =
                add(CharacterTaskKind::Configured, proposal.domain, proposal.state,
                    SelectCatalogAction{id, {catalog->revision, ConfiguredActionKey{proposal.domain, proposal.state}}});
            const bool event_only =
                !proposal.references.empty() &&
                std::all_of(proposal.references.begin(), proposal.references.end(), [](const auto& reference) {
                    return reference.source.kind == api::ActionConfigKind::AnimationEvent && !reference.task;
                });
            entry.diagnostic =
                proposal.domain == ActionDomain::Battle && (composed.count(proposal.state) != 0 || event_only);
            for (const auto& reference : proposal.references)
                entry.sources.push_back({reference.source.path, reference.field});
            reading(entry);
        }
        struct VisualGroup {
            const VisualState* body;
            bool recover;
            std::size_t entry;
            std::vector<const VisualState*> policies;
        };
        std::vector<VisualGroup> visual_groups;
        auto qualify = [&](CharacterTaskEntry& entry, const VisualState& visual, bool recover) {
            const CharacterControllerActions* checked = nullptr;
            if (const auto found = snapshot.controller_actions.find(id); found != snapshot.controller_actions.end() &&
                                                                         found->second->creation == actor->creations &&
                                                                         found->second->checked_domain == entry.domain)
                checked = found->second.get();
            auto state = [&](const api::VisualPhaseInput& phase) {
                if (!phase.resolved_state.available || phase.resolved_state.value.empty())
                    return;
                const auto& name = phase.resolved_state.value;
                const api::Observed<bool>* membership = nullptr;
                if (checked)
                    if (const auto found = checked->checked_states.find({0, name});
                        found != checked->checked_states.end())
                        membership = &found->second;
                if (membership && membership->read.returned && membership->value && !actor->visual_phase_override)
                    return;
                const bool absent = membership && membership->read.returned && !actor->visual_phase_override;
                if (absent || entry.query.availability == TaskAvailability::Available)
                    entry.query.availability = absent ? TaskAvailability::Unavailable : TaskAvailability::Unknown;
                entry.query.reasons.push_back({absent ? TaskReasonKind::Unimplemented : TaskReasonKind::Unresolved,
                                               "model.state",
                                               absent ? "animation absent from the current model: " + name
                                                      : "animation membership awaits the target model: " + name});
            };
            if (recover)
                state(visual.value.recover);
            else {
                state(visual.value.occur);
                state(visual.value.loop);
            }
        };
        auto hit_label = [](const VisualState& visual) {
            const auto& policy = visual.value;
            std::string text;
            if (policy.force_hit_h.available && policy.force_hit_h.value)
                text = "Forced heavy hit";
            if (policy.reactions.present)
                for (const auto* input : {&policy.reactions.hit, &policy.reactions.hit_h, &policy.reactions.fly_hit,
                                          &policy.reactions.hit_fly})
                    if (input->available && !input->value.empty()) {
                        if (!text.empty())
                            text += " / ";
                        text += input->value;
                    }
            return text.empty() ? std::string("Default hit response") : text;
        };
        for (const auto& visual : catalog->actions.visual_states)
            if (visual.value.flag.available)
                for (bool recover : {false, true}) {
                    const auto& value = visual.value;
                    auto label = value.occur.resolved_state.value.empty() ? value.loop.resolved_state.value
                                                                          : value.occur.resolved_state.value;
                    if (recover)
                        label = value.recover.resolved_state.value;
                    if (label.empty())
                        label = "Visual state";
                    if (visual.expression.available && !visual.expression.value.empty())
                        label += " · " + visual.expression.value;
                    auto& entry =
                        add(CharacterTaskKind::Reaction, ActionDomain::Battle, std::move(label),
                            SelectCatalogAction{id,
                                                {catalog->revision,
                                                 VisualActionKey{visual.character_config, value.flag.value, recover}}});
                    entry.recovery = recover;
                    entry.detail = value.occur.resolved_state.value + " → " + value.loop.resolved_state.value + " → " +
                                   value.recover.resolved_state.value;
                    entry.sources.push_back({visual.source, "flag=" + std::to_string(value.flag.value)});
                    if (!visual.expression_source.empty())
                        entry.sources.push_back({visual.expression_source, "expression"});
                    const auto query = QueryVisualAction(visual, recover);
                    if (!query.available)
                        entry.query.availability = TaskAvailability::Unavailable;
                    for (const auto& issue : query.issues)
                        entry.query.reasons.push_back({TaskReasonKind::Unimplemented, visual.source, issue});
                    qualify(entry, visual, recover);
                    CharacterTaskVariant variant{visual.character_config + ":" + std::to_string(value.flag.value) +
                                                     (recover ? ":recover" : ":enter"),
                                                 hit_label(visual), entry.detail, entry.command, entry.query};
                    const auto group = std::find_if(visual_groups.begin(), visual_groups.end(), [&](const auto& row) {
                        return row.recover == recover && detail::SameVisualBody(*row.body, visual, recover);
                    });
                    if (group == visual_groups.end()) {
                        entry.variants.push_back(std::move(variant));
                        visual_groups.push_back({&visual, recover, result.size() - 1, {&visual}});
                    } else {
                        const auto aliases = entry.sources;
                        result.pop_back();
                        auto& first = result[group->entry];
                        first.sources.insert(first.sources.end(), aliases.begin(), aliases.end());
                        if (recover)
                            group->policies.push_back(&visual);
                        if (!recover &&
                            std::none_of(group->policies.begin(), group->policies.end(), [&](const auto* prior) {
                                return detail::SameVisualHitPolicy(*prior, visual);
                            })) {
                            first.variants.push_back(std::move(variant));
                            group->policies.push_back(&visual);
                        }
                    }
                }
        const auto* active_reaction = actor->tasks && actor->tasks->control.manual_reaction
                                          ? &*actor->tasks->control.manual_reaction
                                          : nullptr;
        for (const auto& group : visual_groups)
            if (group.recover) {
                auto& entry = result[group.entry];
                const auto member = std::find_if(group.policies.begin(), group.policies.end(), [&](const auto* visual) {
                    return active_reaction && visual->character_config == active_reaction->source &&
                           visual->value.flag.value == active_reaction->flag;
                });
                if (member == group.policies.end()) {
                    entry.query.availability = TaskAvailability::Unavailable;
                    entry.query.reasons.push_back(
                        {TaskReasonKind::Condition, "context", "no matching active visual state"});
                    for (auto& variant : entry.variants)
                        variant.query = entry.query;
                    continue;
                }
                const auto& visual = **member;
                entry.command = SelectCatalogAction{
                    id, {catalog->revision, VisualActionKey{visual.character_config, visual.value.flag.value, true}}};
                entry.query = {};
                if (!actor->active || !actor->enabled || actor->deleting)
                    entry.query.availability = TaskAvailability::Unavailable;
                const auto playable = QueryVisualAction(visual, true);
                if (!playable.available)
                    entry.query.availability = TaskAvailability::Unavailable;
                for (const auto& issue : playable.issues)
                    entry.query.reasons.push_back({TaskReasonKind::Unimplemented, visual.source, issue});
                qualify(entry, visual, true);
                entry.variants = {{visual.character_config + ":" + std::to_string(visual.value.flag.value) + ":recover",
                                   entry.label, entry.detail, entry.command, entry.query}};
            }
        for (const auto& group : visual_groups)
            if (!group.recover && api::IsDeathVisualBehavior(group.body->value)) {
                auto& entry = result[group.entry];
                const auto names = catalog->death_names.find(group.body->character_config);
                if (names == catalog->death_names.end())
                    continue;
                const auto defaults = entry.variants;
                for (const auto& original : defaults)
                    for (auto slot :
                         {DeathOccurrence::Die, DeathOccurrence::Hit, DeathOccurrence::Fly, DeathOccurrence::Limbo}) {
                        const auto& original_key =
                            std::get<VisualActionKey>(std::get<SelectCatalogAction>(original.command).action.key);
                        const auto policy =
                            std::find_if(group.policies.begin(), group.policies.end(),
                                         [&](const auto* row) { return row->value.flag.value == original_key.flag; });
                        auto selected = policy == group.policies.end() ? *group.body : **policy;
                        try {
                            ApplyDeathOccurrence(selected, slot, names->second);
                        } catch (const std::exception&) {
                            continue;
                        }
                        CharacterTaskEntry candidate;
                        candidate.kind = CharacterTaskKind::Reaction;
                        candidate.domain = ActionDomain::Battle;
                        const auto playable = QueryVisualAction(selected, false);
                        if (!playable.available)
                            continue;
                        if (!actor->active || !actor->enabled || actor->deleting)
                            candidate.query.availability = TaskAvailability::Unavailable;
                        qualify(candidate, selected, false);
                        if (candidate.query.availability == TaskAvailability::Unavailable)
                            continue; // confirmed absent, not a proposed playable slot
                        auto command = original.command;
                        auto& key = std::get<VisualActionKey>(std::get<SelectCatalogAction>(command).action.key);
                        key.occurrence = slot;
                        entry.variants.push_back({original.key + ":" + DeathOccurrenceName(slot),
                                                  selected.value.occur.resolved_state.value, entry.detail,
                                                  std::move(command), std::move(candidate.query)});
                    }
                for (auto& variant : entry.variants)
                    if (std::get<VisualActionKey>(std::get<SelectCatalogAction>(variant.command).action.key)
                            .occurrence == DeathOccurrence::Configured)
                        variant.label = defaults.size() == 1 ? "Default" : "Default · " + variant.label;
                entry.variants.erase(
                    std::remove_if(entry.variants.begin(), entry.variants.end(),
                                   [](const auto& variant) {
                                       return variant.query.availability == TaskAvailability::Unavailable &&
                                              std::any_of(variant.query.reasons.begin(), variant.query.reasons.end(),
                                                          [](const auto& reason) {
                                                              return reason.source == "model.state" &&
                                                                     reason.kind == TaskReasonKind::Unimplemented;
                                                          });
                                   }),
                    entry.variants.end());
                if (entry.query.availability == TaskAvailability::Unavailable)
                    for (const auto& variant : entry.variants)
                        if (variant.query.availability != TaskAvailability::Unavailable) {
                            entry.command = variant.command;
                            entry.query = variant.query;
                            break;
                        }
            }
    }
    for (const auto& [key, state] : actor->program_actions) {
        auto& entry =
            add(CharacterTaskKind::Program, ActionDomain::Battle, key,
                SelectCharacterAction{id, {ActionDomain::Battle, ProgramAction{actor->visual_program, key}}});
        entry.detail = state;
    }
    if (actor->tasks)
        for (const auto& [name, definition] : actor->tasks->definitions) {
            if (definition.trigger == TaskTrigger::Internal)
                continue;
            TaskSelection selection{{actor->task_catalog, name}};
            auto& entry =
                add(CharacterTaskKind::Sequence, actor->tasks->domain, name,
                    ControlCharacterTask{id, actor->creations, {TaskControlOperation::Select, std::move(selection)}});
            if (definition.presentation) {
                const auto& display = *definition.presentation;
                if (display.setting) {
                    entry.kind = CharacterTaskKind::Setting;
                    entry.setting_contexts = display.contexts;
                }
                entry.diagnostic = !display.listed;
                if (!display.label.empty())
                    entry.label = display.label;
                for (const auto& omission : display.omissions) {
                    if (!entry.detail.empty())
                        entry.detail += '\n';
                    entry.detail += omission;
                }
            }
            entry.sources = definition.sources;
            entry.inputs = definition.inputs;
            entry.bindings = TaskBindings(definition);
            entry.mode = definition.mode;
            entry.trigger = definition.trigger;
            const auto found = actor->tasks->control.catalog.find(name);
            if (found == actor->tasks->control.catalog.end()) {
                entry.query.availability = TaskAvailability::Unknown;
                entry.query.reasons.push_back({TaskReasonKind::Unresolved, name, "task session is not prepared"});
            } else {
                const auto prior = entry.query;
                entry.query = found->second;
                if (prior.availability != TaskAvailability::Available)
                    entry.query.availability = prior.availability;
                entry.query.reasons.insert(entry.query.reasons.end(), prior.reasons.begin(), prior.reasons.end());
            }
            if (actor->tasks->catalog &&
                !actor->tasks->catalog->Prepared(name).has_presentation) {
                entry.diagnostic = true;
                if (!entry.detail.empty())
                    entry.detail += '\n';
                entry.detail += "entry has no independent visual/audio presentation";
            }
            if (!actor->visual_phase_override && std::find(definition.tags.begin(), definition.tags.end(),
                                                           "visual-phase-reset") != definition.tags.end()) {
                entry.query.availability = TaskAvailability::Unavailable;
                entry.query.reasons.push_back({TaskReasonKind::Condition, "phase", "no active visual phase"});
            }
        }
    auto order = [](CharacterTaskKind kind) {
        switch (kind) {
        case CharacterTaskKind::Sequence:
        case CharacterTaskKind::Npc:
            return 0;
        case CharacterTaskKind::Program:
            return 1;
        case CharacterTaskKind::Idle:
            return 2;
        case CharacterTaskKind::Ready:
            return 3;
        case CharacterTaskKind::Reaction:
            return 4;
        default:
            return 5;
        }
    };
    std::stable_sort(result.begin(), result.end(),
                     [&](const auto& a, const auto& b) { return order(a.kind) < order(b.kind); });
    return result;
}
}
