#include <hksr/framework/presentation/VisualTimelines.hpp>
#include <stdexcept>

namespace {
void Require(bool value,const char* expression,int line){if(!value)throw std::runtime_error(
    "visual Timeline line "+std::to_string(line)+": "+expression);}
#define Check(...) Require((__VA_ARGS__),#__VA_ARGS__,__LINE__)
}
void TestVisualTimelines() {
    using namespace hksr;
    using namespace hksr::framework;
    int next=0,stops=0;
    std::vector<std::string> trace;
    std::function<void()> on_start;
    std::map<int,api::TimelinePlaybackPhase> phases;
    auto timelines=std::make_shared<VisualTimelines>([&] {
        const int id=++next;api::TimelineRequestServices services;
        services.start=[&,id](const std::string& path,int wrap,double time) {
            Check(path=="scene" && wrap==0 && time==0);
            phases[id]=api::TimelinePlaybackPhase::Playing;
            api::TimelineStartResult result;result.call.returned=true;result.playback=id;result.attributed=true;
            result.references.push_back({reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),id,static_cast<std::uint64_t>(id)});
            if(on_start)on_start();return result;
        };
        services.observe=[&](api::TimelineReference ref){return phases.at(ref.playback);};
        services.stop=[&](api::TimelineReference ref){++stops;phases[ref.playback]=api::TimelinePlaybackPhase::Stopped;return api::CallResult{true,0};};
        return std::make_unique<api::OwnedTimeline>(std::move(services));
    },[&](const auto& text){trace.push_back(text);});
    VisualProgramServices services;ConnectVisualTimelines(services,timelines);
    Check(services.timeline(0,{"gift","scene"}).state==VisualStepState::Done && !services.timelines_drained());
    Check(!trace.empty());const auto trace_once=trace.size();
    services.timelines_drained();Check(trace.size()==trace_once);
    Check(services.timeline(0,{"gift","scene"}).state==VisualStepState::Done);
    Check(!services.timelines_drained() && stops==1); // old receipt retained
    Check(services.timeline(0,{"gift",{},false}).state==VisualStepState::Done && stops==2);
    Check(!services.timelines_drained()); // stopped is not pooled
    phases[1]=api::TimelinePlaybackPhase::Retired;Check(!services.timelines_drained());
    phases[2]=api::TimelinePlaybackPhase::Retired;Check(services.timelines_drained());
    Check(trace.back().find("drained=1")!=std::string::npos);
    Check(services.timeline(0,{"resolution","scene"}).state==VisualStepState::Done);
    services.cancel_timelines(0);Check(!services.timelines_drained() && stops==3);
    phases[3]=api::TimelinePlaybackPhase::Retired;Check(services.timelines_drained());
    Check(services.timeline(0,{"missing",{},false}).state==VisualStepState::Failed);
    // Identical local keys in separate lifetime tokens are independent. A
    // retired token cannot stop a re-entered context with the same display name.
    Check(services.timeline(11,{"same","scene"}).state==VisualStepState::Done);
    Check(services.timeline(12,{"same","scene"}).state==VisualStepState::Done);
    services.cancel_timelines(0);services.timelines_drained();Check(stops==3);
    services.cancel_timelines(11);services.timelines_drained();Check(stops==4);
    Check(phases[4]==api::TimelinePlaybackPhase::Stopped && phases[5]==api::TimelinePlaybackPhase::Playing);
    phases[4]=api::TimelinePlaybackPhase::Retired;
    services.timelines_drained();
    Check(services.timeline_scope_drained(11) && !services.timeline_scope_drained(12));
    Check(services.timeline(13,{"same","scene"}).state==VisualStepState::Done);
    services.cancel_timelines(11);services.timelines_drained();Check(stops==4 && phases[6]==api::TimelinePlaybackPhase::Playing);
    services.cancel_timelines(12);services.cancel_timelines(13);services.timelines_drained();Check(stops==6);
    phases[4]=phases[5]=phases[6]=api::TimelinePlaybackPhase::Retired;Check(services.timelines_drained());

    auto program=std::make_shared<VisualProgram>(ParseVisualProgram(R"({
      "format":"hksr.visual-program","id":"timeline-lifetimes","source":"config","scope":"action","effects":{},
      "contexts":{
        "persistent":{"enter":[{"op":"timeline_play","key":"same","path":"scene","source":"persistent.enter"}]},
        "other":{"enter":[{"op":"timeline_play","key":"same","path":"scene","source":"other.enter"}]}
      },
      "actions":{
        "start":{"state":"Start","restart":true,"enter":[
          {"op":"context_enter","name":"persistent","source":"start.persistent"},
          {"op":"context_enter","name":"other","source":"start.other"},
          {"op":"timeline_play","key":"same","path":"scene","source":"start.action"}]},
        "next":{"state":"Next","restart":true,"completion_state":"Idle"},
        "stop":{"state":"Stop","restart":true,"enter":[
          {"op":"timeline_stop","context":"persistent","key":"same","source":"stop.timeline"},
          {"op":"context_exit","name":"persistent","source":"stop.context"}]},
        "enter":{"state":"Enter","restart":true,"enter":[
          {"op":"context_enter","name":"persistent","source":"enter.context"}]}
      },
      "signals":{"stop_other":{"context":"other","instructions":[
        {"op":"timeline_stop","key":"same","source":"signal.stop"}]}}
    })"));
    services.effects=std::make_shared<api::PresentationEffects>(api::EffectRequestServices{{},api::EffectAccess({}),{},{},{}});
    services.create=[](auto&,const auto&,const auto&)->VisualEffectId{throw std::logic_error("unexpected effect");};
    VisualProgramPlayer player(program,services);
    player.Select("start");player.Service();Check(player.Status().prepared && next==9);
    const auto persistent=player.ContextToken("persistent"),other=player.ContextToken("other");
    const auto intent=player.Select("next");player.Service();player.Service(); // service the queued cancellation
    Check(player.Status().prepared && player.ContextToken("persistent")==persistent);
    Check(phases[7]==api::TimelinePlaybackPhase::Playing && phases[8]==api::TimelinePlaybackPhase::Playing);
    Check(phases[9]==api::TimelinePlaybackPhase::Stopped);
    Check(player.Played(intent,1,1));player.Observe(1,1,"Next",1);player.Service();
    Check(player.SuccessorPlayed(intent,2,1));player.Observe(2,1,"Idle",0);player.Service();
    Check(player.Status().completed && phases[7]==api::TimelinePlaybackPhase::Playing);
    player.InterruptAction();player.Service();Check(phases[8]==api::TimelinePlaybackPhase::Playing);
    player.Select("stop");player.Service();Check(player.Status().prepared && !player.ContextToken("persistent"));
    Check(phases[7]==api::TimelinePlaybackPhase::Stopped && phases[8]==api::TimelinePlaybackPhase::Playing);
    player.Select("enter");player.Service();Check(player.ContextToken("persistent")!=persistent && next==10);
    Check(player.Signal(other,"stop_other"));player.Service();Check(phases[8]==api::TimelinePlaybackPhase::Stopped);
    player.End();player.Service();Check(phases[10]==api::TimelinePlaybackPhase::Stopped && !player.Drained());
    for(int id=7;id<=10;++id)phases[id]=api::TimelinePlaybackPhase::Retired;
    player.Service();Check(player.Drained());
    VisualProgramPlayer reentrant(program,services);
    on_start=[&]{reentrant.End();reentrant.Service();};
    reentrant.Select("enter");reentrant.Service();on_start={};
    Check(next==11);reentrant.Service();Check(phases[11]==api::TimelinePlaybackPhase::Stopped && !reentrant.Drained());
    phases[11]=api::TimelinePlaybackPhase::Retired;reentrant.Service();Check(reentrant.Drained());
}
