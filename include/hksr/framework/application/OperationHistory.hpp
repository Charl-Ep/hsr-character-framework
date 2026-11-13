#pragma once
#include <hksr/framework/application/OperationStatus.hpp>
#include <hksr/framework/character/SceneCharacterState.hpp>
#include <hksr/framework/catalog/CatalogSnapshot.hpp>
#include <deque>
#include <memory>

namespace hksr::framework {
class ControlSubmission;
// Official-thread value projection. Never calls the engine or gates execution.
// Pending operations are never evicted; only completed history is bounded.
class OperationHistory {
  public:
    explicit OperationHistory(std::size_t retained_terminal = 256) : retained_terminal_(retained_terminal) {}
    void Add(OperationStatus operation, std::shared_ptr<const ControlSubmission> control = {});
    void Observe(const std::vector<SceneCharacterStatus>& characters, const CatalogSnapshot* catalog, bool closing);
    const OperationSnapshot& Snapshot() const noexcept {
        return snapshot_;
    }

  private:
    void Trim();
    OperationSnapshot snapshot_;
    std::deque<std::uint64_t> terminal_;
    std::size_t retained_terminal_;
    std::map<std::uint64_t, std::shared_ptr<const ControlSubmission>> controls_;
};
}
