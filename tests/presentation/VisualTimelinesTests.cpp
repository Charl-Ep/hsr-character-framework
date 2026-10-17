#include <hksr/framework/presentation/VisualTimelines.hpp>
#include <stdexcept>

namespace {
void Check(bool value){if(!value)throw std::runtime_error("visual Timeline test failed");}
}
void TestVisualTimelines() {
    using namespace hksr;
    using namespace hksr::framework;
    int next=0,stops=0;
    std::map<int,api::TimelinePlaybackPhase> phases;
    auto timelines=std::make_shared<VisualTimelines>([&] {
        const int id=++next;api::TimelineRequestServices services;
        services.start=[&,id](const std::string& path,int wrap,double time) {
            Check(path=="scene" && wrap==0 && time==0);
            phases[id]=api::TimelinePlaybackPhase::Playing;
            api::TimelineStartResult result;result.call.returned=true;result.playback=id;result.attributed=true;
            result.references.push_back({reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),id,static_cast<std::uint64_t>(id)});return result;
        };
        services.observe=[&](api::TimelineReference ref){return phases.at(ref.playback);};
        services.stop=[&](api::TimelineReference ref){++stops;phases[ref.playback]=api::TimelinePlaybackPhase::Stopped;return api::CallResult{true,0};};
        return std::make_unique<api::OwnedTimeline>(std::move(services));
    });
    VisualProgramServices services;ConnectVisualTimelines(services,timelines);
    Check(services.timeline({"gift","scene"}).state==VisualStepState::Done && !services.timelines_drained());
    Check(services.timeline({"gift","scene"}).state==VisualStepState::Done);
    Check(!services.timelines_drained() && stops==1); // old receipt retained
    Check(services.timeline({"gift",{},false}).state==VisualStepState::Done && stops==2);
    Check(!services.timelines_drained()); // stopped is not pooled
    phases[1]=api::TimelinePlaybackPhase::Retired;Check(!services.timelines_drained());
    phases[2]=api::TimelinePlaybackPhase::Retired;Check(services.timelines_drained());
    Check(services.timeline({"resolution","scene"}).state==VisualStepState::Done);
    services.cancel_timelines();Check(!services.timelines_drained() && stops==3);
    phases[3]=api::TimelinePlaybackPhase::Retired;Check(services.timelines_drained());
    Check(services.timeline({"missing",{},false}).state==VisualStepState::Failed);
}
