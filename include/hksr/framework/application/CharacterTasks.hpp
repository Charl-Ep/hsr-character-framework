#pragma once
#include <hksr/framework/application/ApplicationService.hpp>

namespace hksr::framework {
enum class CharacterTaskKind { Npc, Configured, Ready, Reaction, Program, Sequence, Idle, Setting };
struct CharacterTaskVariant {
    std::string key, label, detail;
    CharacterCommand command;
    TaskQueryResult query;
};
// Value-only directory shared by UI and HTTP. Backend identities are retained:
// a configured animation candidate is not a complete battle ability.
struct CharacterTaskEntry {
    CharacterTaskKind kind = CharacterTaskKind::Configured;
    ActionDomain domain = ActionDomain::Exploration;
    std::string label, detail;
    TaskQueryResult query;
    std::vector<TaskSourceLocation> sources;
    std::vector<TaskInputDefinition> inputs;
    CharacterCommand command;
    bool recovery = false;
    TaskBindingSchema bindings;
    bool diagnostic = false;
    std::vector<std::string> setting_contexts;
    TaskExecutionMode mode = TaskExecutionMode::Replace;
    TaskTrigger trigger = TaskTrigger::User;
    std::vector<CharacterTaskVariant> variants;
};
inline bool HasTaskVariants(const CharacterTaskEntry& entry) {
    return entry.variants.size() > 1;
}
bool HasTaskInputs(const CharacterTaskEntry&);
bool CanConfigureTask(const CharacterTaskEntry&);
std::vector<CharacterTaskEntry> CharacterTasks(const ApplicationSnapshot&, std::uint64_t character);
}
