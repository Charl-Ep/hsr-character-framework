#pragma once
#include "SceneCharacters.hpp"
#include <hksr/world/WorldMap.hpp>
#include <hksr/managed/ManagedRoots.hpp>
#include <array>

namespace hksr::framework {
// Framework persistence policy, not an official persistent-ID API.
// A generation is a retained (world, phase, group-manager, map) tuple. In the
// inspected 4.5.0 ordinary initialization a new group manager is allocated even
// when MapDef is reused. No promise is made for unobserved hot-update paths.
class SceneIdentity {
public:
    explicit SceneIdentity(std::unique_ptr<api::ManagedRoots> roots):roots_(std::move(roots)){}
    std::optional<SceneAddress> Observe(const api::WorldFrame& frame,const api::WorldMapSnapshot& map);
    void Close(); // explicit engine-thread release; destructor calls no game
    bool Closed() const {return closing_ && owned_.empty();}
    const std::string& Error() const {return error_;}
    std::size_t PendingRoots() const {return owned_.size();}
private:
    void ReleaseActive();
    void Drain();
    std::unique_ptr<api::ManagedRoots> roots_;
    std::vector<api::ManagedRootId> owned_;
    std::array<api::ManagedRootId,4> active_{};
    std::optional<SceneAddress> address_;
    std::uint64_t sequence_=0;
    bool closing_=false;
    std::string error_;
};
}
