#pragma once
#include "VisualProgram.hpp"
#include <hksr/presentation/timeline/OwnedTimeline.hpp>

namespace hksr::framework {
// One authored scene's Timeline receipts, including cancelled playback tails.
// The factory binds the concrete actor through PresentationContext::CreateTimeline.
class VisualTimelines {
public:
    using Factory=std::function<std::unique_ptr<api::OwnedTimeline>()>;
    using Trace=std::function<void(const std::string&)>;
    explicit VisualTimelines(Factory,Trace={});
    VisualStepResult Execute(std::uint64_t scope,const VisualTimeline&);
    void Cancel(std::uint64_t scope);
    void Service();
    bool Drained() const;
    bool Drained(std::uint64_t scope) const;
    void Forget(std::uint64_t scope);
    const std::vector<std::string>& Issues() const{return issues_;}
private:
    Factory factory_;
    Trace trace_;
    std::map<const api::OwnedTimeline*,std::string> last_trace_;
    void Report(const std::string& key,const api::OwnedTimeline&);
    using Key=std::pair<std::uint64_t,std::string>;
    std::map<Key,std::shared_ptr<api::OwnedTimeline>> current_;
    struct Retired {std::uint64_t scope;std::shared_ptr<api::OwnedTimeline> request;};
    std::vector<Retired> retired_;
    std::vector<std::string> issues_;
};
// Shares one receipt owner across all three VisualProgram callback boundaries.
void ConnectVisualTimelines(VisualProgramServices&,std::shared_ptr<VisualTimelines>);
}
