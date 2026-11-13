#pragma once
#include <hksr/framework/presentation/scene/VisualInstruction.hpp>
#include <hksr/framework/presentation/scene/VisualStepResult.hpp>
#include <hksr/framework/presentation/scene/SharedVisualServices.hpp>
#include <hksr/presentation/timeline/OwnedTimeline.hpp>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace hksr::framework {
struct VisualProgramServices;
// One authored scene's Timeline receipts, including cancelled playback tails.
// The factory binds the concrete actor through PresentationContext::CreateTimeline.
class VisualTimelines {
  public:
    using Factory = std::function<std::unique_ptr<api::OwnedTimeline>()>;
    using Trace = std::function<void(const std::string&)>;
    using Resource = std::function<std::optional<std::string>(const std::string&)>;
    explicit VisualTimelines(Factory, Trace = {}, std::shared_ptr<SharedVisualServices> shared = {}, Resource = {});
    VisualStepResult Execute(std::uint64_t scope, const VisualTimeline&);
    void Cancel(std::uint64_t scope);
    void Service();
    bool Drained() const;
    bool Drained(std::uint64_t scope) const;
    void Forget(std::uint64_t scope);
    const std::vector<std::string>& Issues() const {
        return issues_;
    }

  private:
    Factory factory_;
    Resource resource_;
    Trace trace_;
    std::map<const api::OwnedTimeline*, std::string> last_trace_;
    void Report(const std::string& key, const api::OwnedTimeline&);
    using Key = std::pair<std::uint64_t, std::string>;
    std::map<Key, std::shared_ptr<api::OwnedTimeline>> current_;
    std::set<Key> suppressed_;
    struct Retired {
        std::uint64_t scope;
        std::shared_ptr<api::OwnedTimeline> request;
    };
    std::vector<Retired> retired_;
    std::shared_ptr<SharedVisualServices> shared_;
    std::map<Key, SharedVisualServices::Claim> waiting_;
    std::map<const api::OwnedTimeline*, SharedVisualServices::Claim> claims_;
    void ReleaseClaim(const api::OwnedTimeline&);
    std::vector<std::string> issues_;
};
// Shares one receipt owner across all three VisualProgram callback boundaries.
void ConnectVisualTimelines(VisualProgramServices&, std::shared_ptr<VisualTimelines>);
}
