#pragma once
#include <stdexcept>
#include <string>

namespace hksr::framework {
enum class DeathOccurrence { Configured, Die, Hit, Fly, Limbo };
inline const char* DeathOccurrenceName(DeathOccurrence value) {
    switch (value) {
    case DeathOccurrence::Configured:
        return "default";
    case DeathOccurrence::Die:
        return "die";
    case DeathOccurrence::Hit:
        return "die_hit";
    case DeathOccurrence::Fly:
        return "die_fly";
    case DeathOccurrence::Limbo:
        return "die_limbo";
    }
    throw std::invalid_argument("invalid death occurrence");
}
inline DeathOccurrence ParseDeathOccurrence(const std::string& value) {
    for (auto slot : {DeathOccurrence::Configured, DeathOccurrence::Die, DeathOccurrence::Hit, DeathOccurrence::Fly,
                      DeathOccurrence::Limbo})
        if (value == DeathOccurrenceName(slot))
            return slot;
    throw std::invalid_argument("invalid death occurrence");
}
}
