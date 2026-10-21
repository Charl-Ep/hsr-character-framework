#pragma once
#include "ApplicationService.hpp"

namespace hksr::framework {
enum class CharacterTaskKind { Npc,Configured,Ready,Reaction,Program,Sequence,Idle };
// Value-only directory shared by UI and HTTP. Backend identities are retained:
// a configured animation candidate is not a complete battle ability.
struct CharacterTaskEntry {
    CharacterTaskKind kind=CharacterTaskKind::Configured;
    ActionDomain domain=ActionDomain::Exploration;
    std::string label,detail;
    TaskQueryResult query;
    std::vector<TaskSourceLocation> sources;
    std::vector<TaskInputDefinition> inputs;
    CharacterCommand command;
    bool recovery=false;
};
std::vector<CharacterTaskEntry> CharacterTasks(const ApplicationSnapshot&,std::uint64_t character);
}
