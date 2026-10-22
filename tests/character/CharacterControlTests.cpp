#include <hksr/framework/character/CharacterControls.hpp>
#include <hksr/presentation/PresentationBindings.hpp>
#include <hksr/framework/host/RuntimeServices.hpp>
#include <hksr/entity/LifecycleHooks.hpp>
#include <Windows.h>
#include <map>
#include <stdexcept>
#include <iostream>

namespace {
using namespace hksr;
int info,world,controller_type,driver_type,model_type,head_name,next=0;
struct Node {api::Position3 p;api::Rotation4 q;bool alive=true;};
struct Actor {Node root,head,look;bool automatic=true;void* target=nullptr;};
Actor actor,other;Node anchor;
std::map<int,void*> roots;
unsigned flushes=0,places=0,follows=0,stops=0,allocations=0;
void CheckAt(bool v,int line){if(!v){std::cerr<<"character controls assertion line "<<line<<'\n';throw std::runtime_error("character controls test failed");}}
#define Check(value) CheckAt((value),__LINE__)
template<class F> api::Method M(F f){return {reinterpret_cast<void*>(f),&info};}
std::int32_t __fastcall Keep(void* p,int,int,void*){roots[++next]=p;return next;}
void* __fastcall Target(int id,void*){return roots.at(id);}
void __fastcall Free(int id,void*){roots.erase(id);}
api::GameWorld* __fastcall World(void*,void*){return reinterpret_cast<api::GameWorld*>(&world);}
bool __fastcall Loaded(void*,void*){return true;}
void* __fastcall Component(void* value,void* type,void*){Check(type==&controller_type || type==&driver_type || type==&model_type);return value;}
void* __fastcall GameObject(Actor* value,void*){return &value->root;}
void* __fastcall Transform(void* value,void*){return value;}
void __fastcall Position(Node* value,api::Position3* out,void*){*out=value->p;}
void __fastcall Rotation(Node* value,api::Rotation4* out,void*){*out=value->q;}
__declspec(noinline) void __fastcall Flush(Actor* value,void*){value->root.p={-9,-8,-7};++flushes;MemoryBarrier();}
void __fastcall Place(Actor* value,const api::Position3* p,const api::Rotation4* q,void*){Check(flushes>0);value->root.p=*p;value->root.q=*q;++places;}
void* __fastcall Mono(void* value,void*){return value;}
void* __fastcall Head(Actor* value,void*){return &value->head;}
void* __fastcall ModelHead(Actor* value,void* name,bool fallback,void*){Check(name==&head_name && !fallback);return &value->head;}
void* __fastcall Look(Actor* value,void*){return &value->look;}
bool __fastcall Automatic(Actor* value,void*){return value->automatic;}
void __fastcall SetAutomatic(Actor* value,bool enabled,void*){value->automatic=enabled;}
void __fastcall Follow(Actor* value,void* target,bool force,bool second,void* constraint,void*){Check(force && !second && !constraint);value->target=target;++follows;}
void __fastcall Stop(Actor* value,bool second,void*){Check(!second);value->target=nullptr;++stops;}
void __fastcall Point(Node* value,const api::Position3* p,void*){value->p=*p;}
bool __fastcall Alive(Node* value,void*){return value->alive;}
void __fastcall Destroy(Node* value,void*){value->alive=false;}
void __fastcall Initialize(Node* value,void*){value->alive=true;}
struct Objects:api::ManagedObjects {
    api::ObjectResult ManagedType(const char* ns,const char* name) override {
        if(std::string(ns)=="RPG.GameCore" && std::string(name)=="AdventureCharacterController")return {{true,0},&controller_type};
        if(std::string(ns)=="RPG.GameCore" && std::string(name)=="CharacterModelComponent")return {{true,0},&model_type};
        Check(std::string(name)=="PKBOEALAKCM");return {{true,0},&driver_type};
    }
    api::ObjectResult AllocateObject(const char* ns,const char* type) override {Check(std::string(ns)=="UnityEngine" && std::string(type)=="GameObject");++allocations;return {{true,0},&anchor};}
    api::ObjectResult NewStringUtf8(const char* value) override {Check(std::string(value)=="Head");return {{true,0},&head_name};}
};
}
void TestCharacterControls() {
    api::PresentationBindings b;
    b.references={M(Keep),M(Target),M(Free),{}, {}};b.lifetime={M(Alive),M(Destroy)};
    b.entity.owner_world=M(World);b.entity.loaded=M(Loaded);b.entity.component=M(Component);
    b.placement.game_object=M(GameObject);b.placement.transform=M(Transform);b.placement.position=M(Position);b.placement.rotation=M(Rotation);
    b.actor_control={M(Flush),M(Place),M(Mono),M(Automatic),M(SetAutomatic),M(Head),M(Look),M(Follow),M(Stop),M(Point),{}};
    b.actor_control.model_attach_point=M(ModelHead);
    b.scene.transform=M(Transform);b.visual.initialize=M(Initialize);
    Objects objects;framework::RuntimeServices services(objects,b);
    Check(bool(services.Sessions(false).handoff.sound));
    api::WorldFrame frame;frame.world=reinterpret_cast<api::GameWorld*>(&world);frame.disposing={false,{true,0}};
    auto token=api::TrackedEntities().Track(reinterpret_cast<api::GameEntity*>(&actor));
    api::InstallMovementPlacement(b.actor_control);
    framework::CharacterControls controls(services.Controls()(token,frame.world));
    api::PlayerPlacement fallback;fallback.ready=true;fallback.position={4,5,6};fallback.rotation={0,0,0,1};
    controls.Place({1,2,3},{0,0,0,1},true);
    Check(controls.PlannedPlacement(fallback).position.x==5 && controls.PlannedPlacement(fallback).position.z==9);
    const auto position=controls.Place({10,20,30},{0,0,0,1},false);
    Check(controls.PlannedPlacement().ready && controls.PlannedPlacement().position.x==10);
    controls.Service(frame,services.Appearance(),nullptr,true);
    Check(places==0 && controls.Status().placement_pending);
    Flush(&other,&info);Check(places==0);
    Flush(&actor,&info);Check(places==1 && actor.root.p.x==10 && actor.root.p.z==30);
    controls.Service(frame,services.Appearance(),nullptr,true);Check(!controls.Status().placement_pending && controls.Status().live.position.y==20);
    Check(position->phase==api::ControlPhase::Returned);
    controls.Place({1,2,3},{0,1,0,0},true);controls.Service(frame,services.Appearance(),nullptr,true);
    controls.Place({2,0,0},{0,0,0,1},true);
    Check(controls.PlannedPlacement().position.x==13 && controls.PlannedPlacement().rotation.y==1);
    controls.Service(frame,services.Appearance(),nullptr,true);
    Flush(&actor,&info);Check(places==2 && actor.root.p.x==13 && actor.root.p.y==22 && actor.root.q.y==1);
    const auto gaze=controls.Look({framework::LookMode::Point,0,{2,3,4}});controls.Service(frame,services.Appearance(),nullptr,true);
    Check(gaze->phase==api::ControlPhase::Returned);
    Check(!actor.automatic && actor.target==&anchor && anchor.p.z==4 && follows==1 && allocations==1);
    controls.Look({framework::LookMode::Point,0,{5,6,7}});controls.Service(frame,services.Appearance(),nullptr,true);
    Check(anchor.p.z==7 && allocations==1);
    const auto count=follows;controls.Service(frame,services.Appearance(),nullptr,true);Check(follows==count);
    controls.Look({framework::LookMode::Instance,2,{}});controls.Service(frame,services.Appearance(),reinterpret_cast<api::GameEntity*>(&other),true);
    Check(actor.target==&other.head);
    controls.Service(frame,services.Appearance(),nullptr,true);Check(actor.target==nullptr); // target removed
    controls.Look({framework::LookMode::Automatic,0,{}});controls.Service(frame,services.Appearance(),nullptr,true);Check(actor.automatic);
    controls.Look({framework::LookMode::Disabled,0,{}});controls.Service(frame,services.Appearance(),nullptr,true);Check(!actor.automatic);
    Check(controls.Suspend()==api::ControlPhase::Returned && actor.automatic);
    controls.Place({99,0,0},{0,0,0,1},false);controls.Service(frame,services.Appearance(),nullptr,true);
    controls.Cancel();Check(controls.PlannedPlacement().position.x==13);Flush(&actor,&info);Check(places==2);
    Check(!controls.Close(frame) && !anchor.alive); // Destroy is observed on a later service
    Check(controls.Close(frame) && roots.empty());
    api::TrackedEntities().Disposed(token);api::TrackedEntities().Forget(token);
    api::RemoveMovementPlacementAtQuiescentPoint();
    std::cout<<"Character controls passed: actual post-flush hook, actor isolation, combined offsets, cancellation, live pose, reusable point, target loss and policy restoration; local only.\n";
}
