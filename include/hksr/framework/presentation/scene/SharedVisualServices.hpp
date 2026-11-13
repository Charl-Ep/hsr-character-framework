#pragma once
#include <cstdint>
#include <map>

namespace hksr::framework {
enum class SharedVisualService { Environment, FrameCapture };

// Engine-thread admission for this Framework's requests. Native lifetime still
// belongs to the request: release a claim only after its actual drain.
class SharedVisualServices {
  public:
    using Claim = std::uint64_t;
    Claim Identify() {
        return ++next_;
    }
    bool Acquire(SharedVisualService service, Claim claim) {
        if (!claim)
            return false;
        const auto found = owners_.find(service);
        if (found != owners_.end())
            return found->second == claim;
        owners_.emplace(service, claim);
        return true;
    }
    void Release(SharedVisualService service, Claim claim) {
        const auto found = owners_.find(service);
        if (found != owners_.end() && found->second == claim)
            owners_.erase(found);
    }

  private:
    Claim next_ = 0;
    std::map<SharedVisualService, Claim> owners_;
};
}
