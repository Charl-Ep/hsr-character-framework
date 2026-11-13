#pragma once
#include <hksr/animation/ActorBgmSync.hpp>
namespace hksr::framework {
struct VisualBgmSync {
    bool active = true;
    api::BgmSyncConfig config;
};
}
