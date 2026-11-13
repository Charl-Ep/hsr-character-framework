#pragma once
#include <hksr/framework/catalog/ActionCatalogTypes.hpp>

namespace hksr::framework {
// Stable selection within a character's resolved official configuration. A
// flag distinguishes rows sharing the same body clip but different expressions.
struct VisualActionKey {
    std::string character_config;
    std::int32_t flag = 0;
    bool recover = false;
    DeathOccurrence occurrence = DeathOccurrence::Configured;
};
struct VisualActionAvailability {
    bool available = true;
    std::vector<std::string> issues;
};
VisualActionAvailability QueryVisualAction(const VisualState&, bool recover);
VisualState ResolveVisualAction(const ActionCatalogStatus&, const VisualActionKey&,
                                const api::VisualReactionInput* death_names = nullptr);
void ApplyDeathOccurrence(VisualState&, DeathOccurrence, const api::VisualReactionInput& defaults,
                          const api::VisualReactionInput* phase = nullptr);
// Only this source is a prerequisite; unrelated skill discovery is not.
bool VisualActionSourceReady(const ActionCatalogStatus&, const std::string& character_config);
}
