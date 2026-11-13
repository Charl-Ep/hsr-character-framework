#pragma once
#include <cstdint>
#include <string>

namespace hksr::framework {
// Host-supplied persistence key and observed incarnation, not a native pointer.
struct SceneAddress {
    std::string key;
    std::uint64_t incarnation = 0;
};
}
