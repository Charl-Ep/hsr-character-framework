#pragma once
#include "OperationStatus.hpp"
#include "../character/SceneCharacters.hpp"
#include "../catalog/CatalogBrowser.hpp"
#include <deque>

namespace hksr::framework {
// Official-thread value projection. Never calls the engine or gates execution.
// Pending operations are never evicted; only completed history is bounded.
class OperationHistory {
public:
    explicit OperationHistory(std::size_t retained_terminal=256):retained_terminal_(retained_terminal){}
    void Add(OperationStatus operation,std::shared_ptr<const ControlSubmission> control={});
    void Observe(const std::vector<SceneCharacterStatus>& characters,const CatalogSnapshot* catalog,bool closing);
    const OperationSnapshot& Snapshot() const noexcept {return snapshot_;}
private:
    void Trim();
    OperationSnapshot snapshot_;
    std::deque<std::uint64_t> terminal_;
    std::size_t retained_terminal_;
    std::map<std::uint64_t,std::shared_ptr<const ControlSubmission>> controls_;
};
}
