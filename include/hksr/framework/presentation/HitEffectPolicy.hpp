#pragma once
#include <hksr/world/WorldPlacement.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <tuple>

namespace hksr::framework {
enum class HitEffectStacking { HighestPriority, Multiple };
// Receiver-owned modifier snapshot, in source-list order. No borrowed game
// component or effect pointer is carried into the policy.
struct HitEffectOverride {
    HitEffectStacking stacking=HitEffectStacking::HighestPriority;
    std::optional<std::string> path; // null is a removed registration, not an empty resource
    std::string death_path;
    int priority=0,count=1,death_count=0;
    std::uint32_t version=0;
    std::optional<api::Position3> offset;
    bool keep_original=false,replay_on_mutex=false;
    bool operator==(const HitEffectOverride& other) const {
        const auto position=[](const std::optional<api::Position3>& value){return std::make_tuple(bool(value),value?value->x:0,value?value->y:0,value?value->z:0);};
        return std::tie(stacking,path,death_path,priority,count,death_count,version,keep_original,replay_on_mutex)==
            std::tie(other.stacking,other.path,other.death_path,other.priority,other.count,other.death_count,other.version,other.keep_original,other.replay_on_mutex) && position(offset)==position(other.offset);
    }
};
struct HitEffectChoice {
    std::string path;
    std::optional<api::Position3> offset;
    bool replay_on_mutex=false;
};
struct HitEffectPlan {
    std::vector<HitEffectChoice> effects;
    bool has_overrides=false; // enables target-scoped existing-effect updates
};
// Source path has already passed AttackProperty/key/random resolution. Empty
// path and empty hit animation disable this branch before receiver overrides.
HitEffectPlan SelectHitEffects(const std::string& path,const std::string& animation,
    const std::vector<HitEffectOverride>&);

}
