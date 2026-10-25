#include <hksr/framework/presentation/ReactionPolicy.hpp>
#include <hksr/framework/tasks/BattleControl.hpp>
#include <stdexcept>
#include <limits>

namespace {
using namespace hksr;
using namespace hksr::framework;
void CheckAt(bool ok,int line){if(!ok)throw std::runtime_error("reaction policy test failed at line "+std::to_string(line));}
#define Check(...) CheckAt((__VA_ARGS__),__LINE__)
api::TextInput Text(std::string value){return {true,std::move(value),{}};}
api::VisualReactionInput Reactions(const std::string& prefix) {
    api::VisualReactionInput out;out.present=true;
    out.hit=Text(prefix+"Hit");out.hit_h=Text(prefix+"Heavy");out.hit_fly=Text(prefix+"Launch");out.fly_hit=Text(prefix+"Air");
    out.force_hit_h={true,false,{}};out.hit_weight={true,-1,{}};return out;
}
template<class F>void Reject(F function){bool rejected=false;try{function();}catch(const std::exception&){rejected=true;}Check(rejected);}
}
void TestReactionPolicy() {
    BattleControlSnapshot control;
    control.sources={{"visual-only",12,10,1,std::vector<std::int32_t>{}},
        {"independent-behavior",99,20,2,std::vector<std::int32_t>{12,21}}};
    control.dominant_source=control.sources[0];
    auto flags=ResolveHitControl(control);
    Check(flags.visual_flag==12 && flags.endurance && flags.mute_heavy && !flags.force_heavy);
    control.sources.pop_back();Check(!ResolveHitControl(control).endurance); // visual flag 12 is not Endurance
    control.sources[0].behaviors.reset();Reject([&]{ResolveHitControl(control);});
    control={};Check(!ResolveHitControl(control).visual_flag && !ResolveHitControl(control).endurance);
    auto defaults=Reactions("base."),phase=Reactions("phase.");
    api::VisualBehaviorInput visual;visual.flag={true,99,{}};visual.force_hit_h={true,false,{}};
    visual.repeat_occur_at={true,-1,{}};visual.reactions=Reactions("visual.");visual.occur.state=Text("abnormal.enter");
    api::HitAnimationDefaults global;global.resilience_weights={true,{.25f,.5f,.75f,1.f,1.25f},{}};
    global.minimum_frame_interval={true,2,{}};
    HitReceiverState state;state.resilience=3;state.frame=20;
    ReactionSources sources{defaults};
    auto decide=[&](const std::string& name){return SelectHit(name,state,sources,global);};
    auto value=decide("DamageHit");Check(value.kind==HitKind::Additive && value.state=="base.Hit" && value.weight==1);
    Check(decide("").kind==HitKind::None);
    Check(decide("DamageHit_H").kind==HitKind::Heavy);
    Check(decide("Damage_H_Left").kind==HitKind::Heavy);
    Check(decide("DamageHit_h").kind==HitKind::Additive);
    Check(decide("Heavy").kind==HitKind::Additive);
    state.endurance=true;value=decide("DamageHit_H");Check(value.kind==HitKind::Additive && value.weight==.25f);
    state.force_heavy=true;Check(decide("DamageHit").kind==HitKind::Heavy);
    state.mute_heavy=true;Check(decide("DamageHit_H").kind==HitKind::Additive);
    state={};state.resilience=3;state.frame=20;sources.phase=&phase;
    value=decide("DamageHit");Check(value.state=="phase.Hit");
    phase.hit=Text("");Check(decide("DamageHit").state=="base.Hit");
    phase.hit.available=false;Reject([&]{decide("DamageHit");});phase.hit=Text("phase.Hit");
    phase.hit_weight={true,.4f,{}};state.entity_weight=.7f;Check(decide("DamageHit").weight==.4f);
    state.resilience.reset();state.entity_weight.reset();
    Check(decide("DamageHit").weight==.4f); // unread inputs not consumed by phase override
    Check(decide("DamageHit_H").kind==HitKind::Heavy);
    phase.hit_weight.value=-1;Reject([&]{decide("DamageHit");});
    state.resilience=3;state.entity_weight=.7f;
    phase.hit_weight.value=-1;Check(decide("DamageHit").weight==.7f);
    phase.force_hit_h.value=true;Check(decide("DamageHit").kind==HitKind::Heavy);
    sources.visual=&visual;
    // A present visual row supplies its own force decision, not a merged OR
    // of every phase/default row. Name fallback is a different operation.
    Check(decide("DamageHit").kind==HitKind::Additive);
    Check(decide("DamageHit").state=="visual.Hit");
    visual.reactions.hit=Text("");Check(decide("DamageHit").state=="phase.Hit");
    visual.force_hit_h.value=true;state.mute_heavy=true;Check(decide("DamageHit").kind==HitKind::Additive);
    visual.flag.value=133;Check(decide("DamageHit").kind==HitKind::Heavy);
    state.endurance=true;Check(decide("DamageHit").kind==HitKind::Heavy);
    state.height=.10001f;Check(decide("DamageHit_H").kind==HitKind::Airborne);
    state.height=.1f;state.endurance=false;state.launch_allowed=state.has_launch_motion=true;
    Check(decide("DamageHit").kind==HitKind::Launch);
    state.launch_allowed=false;state.has_launch_motion=false;state.mute_heavy=false;visual.force_hit_h.value=false;
    phase.force_hit_h.value=false;visual.repeat_occur_at.value=.35f;state.in_occur=true;state.occur_progress=.35f;
    value=decide("DamageHit");Check(value.repeated_occur && value.kind==HitKind::Heavy && value.state=="abnormal.enter");
    state.occur_progress=.36f;Check(!decide("DamageHit").repeated_occur);
    state.in_occur.reset();Reject([&]{decide("DamageHit");});
    visual.repeat_occur_at.value=-1;Check(!decide("DamageHit").repeated_occur);
    state.in_occur=true;visual.repeat_occur_at.value=.35f;
    state.repeat_occur_at=.5f;Check(decide("DamageHit").repeated_occur);
    state.height=1;Check(!decide("DamageHit").repeated_occur);
    state.height=0;state.in_occur=false;
    // The gate compares final resolved names and original frame numbers.
    auto previous=PreviousHit{"phase.Hit",18};
    Check(SelectHit("DamageHit",state,sources,global,previous).throttled);
    previous.frame=17;Check(!SelectHit("DamageHit",state,sources,global,previous).throttled);
    previous.frame=0;Check(!SelectHit("DamageHit",state,sources,global,previous).throttled);
    previous={"visual.Launch",20};state.launch_allowed=state.has_launch_motion=true;
    Check(!SelectHit("DamageHit",state,sources,global,previous).throttled);
    state.launch_allowed=state.has_launch_motion=false;
    state.resilience=99;Reject([&]{decide("DamageHit");});state.endurance=true;Check(decide("DamageHit").weight==.175f);
    state.height=std::numeric_limits<float>::quiet_NaN();Reject([&]{decide("DamageHit");});state.height=0;

    HitPlaybackDefaults names{"Visual","Additive","Override","EmoHit",.15f,.2f,2};
    auto command=BuildHitCommand({HitKind::Additive,"Hit",.75f},names,.4f);
    Check(!command.body && command.layers.size()==2 && command.layers[0].weight==.75f && command.layers[1].state=="Hit");
    Check(command.layers[1].start==.4f && command.expression=="EmoHit" && command.blink_source==2);
    command=BuildHitCommand({HitKind::Heavy,"Heavy"},names);
    Check(command.body && command.body->layer=="Visual" && command.body->state=="Heavy");
    Check(command.layers[0].weight==0 && command.layers[0].state.empty() && command.layers[1].state.empty());
    Reject([&]{BuildHitCommand({},names);});
    Reject([&]{BuildHitCommand({HitKind::Additive,"Hit",1,false,true},names);});

    api::PhaseVisualConfigSources active;active.standby=Text("phase.idle");
    auto idle=SelectIdle(Text("default.idle"),Text("default.face"),&active,nullptr,nullptr);
    Check(idle.state=="phase.idle" && idle.expression=="default.face" && !idle.visual);
    active.standby=Text("");Check(SelectIdle(Text("default.idle"),Text("default.face"),&active,nullptr,nullptr).state=="default.idle");
    visual.loop.state=Text("ignored.literal");visual.loop.resolved_state=Text("phase.abnormal.loop");
    visual.emotion=Text("not.the.global.expression");auto expression=Text("global.face");
    idle=SelectIdle(Text("default.idle"),Text("default.face"),&active,&visual,&expression);
    Check(idle.visual && idle.state=="phase.abnormal.loop" && idle.expression=="global.face");
    visual.loop.resolved_state=Text("");expression=Text("");
    idle=SelectIdle(Text("default.idle"),Text("default.face"),&active,&visual,&expression);
    Check(idle.state.empty() && idle.expression=="default.face");
    visual.loop.resolved_state.available=false;
    Reject([&]{SelectIdle(Text("default.idle"),Text("default.face"),&active,&visual,&expression);});
}
