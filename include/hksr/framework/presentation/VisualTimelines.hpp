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
    VisualStepResult Execute(const VisualTimeline&);
    void Cancel();
    void Service();
    bool Drained() const;
    const std::vector<std::string>& Issues() const{return issues_;}
private:
    Factory factory_;
    Trace trace_;
    std::map<const api::OwnedTimeline*,std::string> last_trace_;
    void Report(const std::string& key,const api::OwnedTimeline&);
    std::map<std::string,std::shared_ptr<api::OwnedTimeline>> current_;
    std::vector<std::shared_ptr<api::OwnedTimeline>> retired_;
    std::vector<std::string> issues_;
};
// Shares one receipt owner across all three VisualProgram callback boundaries.
void ConnectVisualTimelines(VisualProgramServices&,std::shared_ptr<VisualTimelines>);
}
