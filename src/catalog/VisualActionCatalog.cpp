#include <hksr/framework/catalog/VisualActionCatalog.hpp>
#include <cmath>
#include <stdexcept>

namespace hksr::framework {
void ApplyDeathOccurrence(VisualState& row, DeathOccurrence slot, const api::VisualReactionInput& defaults,
                          const api::VisualReactionInput* phase) {
    row.occurrence = slot;
    if (slot == DeathOccurrence::Configured || row.recover)
        return;
    row.death_defaults = defaults;
    if (!api::IsDeathVisualBehavior(row.value))
        throw std::invalid_argument("death occurrence requires an official death visual row");
    auto field = [&](const auto& names) -> const api::TextInput& {
        switch (slot) {
        case DeathOccurrence::Die:
            return names.die;
        case DeathOccurrence::Hit:
            return names.die_hit;
        case DeathOccurrence::Fly:
            return names.die_fly;
        case DeathOccurrence::Limbo:
            return names.die_limbo;
        default:
            throw std::invalid_argument("invalid death occurrence");
        }
    };
    const api::VisualReactionInput* sources[] = {row.value.reactions.present ? &row.value.reactions : nullptr, phase,
                                                 &defaults};
    for (const auto* names : sources)
        if (names && names->present) {
            const auto& value = field(*names);
            if (!value.available)
                throw std::runtime_error("death animation name unread: " + value.error);
            if (value.value.empty())
                continue;
            row.value.occur.state = row.value.occur.resolved_state = value;
            return;
        }
    throw std::runtime_error("death occurrence has no configured animation");
}
VisualActionAvailability QueryVisualAction(const VisualState& row, bool recover) {
    VisualActionAvailability out;
    auto issue = [&](std::string detail) {
        out.available = false;
        out.issues.push_back(std::move(detail));
    };
    bool body = false;
    auto phase = [&](const api::VisualPhaseInput& value, const char* name) {
        if (!value.resolved_state.available) {
            issue(std::string(name) + ": " + value.resolved_state.error);
            return;
        }
        if (value.resolved_state.value.empty())
            return;
        body = true;
        if (!value.transition.available || !std::isfinite(value.transition.value) || value.transition.value < 0)
            issue(std::string(name) + ": transition unavailable");
    };
    if (recover)
        phase(row.value.recover, "recover");
    else {
        phase(row.value.occur, "occur");
        phase(row.value.loop, "loop");
    }
    if (!body)
        issue("no animation in this visual phase");
    if (!recover) {
        if (!row.expression.available)
            issue("expression: " + row.expression.error);
        else if (!row.expression.value.empty() &&
                 (!row.expression_transition.available || !std::isfinite(row.expression_transition.value) ||
                  row.expression_transition.value < 0))
            issue("expression transition unavailable");
    }
    return out;
}
bool VisualActionSourceReady(const ActionCatalogStatus& catalog, const std::string& path) {
    for (const auto& source : catalog.visual_sources)
        if (source.character_config == path)
            return true;
    return catalog.finished; // a completed catalog can report an absent source
}
VisualState ResolveVisualAction(const ActionCatalogStatus& catalog, const VisualActionKey& key,
                                const api::VisualReactionInput* death_names) {
    const VisualState* result = nullptr;
    for (const auto& row : catalog.visual_states)
        if (row.character_config == key.character_config && row.value.flag.available &&
            row.value.flag.value == key.flag) {
            if (result)
                throw std::runtime_error("ambiguous visual behavior identity");
            result = &row;
        }
    if (!result)
        throw std::runtime_error("visual behavior absent from this character's configuration");
    auto value = *result;
    value.recover = key.recover;
    if (key.occurrence != DeathOccurrence::Configured) {
        if (!death_names)
            throw std::runtime_error("death animation sources are not prepared");
        ApplyDeathOccurrence(value, key.occurrence, *death_names);
    }
    const auto availability = QueryVisualAction(value, key.recover);
    if (!availability.available)
        throw std::runtime_error(availability.issues.front());
    return value;
}
}
