#include <hksr/framework/tasks/TaskPresentation.hpp>
#include <hksr/framework/presentation/VisualTimelines.hpp>
#include <stdexcept>

namespace {
using namespace hksr;
using namespace hksr::framework;
void CheckAt(bool ok,int line){if(!ok)throw std::runtime_error("task presentation line "+std::to_string(line));}
#define Check(...) CheckAt((__VA_ARGS__),__LINE__)
TaskNode Visual(const char* name,VisualOperation operation) {
    return {name,TaskSubmit{name,VisualInstruction{std::move(operation),name}}};
}
std::shared_ptr<TaskSequence> Plan(std::initializer_list<TaskNode> nodes,TaskNodeIndex entry=0) {
    auto plan=std::make_shared<TaskSequence>();plan->nodes=nodes;plan->entry=entry;return plan;
}
struct Fixture {
    unsigned starts=0,stops=0,attached=0,detached=0;
    std::map<int,api::TimelinePlaybackPhase> timelines;
    std::shared_ptr<VisualProgram> program=std::make_shared<VisualProgram>();
    std::shared_ptr<VisualProgramPlayer> player;
    std::function<void()> on_start,on_attach;
    std::map<std::string,std::shared_ptr<AnimationRequestStatus>> animations;
    std::map<std::string,std::uint64_t> generations{{"body",10},{"partner",20}};
    std::vector<std::pair<std::string,std::uint64_t>> released;
    bool partner_ready=false;
    Fixture() {
        VisualProgramServices services;
        services.effects=std::make_shared<api::PresentationEffects>(api::EffectRequestServices{{},api::EffectAccess({}),{},{},{}});
        services.create=[](auto&,const auto&,const auto&)->VisualEffectId{throw std::runtime_error("unexpected effect create");};
        services.attach=[&](auto){++attached;if(on_attach)on_attach();};
        services.detach=[&]{++detached;};
        ConnectVisualTimelines(services,std::make_shared<VisualTimelines>([&] {
            const int id=static_cast<int>(++starts);api::TimelineRequestServices native;
            native.start=[&,id](const auto&,int,double){
                timelines[id]=api::TimelinePlaybackPhase::Playing;
                api::TimelineStartResult result;result.call.returned=true;result.playback=id;result.attributed=true;
                result.references.push_back({reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),id,static_cast<std::uint64_t>(id)});
                if(on_start)on_start();return result;
            };
            native.observe=[&](auto ref){return timelines.at(ref.playback);};
            native.stop=[&](auto ref){++stops;timelines[ref.playback]=api::TimelinePlaybackPhase::Stopped;return api::CallResult{true,0};};
            return std::make_unique<api::OwnedTimeline>(std::move(native));
        }));
        player=std::make_shared<VisualProgramPlayer>(program,std::move(services));
    }
    TaskPresentationServices Services() {
        TaskPresentationServices result;
        result.animation=[&](const auto& participant)->std::optional<TaskAnimationEndpoint>{
            const auto name=participant.name;
            const auto found=generations.find(name);
            if(found==generations.end())throw std::runtime_error("unknown participant: "+name);
            if(name=="partner" && !partner_ready)return {};
            const auto generation=found->second;
            return TaskAnimationEndpoint{generation,[&,name,generation](const auto&){
                auto value=std::make_shared<AnimationRequestStatus>();value->serial=animations.size()+1;
                value->entity_generation=generation;animations[name]=value;return value;
            },[&,name](auto serial){released.emplace_back(name,serial);animations.at(name)->released=true;}};
        };return result;
    }
    void Retire(){for(auto& [id,phase]:timelines)phase=api::TimelinePlaybackPhase::Retired;player->Service();}
    ~Fixture(){player->End();player->Service();Retire();}
};
void Scopes() {
    Fixture f;
    auto plan=Plan({Visual("scene",VisualTimeline{"scene","scene.playable"})});
    TaskPresentation a({1},10,plan,f.player,{},f.Services());a.Service(0,1);a.Service(0,2);
    Check(f.starts==1 && f.stops==0 && !a.Drained()); // bare submission still executes; natural tail remains
    TaskPresentation b({2},10,plan,f.player,{},f.Services());b.Service(0,3);b.Service(0,4);
    Check(f.starts==2 && f.timelines.at(2)==api::TimelinePlaybackPhase::Playing);
    a.Stop(TaskExitReason::Deleted);a.Service(0,5);
    Check(f.timelines.at(1)==api::TimelinePlaybackPhase::Stopped && f.timelines.at(2)==api::TimelinePlaybackPhase::Playing);
    f.timelines[1]=api::TimelinePlaybackPhase::Retired;a.Service(0,6);
    Check(a.Drained() && !b.Drained()); // another scope cannot delay this drain
    b.Stop(TaskExitReason::Deleted);b.Service(0,7);f.Retire();b.Service(0,8);Check(b.Drained());
    // A late close of the old scope must not detach the new event producer.
    const auto old=f.player->BeginSequence({});f.player->Service();
    const auto current=f.player->BeginSequence({});f.player->Service();
    const auto detach=f.detached;f.player->EndSequence(old,false);f.player->Service();
    Check(f.detached==detach && f.player->SequenceDrained(old));f.player->ForgetSequence(old);
    f.player->EndSequence(current,false);f.player->Service();Check(f.detached==detach+1);
    f.player->ForgetSequence(current);
}
void IndependentParticipants() {
    Fixture f;
    auto plan=Plan({
        {"body",TaskSubmit{"body",TaskAnimationCommand{{"body"},"Body"}}},
        {"partner",TaskSubmit{"partner",TaskAnimationCommand{{"partner"},"Partner"}}},
        {"wait-body",TaskWaitRequest{"body"}},{"wait-partner",TaskWaitRequest{"partner"}},
        {"wait-both",TaskParallel{{2,3}}},{"entry",TaskSerial{{0,1,4}}}
    },5);
    TaskPresentation run({3},10,plan,f.player,{},f.Services());run.Service(0,1);
    Check(f.animations.size()==1 && !f.animations.at("body")->accepted && run.Status().issues.empty());
    auto body=f.animations.at("body");body->accepted=true;body->model_generation=101;body->progress=1;body->complete=true;
    run.Service(0.1,2);Check(!run.Drained());
    f.partner_ready=true;run.Service(0.1,3);Check(f.animations.size()==2 && !run.Drained());
    auto partner=f.animations.at("partner");partner->accepted=true;partner->model_generation=202;partner->progress=0.2;
    run.Service(0.1,4);Check(!run.Drained());
    partner->complete=true;partner->progress=1;run.Service(0.1,5);run.Service(0,6);
    Check(run.Drained() && run.Status().issues.empty() && f.released.size()==2);
    // A cancelled unresolved participant is never resolved/submitted later.
    Fixture waiting;TaskPresentation cancelled({4},10,plan,waiting.player,{},waiting.Services());
    cancelled.Service(0,1);cancelled.Stop(TaskExitReason::Deleted);waiting.partner_ready=true;cancelled.Service(0,2);
    Check(cancelled.Drained() && waiting.animations.size()==1);
}
void ErrorsAndReentry() {
    Fixture f;
    auto plan=Plan({Visual("scene",VisualTimeline{"scene","scene.playable"}),{"keep",TaskWaitSeconds{10}},
        {"entry",TaskSerial{{0,1}}}},2);
    TaskPresentation run({5},10,plan,f.player,{},f.Services());
    f.on_start=[&]{run.Stop(TaskExitReason::Deleted);run.Service(0,1);};
    run.Service(0,1);f.on_start={};run.Service(0,2);
    Check(f.starts==1 && f.stops==1 && !run.Drained());
    f.Retire();run.Service(0,3);Check(run.Drained());
    Fixture bad;
    auto bad_plan=Plan({Visual("missing-variable",VisualProgress{"missing",std::string("undefined"),0}),
        {"wait",TaskWaitRequest{"missing-variable"}},{"entry",TaskSerial{{0,1}}}},2);
    TaskPresentation failure({6},10,bad_plan,bad.player,{},bad.Services());failure.Service(0,1);failure.Service(0,2);
    Check(failure.Status().reason==TaskExitReason::Failed && !failure.Status().issues.empty() && failure.Drained());
    // Setup reentry cancels queued instructions before they reach native calls.
    Fixture setup;TaskPresentation removed({7},10,plan,setup.player,{},setup.Services());
    setup.on_attach=[&]{removed.Stop(TaskExitReason::Deleted);};removed.Service(0,1);setup.on_attach={};removed.Service(0,2);
    Check(setup.starts==0 && removed.Drained());
    Fixture wrong;
    auto unknown=Plan({{"unknown",TaskSubmit{"unknown",TaskAnimationCommand{{"official-player"},"Attack"}}}});
    TaskPresentation missing({8},10,unknown,wrong.player,{},wrong.Services());missing.Service(0,1);missing.Service(0,2);
    Check(missing.Status().reason==TaskExitReason::Failed && wrong.animations.empty() && missing.Drained());
}
void NamedContexts() {
    Fixture f;
    f.program->contexts["phase"].enter={{VisualTimeline{"phase-scene","scene.playable"},"phase.enter"}};
    auto enter=Plan({Visual("phase",VisualContextOperation{"phase",true})});
    TaskPresentation first({9},10,enter,f.player,{},f.Services());first.Service(0,1);first.Service(0,2);
    Check(first.Drained() && f.starts==1 && f.player->ContextToken("phase"));
    auto exit=Plan({Visual("phase-exit",VisualContextOperation{"phase",false})});
    TaskPresentation second({10},10,exit,f.player,{},f.Services());second.Service(0,3);second.Service(0,4);
    Check(second.Drained() && !f.player->ContextToken("phase") && f.stops==1);
    f.Retire();f.player->End();f.player->Service();Check(f.player->Drained());
}
void IndependentTail() {
    Fixture f;
    f.program->contexts["phase"].enter={{VisualTimeline{"phase-scene","scene.playable"},"phase.enter"}};
    auto enter=Plan({Visual("phase",VisualContextOperation{"phase",true})});
    TaskPresentation start({11},10,enter,f.player,{},f.Services());start.Service(0,1);start.Service(0,2);
    auto end=Plan({Visual("tail",VisualTimeline{"tail","tail.playable"}),
        {"tail-accepted",TaskWaitRequest{"tail",TaskWaitSignal::RequestComplete}},
        Visual("exit",VisualContextOperation{"phase",false}),
        {"exit-accepted",TaskWaitRequest{"exit",TaskWaitSignal::RequestComplete}},
        {"entry",TaskSerial{{0,1,2,3}}}},4);
    TaskPresentation finish({12},10,end,f.player,{},f.Services());
    for(unsigned frame=3;frame<8;++frame)finish.Service(0,frame);
    Check(f.stops==1 && f.timelines.at(1)==api::TimelinePlaybackPhase::Stopped &&
        f.timelines.at(2)==api::TimelinePlaybackPhase::Playing && !finish.Drained());
    f.timelines[1]=api::TimelinePlaybackPhase::Retired;f.timelines[2]=api::TimelinePlaybackPhase::Retired;
    finish.Service(0,8);Check(finish.Drained() && finish.Status().issues.empty());
}
}
void TestTaskPresentation(){Scopes();IndependentParticipants();ErrorsAndReentry();NamedContexts();IndependentTail();}
