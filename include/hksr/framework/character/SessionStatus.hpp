#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hksr::framework {
enum class SessionPhase { NotCreated, PreparingBaseline, Active, Failed, Closing, Closed };
struct CharacterSessionStatus {
    SessionPhase phase=SessionPhase::NotCreated;
    bool baseline_attempted=false,alternate_available=false,closed=false;
    std::uint64_t selected_action=0,submitted_action=0;
    std::uint64_t handoff_request=0;
    std::string dispatch_error;
    std::string waiting;
    std::vector<std::string> errors;
};
}
