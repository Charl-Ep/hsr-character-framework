#pragma once
#include <hksr/animation/OfficialCatalog.hpp>

namespace hksr::framework {
struct ActionRetirement {
    std::string state;
    api::EffectRemovalSource removal;
};
}
