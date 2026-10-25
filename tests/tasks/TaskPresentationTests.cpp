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
    std::function<std::optional<MovementRequestEndpoint>(VisualEffectContext&,const VisualMovement&)> movement;
    std::function<std::optional<HitDelivery>(VisualEffectContext&,const std::string&,const HitInput&)> hit;
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
        services.movement=[&](auto& context,const auto& command)->std::optional<MovementRequestEndpoint> {
            if(!movement)throw std::runtime_error("test movement unavailable");return movement(context,command);
        };
        services.hit=[&](auto& context,const auto& target,const auto& command)->std::optional<HitDelivery> {
            if(!hit)throw std::runtime_error("test receiver unavailable");return hit(context,target,command);
        };
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
void ConfiguredPoses() {
    Fixture f;auto ports=f.Services();bool prepared=false;
    std::shared_ptr<AnimationRequestStatus> pose;unsigned submitted=0;bool cancelled=false;
    ports.pose=PoseRequestEndpoint{10,[&](const auto& key)->std::shared_ptr<const AnimationRequestStatus> {
        Check(std::get<ReadyPoseSelection>(key).skill==8);
        if(!prepared)return {};
        ++submitted;pose=std::make_shared<AnimationRequestStatus>();pose->serial=41;pose->entity_generation=10;return pose;
    },[&](auto serial,bool cancel){Check(serial==41);cancelled=cancel;pose->released=true;}};
    auto plan=Plan({{"pose",TaskSubmit{"pose",TaskPoseCommand{ReadyPoseSelection{"source",8,-1}}}},
        {"wait",TaskWaitRequest{"pose"}},{"entry",TaskSerial{{0,1}}}},2);
    TaskPresentation run({91},10,plan,f.player,{},ports);run.Service(0,1);
    Check(!submitted && !run.Drained());prepared=true;run.Service(0,2);
    pose->accepted=true;run.Service(0,3);Check(!run.Drained());
    pose->entered=true;run.Service(0,4);Check(!run.Drained());
    pose->complete=true;run.Service(0,5);run.Service(0,6);
    Check(run.Drained() && run.Status().issues.empty() && submitted==1 && !cancelled);
    TaskPresentation next({92},10,plan,f.player,{},ports);next.Service(0,7);pose->accepted=true;
    next.Stop(TaskExitReason::Deleted);next.Service(0,8);Check(next.Drained() && cancelled);
    auto bad=ports;bad.pose->entity_generation=999;
    TaskPresentation foreign({93},10,plan,f.player,{},bad);foreign.Service(0,9);foreign.Service(0,10);
    Check(foreign.Drained() && foreign.Status().reason==TaskExitReason::Failed && submitted==2);
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
void Sounds() {
    Fixture f;auto ports=f.Services();unsigned stops=0;
    auto status=std::make_shared<api::SoundRequestStatus>();
    ports.sound=[&](const TaskSoundCommand& command)->std::optional<TaskSoundEndpoint> {
        Check(command.participant.name=="body" && command.event=="event");status->accepted=true;
        return TaskSoundEndpoint{10,[]{},[&]{++stops;status->cancelled=true;},[status]{return *status;}};
    };
    const auto sound=TaskNode{"sound",TaskSubmit{"sound",TaskSoundCommand{{"body"},"event"}}};
    auto plan=Plan({sound,{"wait-start",TaskWaitRequest{"sound",TaskWaitSignal::Started}},
        {"body",TaskSubmit{"body",TaskAnimationCommand{{"body"},"Body"}}},
        {"wait-end",TaskWaitRequest{"sound"}},{"entry",TaskSerial{{0,1,2,3}}}},4);
    TaskPresentation run({20},10,plan,f.player,{},ports);run.Service(0,1);
    Check(f.animations.empty() && !run.Drained()); // queued isn't started
    status->started=true;run.Service(0,2);Check(f.animations.size()==1 && !run.Drained());
    auto body=f.animations.at("body");body->accepted=true;body->complete=true;
    status->ended=true;status->closed=true;run.Service(0,3);run.Service(0,4);
    Check(run.Drained() && stops==0 && run.Status().issues.empty());

    // Natural finish retains sound tails; a new execution cannot own/stop them.
    auto tail_status=std::make_shared<api::SoundRequestStatus>();tail_status->accepted=true;
    ports.sound=[tail_status,&stops](const auto&)->std::optional<TaskSoundEndpoint> {
        return TaskSoundEndpoint{10,[]{},[tail_status,&stops]{++stops;tail_status->cancelled=true;},[tail_status]{return *tail_status;}};
    };
    TaskPresentation tail({21},10,Plan({sound}),f.player,{},ports);tail.Service(0,5);tail.Service(0,6);
    Check(!tail.Drained() && stops==0);
    tail.Stop(TaskExitReason::Replaced);tail.Service(0,7);
    Check(stops==1 && !tail.Drained()); // stop request is not backend drain
    tail_status->closed=true;tail.Service(0,8);Check(tail.Drained());

    // Cancellation inside the submitting factory retains the returned request.
    auto pending=std::make_shared<api::SoundRequestStatus>();pending->accepted=true;
    TaskPresentation* active=nullptr;
    ports.sound=[&](const auto&)->std::optional<TaskSoundEndpoint> {
        active->Stop(TaskExitReason::Deleted);
        return TaskSoundEndpoint{10,[]{},[&]{++stops;pending->cancelled=true;},[pending]{return *pending;}};
    };
    TaskPresentation reentrant({22},10,Plan({sound}),f.player,{},ports);active=&reentrant;reentrant.Service(0,9);
    Check(pending->cancelled && !reentrant.Drained());pending->closed=true;reentrant.Service(0,10);
    Check(reentrant.Drained());

    // Missing backend and playback failure remain errors, not completed sound.
    TaskPresentation unavailable({23},10,Plan({sound}),f.player,{},f.Services());unavailable.Service(0,11);unavailable.Service(0,12);
    Check(unavailable.Drained() && unavailable.Status().reason==TaskExitReason::Failed);
    auto failed=std::make_shared<api::SoundRequestStatus>();failed->error="bank failed";failed->closed=true;
    ports.sound=[failed](const auto&)->std::optional<TaskSoundEndpoint> {
        return TaskSoundEndpoint{10,[]{},[]{},[failed]{return *failed;}};
    };
    TaskPresentation failure({24},10,Plan({sound}),f.player,{},ports);failure.Service(0,13);failure.Service(0,14);
    Check(failure.Drained() && failure.Status().reason==TaskExitReason::Failed);
}
void Movements() {
    VisualMovement movement;movement.segments.push_back({});
    const TaskNode body{"body",TaskSubmit{"body",TaskAnimationCommand{{"body"},"Attack"}}};
    const TaskNode moving{"movement",TaskSubmit{"move",TaskMovementCommand{"body",movement}}};
    auto plan=Plan({body,moving,{"wait",TaskWaitRequest{"move"}},{"entry",TaskSerial{{0,1,2}}}},3);
    Fixture f;unsigned starts=0,stops=0;auto status=std::make_shared<api::MovementRequestStatus>();status->accepted=true;
    f.movement=[&](auto&,const auto& command)->std::optional<MovementRequestEndpoint> {
        Check(command.state=="Attack");++starts;
        return MovementRequestEndpoint{10,[]{},[&]{++stops;status->cancelled=true;},[status]{return *status;}};
    };
    TaskPresentation run({30},10,plan,f.player,{},f.Services());run.Service(0,1);
    Check(starts==0&&f.animations.size()==1);
    auto animation=f.animations.at("body");animation->accepted=true;run.Service(0,2);Check(starts==0); // not the old same-name state
    animation->entered=true;animation->model_generation=201;run.Service(0,3);Check(starts==1&&!run.Drained());
    status->started=true;status->movement_time=0.5f;run.Service(0,4);
    bool progress=false;for(const auto& request:run.Requests())if(request.receipt.participant.name=="body"&&request.state.progress==0.5)progress=true;
    Check(progress);
    status->completed=true;status->closed=true;run.Service(0,5);run.Service(0,6);
    Check(run.Drained()&&stops==0&&run.Status().issues.empty());

    // Bare leaves must still be delivered; finishing the sequence cannot release
    // the animation observation before the current playback actually enters.
    Fixture tail;auto tail_status=std::make_shared<api::MovementRequestStatus>();tail_status->accepted=true;
    tail.movement=[&](auto&,const auto&)->std::optional<MovementRequestEndpoint> {
        ++starts;return MovementRequestEndpoint{10,[]{},[&]{++stops;tail_status->cancelled=true;},[tail_status]{return *tail_status;}};
    };
    TaskPresentation finishing({31},10,Plan({body,moving,{"entry",TaskSerial{{0,1}}}},2),tail.player,{},tail.Services());
    finishing.Service(0,1);tail.animations.at("body")->accepted=true;finishing.Service(0,2);
    Check(starts==1&&!tail.animations.at("body")->released);
    tail.animations.at("body")->entered=true;finishing.Service(0,3);Check(starts==2&&!finishing.Drained());
    finishing.Stop(TaskExitReason::WorldLeft);finishing.Service(0,4);Check(stops==1&&!finishing.Drained());
    tail_status->closed=true;finishing.Service(0,5);Check(finishing.Drained());

    // A same-execution animation change first detaches its old motion request.
    Fixture change;auto old=std::make_shared<api::MovementRequestStatus>();old->accepted=true;
    change.movement=[old](auto&,const auto&)->std::optional<MovementRequestEndpoint> {
        return MovementRequestEndpoint{10,[]{},[old]{old->cancelled=true;},[old]{return *old;}};
    };
    auto switching=Plan({body,moving,{"accept",TaskWaitRequest{"move",TaskWaitSignal::RequestComplete}},
        {"return",TaskSubmit{"return",TaskAnimationCommand{{"body"},"StandBy"}}},
        {"entry",TaskSerial{{0,1,2,3}}}},4);
    TaskPresentation switcher({32},10,switching,change.player,{},change.Services());switcher.Service(0,1);
    const auto initial=change.animations.at("body");initial->accepted=true;initial->entered=true;switcher.Service(0,2);switcher.Service(0,3);
    Check(old->cancelled && change.animations.at("body")==initial && !initial->released);
    old->closed=true;switcher.Service(0,4);Check(change.animations.at("body")!=initial&&initial->released);
    change.animations.at("body")->accepted=true;switcher.Service(0,5);switcher.Service(0,6);Check(switcher.Drained());

    Fixture nested;TaskPresentation* active=nullptr;auto pending=std::make_shared<api::MovementRequestStatus>();pending->accepted=true;
    nested.movement=[&](auto&,const auto&)->std::optional<MovementRequestEndpoint> {
        active->Stop(TaskExitReason::Deleted);
        return MovementRequestEndpoint{10,[]{},[pending]{pending->cancelled=true;},[pending]{return *pending;}};
    };
    TaskPresentation reentrant({33},10,plan,nested.player,{},nested.Services());active=&reentrant;reentrant.Service(0,1);
    nested.animations.at("body")->accepted=true;nested.animations.at("body")->entered=true;reentrant.Service(0,2);
    if(!pending->cancelled || reentrant.Drained()) {
        std::string detail="movement reentry: cancelled="+std::to_string(pending->cancelled)+" drained="+std::to_string(reentrant.Drained());
        for(const auto& issue:reentrant.Status().issues)detail+="; "+issue;
        throw std::runtime_error(detail);
    }
    pending->closed=true;reentrant.Service(0,3);
    if(!reentrant.Drained()) {
        std::string detail="movement reentry finish phase="+std::to_string(static_cast<int>(reentrant.Status().phase));
        for(const auto& request:reentrant.Requests())detail+="; "+request.name+" drained="+std::to_string(request.state.drained)+" gen="+std::to_string(request.receipt.entity_generation)+" model="+std::to_string(request.receipt.model);
        for(const auto& issue:reentrant.Status().issues)detail+="; "+issue;
        throw std::runtime_error(detail);
    }

    // Named request identity is required, not a matching state name elsewhere.
    Fixture order;TaskPresentation missing({34},10,Plan({moving,body,{"entry",TaskSerial{{0,1}}}},2),order.player,{},order.Services());
    missing.Service(0,1);missing.Service(0,2);Check(missing.Drained()&&missing.Status().reason==TaskExitReason::Failed);
}
void MovementInputs() {
    VisualMovement command;command.state="Attack";
    VisualMovementSegment attack;attack.destination=MovementDestination::Target;attack.name="target";attack.stop_distance=2;attack.end=0.4f;
    VisualMovementSegment back;back.start=0.8f;back.end=1.2f;back.preserve_height=false;
    command.segments={attack,back};int reads=0;
    const auto resolve=[&](auto kind,const std::string& name){Check(kind==MovementDestination::Target&&name=="target");++reads;return api::Position3{10,99,0};};
    const auto plan=ResolveVisualMovement(command,{0,2,0},{1,3,1},resolve);
    Check(reads==1&&plan.segments[0].target.x==8&&plan.segments[0].target.y==2&&plan.segments[1].target.y==3);
    Check(plan.segments[1].target.x==1&&plan.segments[1].target.z==1&&plan.segments[1].end>1);
    command.segments[0].offset={0,1,0};command.segments[0].stop_distance=0;
    const auto offset=ResolveVisualMovement(command,{0,2,0},{0,2,0},resolve);Check(offset.segments[0].target.y==3);
    command.clock=api::MovementClock::DistanceSpeed;command.segments[0].speed=4;command.segments[1].speed=2;
    Check(api::CompileAnimationMove(ResolveVisualMovement(command,{0,2,0},{0,2,0},resolve)).duration>0);
    command.segments[0].stop_distance=100;
    bool invalid=false;try{ResolveVisualMovement(command,{0,2,0},{0,2,0},resolve);}catch(const std::invalid_argument&){invalid=true;}Check(invalid);
}
void AbilityTailScope() {
    Fixture f;
    auto plan=Plan({{"delay",TaskWaitSeconds{0.5}},Visual("scene",VisualTimeline{"scene","scene.playable"}),
        {"child",TaskSerial{{0,1}}},{"call",TaskAbilityCall{"visual-tail",2,false}},
        {"join",TaskWaitAbilities{}},{"entry",TaskSerial{{3,4}}}},5);
    TaskPresentation run({90},10,plan,f.player,{},f.Services());run.Service(0.1,1);
    Check(run.YieldedControl() && !run.Drained() && f.starts==0);
    run.Service(0.4,2);run.Service(0,3);
    Check(f.starts==1 && f.stops==0 && !run.Drained());
    f.Retire();run.Service(0,4);Check(run.Drained());
}
void HitDispatch() {
    Fixture f;auto inbox=std::make_shared<HitInbox>(20);
    unsigned sends=0,plays=0;bool ready=false;
    std::shared_ptr<HitRequestStatus> playback;
    f.hit=[&](auto&,const std::string& target,const HitInput& input)->std::optional<HitDelivery> {
        Check(target=="Victim" && input.animation=="DamageHit");++sends;return inbox->Post(input);
    };
    auto resolve=[&](const HitInput& input){Check(input.animation=="DamageHit");return HitResolution{ready,HitCommand{{{"Add","Hit",.5f}}}};};
    auto submit=[&](HitCommand command)->std::shared_ptr<const HitRequestStatus> {
        Check(command.layers[0].state=="Hit");++plays;playback=std::make_shared<HitRequestStatus>();playback->entity_generation=20;return playback;
    };
    auto plan=Plan({{"impact",TaskSubmit{"impact",TaskHitCommand{"Victim",{"DamageHit"}}}}});
    TaskPresentation run({91},10,plan,f.player,{},f.Services());run.Service(0,1);run.Service(0,2);
    Check(sends==1 && plays==0 && !run.Drained());inbox->Service(resolve,submit);Check(plays==0);
    ready=true;inbox->Service(resolve,submit);run.Service(0,3);
    Check(plays==1 && run.Drained() && run.Status().reason==TaskExitReason::Completed && !playback->cancelled);
    run.Stop(TaskExitReason::Deleted);run.Service(0,4);Check(!playback->cancelled);
    // Source cancellation before admission retracts the queued value. It does
    // not reset any animation on the target or retry against another actor.
    ready=false;
    TaskPresentation cancelled({92},10,plan,f.player,{},f.Services());cancelled.Service(0,5);
    cancelled.Stop(TaskExitReason::Deleted);ready=true;inbox->Service(resolve,submit);cancelled.Service(0,6);
    Check(cancelled.Drained() && plays==1 && !playback->cancelled);
    // Retiring the bound target reports failure, not a fallback to the caster.
    ready=false;TaskPresentation lost({93},10,plan,f.player,{},f.Services());lost.Service(0,7);
    inbox->Close();lost.Service(0,8);lost.Service(0,9);
    Check(lost.Drained() && lost.Status().reason==TaskExitReason::Failed && plays==1);
    bool closed=false;try{inbox->Post({"DamageHit"});}catch(const std::logic_error&){closed=true;}Check(closed);
    // Cancellation/owner close during a policy read precedes the commit.
    HitInbox reentrant(30);auto pending=reentrant.Post({"DamageHit"});
    reentrant.Service([&](const auto&){pending.cancel();return HitResolution{true,HitCommand{{{"Add","Hit",1}}}};},submit);
    Check(pending.status->cancelled && plays==1);
    // Empty HitAnimation is a delivered no-op, not a missing implementation.
    auto ignored=reentrant.Post({});
    reentrant.Service([](const auto&){return HitResolution{true,{}};},submit);
    Check(ignored.status->delivered && ignored.status->ignored && !ignored.status->playback && plays==1);
}
}
void TestTaskPresentation(){Scopes();IndependentParticipants();ConfiguredPoses();ErrorsAndReentry();NamedContexts();IndependentTail();Sounds();Movements();MovementInputs();AbilityTailScope();HitDispatch();}
