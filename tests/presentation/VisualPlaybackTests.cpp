#include <hksr/framework/presentation/ActionPlayback.hpp>
#include <Windows.h>
#include <array>
#include <map>
#include <memory>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
using namespace hksr;
int entity,world,model,animator,style,model_type,style_type,info,next_root;
int emotion_type;
struct BlinkMask {bool active[4]{},enabled[4]{};} blink_mask;
struct Emotion {void* header[2]{};void* mask=&blink_mask;} emotion;
int blink_writes=0;
bool fail_blink_set=false;
std::map<int,void*> roots;
std::vector<std::unique_ptr<std::string>> strings;
struct Layer {int current=100,next=0;float time=0;bool loop=false,transition=false;float next_time=0;};
std::array<Layer,4> layers;
std::map<std::string,int> names{{"Enter",1},{"Hold",2},{"Exit",3},{"StandBy",4},{"Face",5},{"OtherFace",6}};
bool other_model=false,missing_loop=false,duplicate_face=false,fail_face=false,cancel_on_has=false;
int writes=0,body_writes=0;
float last_blend=0;
framework::ActionPlayback* active=nullptr;
void Check(bool yes) {if(!yes)throw std::runtime_error("visual playback integration failed");}
template<class T> api::Method M(T f){return {reinterpret_cast<void*>(f),&info};}
int __fastcall Keep(void* o,int,int,void*){roots[++next_root]=o;return next_root;}
void* __fastcall Target(int i,void*){return roots.at(i);}
void __fastcall Free(int i,void*){roots.erase(i);}
api::GameWorld* __fastcall World(void* o,void*){Check(o==&entity);return reinterpret_cast<api::GameWorld*>(&world);}
bool __fastcall Loaded(void*,void*){return true;}
void* __fastcall Component(void* o,void* type,void*){Check(o==&entity);if(type==&model_type)return &model;if(type==&emotion_type)return &emotion;Check(type==&style_type);return &style;}
void* __fastcall Animator(void* o,void*){Check(o==&model);return other_model?&info:&animator;}
bool __fastcall Changing(void*,void*){return false;}
int __fastcall Hash(std::string* name,void*){return names.at(*name);}
int __fastcall Count(void* o,void*){Check(o==&animator);return static_cast<int>(layers.size());}
bool __fastcall Has(void* o,int layer,int hash,void*) {
    Check(o==&animator);
    if(cancel_on_has)active->CancelPending();
    if(layer==0)return hash>0 && hash<=4 && !(hash==2 && missing_loop);
    return (hash==5 || hash==6) && (layer==2 || (duplicate_face && layer==3));
}
void __fastcall StateInfo(void* o,int layer,int index,unsigned char* out,void*) {
    Check(o==&animator && (index==0 || index==1));std::memset(out,0,36);
    const auto& v=layers.at(layer);int hash=index==0?v.current:v.next;
    const auto time=index==0?v.time:v.next_time;
    std::memcpy(out,&hash,4);std::memcpy(out+8,&hash,4);std::memcpy(out+12,&time,4);
    int loop=v.loop?1:0;std::memcpy(out+32,&loop,4);
}
bool __fastcall Transition(void*,int layer,void*){return layers.at(layer).transition;}
void __fastcall Play(void* o,int hash,int layer,float time,void*) {
    Check(o==&animator);++writes;layers.at(layer)={hash,0,time,false,false};
}
void __fastcall Body(void* o,int hash,float blend,float offset,void*) {
    Check(o==&style && offset==0);++writes;++body_writes;last_blend=blend;
    layers[0]={hash,0,0,hash==2,false};
}
void __fastcall Fade(void* o,int hash,float blend,int layer,float offset,float progress,void*) {
    Check(o==&animator && (offset==0 || (hash==100 && offset==0.6f)) && progress==0);++writes;
    if(blend>0){layers.at(layer).next=hash;layers.at(layer).transition=true;layers.at(layer).next_time=offset;}
    else layers.at(layer)={hash,0,0,false,false};
    if(fail_face)RaiseException(0xE06A0001,0,0,nullptr);
}
void __fastcall Npc(void* o,unsigned id,void*){Check(o==&style && id==99);++writes;}
bool __fastcall BlinkActive(void* o,int source,void*){Check(o==&blink_mask && source==3);return blink_mask.active[source];}
bool __fastcall BlinkValue(void* o,int source,void*){Check(o==&blink_mask && source==3);return blink_mask.enabled[source];}
void __fastcall BlinkSet(void* o,int source,bool enabled,void*){
    Check(o==&emotion && source==3);++blink_writes;blink_mask.active[source]=true;blink_mask.enabled[source]=enabled;
    if(fail_blink_set && !enabled)RaiseException(0xE06A0002,0,0,nullptr);
}
struct Objects:api::ManagedObjects {
    api::ObjectResult ManagedType(const char* ns,const char* name)override {
        if(std::string(ns).empty() && std::string(name)=="PEPNFLNCDHG")return {{true,0},&emotion_type};
        Check(std::string(ns)=="RPG.GameCore");
        if(std::string(name)=="CharacterModelComponent")return {{true,0},&model_type};
        Check(std::string(name)=="FreeStyleComponent");return {{true,0},&style_type};
    }
    api::ObjectResult NewStringUtf8(const char* v)override{strings.push_back(std::make_unique<std::string>(v));return {{true,0},strings.back().get()};}
    api::ObjectResult AllocateObject(const char*,const char*)override{throw std::runtime_error("no component construction permitted");}
};
framework::VisualState Recipe() {
    framework::VisualState out;out.source="fixture official config";auto& v=out.value;v.prepared=true;v.flag={true,9001,{}};
    v.occur.resolved_state={true,"Enter",{}};v.loop.resolved_state={true,"Hold",{}};v.recover.resolved_state={true,"Exit",{}};
    v.occur.transition={true,0.1f,{}};v.loop.transition={true,0.2f,{}};v.recover.transition={true,0.3f,{}};
    v.emotion={true,"Face",{}};out.expression=v.emotion;out.expression_transition={true,0.2f,{}};out.blink_source=3;return out;
}
}
void TestVisualPlayback() {
    const api::ReferenceMethods refs{M(Keep),M(Target),M(Free),{}, {}};
    api::EntityAccessMethods em;em.owner_world=M(World);em.loaded=M(Loaded);em.component=M(Component);
    api::ModelAccessMethods mm;mm.animator=M(Animator);mm.changing=M(Changing);
    api::AnimationControlMethods am;am.hash=M(Hash);am.has_state=M(Has);am.play=M(Play);am.free_style_state=M(Body);
    am.state_info=M(StateInfo);am.in_transition=M(Transition);am.layer_count=M(Count);am.cross_fade=M(Fade);am.npc_motion=M(Npc);
    am.blink={M(BlinkSet),M(BlinkValue),M(BlinkActive),offsetof(Emotion,mask),true};
    api::ProfileInputMethods pm;pm.animator_initialized=M(Loaded);
    Objects objects;auto token=api::TrackedEntities().Track(reinterpret_cast<api::GameEntity*>(&entity));
    framework::ActionPlayback player(token,reinterpret_cast<api::GameWorld*>(&world),
        std::make_unique<api::ActorAnimationSession>(token,refs,api::EntityAccess(em),api::ModelAccess(mm),api::AnimationControl(am),api::ProfileInputs(pm)));
    active=&player;player.SetCompletionState("StandBy");
    api::WorldFrame frame;frame.world=reinterpret_cast<api::GameWorld*>(&world);frame.disposing={false,{true,0}};
    auto tick=[&]{++frame.sequence;player.Service(frame,true,objects);};
    auto enter=[&]{player.Queue(Recipe());tick();Check(player.Status().last->outcome==framework::ActionOutcome::Returned);};
    layers[2]={100,0,0.6f,false,false};enter();Check(layers[0].current==1 && layers[2].next==5 && last_blend==0.1f);
    Check(blink_mask.active[3] && !blink_mask.enabled[3]);
    blink_mask.active[1]=true;blink_mask.enabled[1]=false; // unrelated Frozen source
    // Cancel DURING expression transition: inspect its target, not only the
    // current state, and restore the real baseline, never Emo_Default by name.
    player.Queue(framework::NpcMotion{99});tick();Check(layers[2].next==100 && layers[2].next_time==0.6f && layers[2].transition);
    Check(blink_mask.enabled[3] && blink_mask.active[1] && !blink_mask.enabled[1]);
    enter();layers[2]={5,0,0,false,false};layers[0].time=1.1f;tick();Check(layers[0].current==2 && last_blend==0.2f);
    int before=body_writes;tick();tick();Check(body_writes==before); // no repeated loop dispatch
    auto exit=Recipe();exit.recover=true;const auto recovery=player.Queue(exit);tick();Check(layers[0].current==3 && last_blend==0.3f);
    Check(layers[0].time==0 && layers[2].transition && layers[2].next==100); // restoration starts WITH recovery
    Check(blink_mask.enabled[3]);
    layers[2]={100,0,0.6f,false,false};layers[0].time=1.2f;tick();Check(layers[0].current==4 && layers[2].current==100);
    Check(player.Status().successor && player.Status().successor->predecessor==recovery && player.Status().successor->returned);
    // An official recovery successor updates persistence without replaying it.
    const auto official_recovery=player.Queue(exit);tick();tick();before=body_writes;
    layers[0]={4,0,0.2f,true,false};tick();Check(body_writes==before);
    Check(player.Status().successor && player.Status().successor->predecessor==official_recovery && player.Status().successor->observed);
    // Natural occurrence -> loop takes precedence; no duplicate manual play.
    enter();before=body_writes;layers[0]={2,0,0.2f,true,false};tick();Check(body_writes==before);
    // A later official expression is not overwritten by cancellation.
    layers[2]={6,0,0.25f,false,false};player.Queue(framework::NpcMotion{99});tick();Check(layers[2].current==6);
    // Switching models retires tracking without touching the old Animator.
    enter();before=writes;other_model=true;tick();Check(writes==before);other_model=false;
    Check(blink_mask.enabled[3]); // entity-level source released across Art changes
    missing_loop=true;before=writes;player.Queue(Recipe());tick();Check(writes==before && player.Status().last->outcome==framework::ActionOutcome::StateAbsent);missing_loop=false;
    duplicate_face=true;player.Queue(Recipe());tick();Check(writes==before && player.Status().last->outcome==framework::ActionOutcome::Unavailable);duplicate_face=false;
    cancel_on_has=true;player.Queue(Recipe());tick();Check(writes==before && player.Status().last->outcome==framework::ActionOutcome::Cancelled);cancel_on_has=false;
    // Partial native write is still cleaned up on a later engine boundary.
    layers[2]={100,0,0.6f,false,false};fail_face=true;player.Queue(Recipe());tick();fail_face=false;
    Check(player.Status().last->outcome==framework::ActionOutcome::CallFailed);tick();Check(layers[2].next==100);
    layers[2]={100,0,0.6f,false,false};
    // Empty expression means no expression request, not a fabricated default.
    auto no_face=Recipe();no_face.expression.value.clear();player.Queue(no_face);tick();Check(layers[2].current==100);
    player.Queue(framework::NpcMotion{99});tick();
    blink_mask.active[3]=true;blink_mask.enabled[3]=false;
    const auto existing_writes=blink_writes;enter();player.Queue(framework::NpcMotion{99});tick();
    Check(blink_writes==existing_writes && !blink_mask.enabled[3]); // preexisting block not ours
    blink_mask.enabled[3]=true;
    fail_blink_set=true;enter();fail_blink_set=false;
    Check(blink_mask.enabled[3] && player.Status().blink_note=="blink source call failed");
    player.Queue(framework::NpcMotion{99});tick();
    emotion.mask=nullptr;before=blink_writes;enter();Check(blink_writes==before && player.Status().blink_note=="blink source snapshot unavailable");
    emotion.mask=&blink_mask;
    player.Queue(framework::NpcMotion{99});tick();
    layers[2]={100,0,0.6f,false,false};
    const auto held=player.QueuePose(Recipe());tick();
    Check(held->accepted && !held->entered);
    layers[0].time=1.2f;tick();tick();Check(held->entered && !held->complete);
    const auto held_body=body_writes;player.Queue(framework::AnimatorState{"OtherFace",2,0});tick();tick();
    Check(!held->released && !held->complete && body_writes==held_body && layers[0].current==2);
    player.ReleasePose(held->serial);
    const auto recovered=player.QueuePose(exit);tick();
    Check(recovered->accepted && !recovered->complete && blink_mask.enabled[3] && layers[2].current==6);
    before=body_writes;layers[0].time=1.2f;tick();
    Check(recovered->complete && recovered->error.empty() && layers[0].current==3 && body_writes==before);
    Check(!player.Status().successor); // task controller, not this player, chooses the next state
    player.ReleasePose(recovered->serial);
    const auto stopped=player.QueuePose(Recipe());tick();player.ReleasePose(stopped->serial,true);tick();
    Check(stopped->released && blink_mask.enabled[3]);
    api::TrackedEntities().Disposed(token);before=writes;tick();Check(writes==before);
    Check(player.Close(frame) && roots.empty());api::TrackedEntities().Forget(token);
    std::cout<<"Visual row playback: entry/hold/recovery, official successor, transition-target cleanup, NPC interruption, model replacement, partial write and owner end passed; no game acceptance.\n";
}
