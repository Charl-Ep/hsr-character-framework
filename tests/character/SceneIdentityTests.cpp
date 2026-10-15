#include <hksr/framework/character/SceneIdentity.hpp>
#include <Windows.h>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
using namespace hksr::api;
using namespace hksr::framework;
int world,other_world,phase,group,other_group,map,info;
int next_handle=0,retains=0,frees=0,fail_retain_at=0;
bool fail_free=false,fail_target=false;
std::map<int,void*> roots;
void Check(bool value){if(!value)throw std::runtime_error("scene identity test failed");}
std::int32_t __fastcall Retain(void* object,std::int32_t prior,std::int32_t kind,void* metadata){
    Check(prior==0 && kind==2 && metadata==&info);++retains;
    if(retains==fail_retain_at)return 0;
    roots[++next_handle]=object;return next_handle;
}
void* __fastcall Target(std::int32_t handle,void* metadata){Check(metadata==&info);if(fail_target)RaiseException(0xE0630010,0,0,nullptr);return roots.at(handle);}
void __fastcall Free(std::int32_t handle,void* metadata){
    Check(metadata==&info);++frees;
    if(fail_free)RaiseException(0xE0630011,0,0,nullptr);
    Check(roots.erase(handle)==1);
}
template<class F> Method M(F function){return {reinterpret_cast<void*>(function),&info};}
}
void TestSceneIdentity(){
    const ReferenceMethods methods{M(Retain),M(Target),M(Free),{}, {}};
    SceneIdentity identity(std::make_unique<hksr::api::ManagedRoots>(methods));
    WorldFrame frame;frame.world=reinterpret_cast<GameWorld*>(&world);frame.adventure_phase={&phase,{true,0}};
    frame.disposing={false,{true,0}};frame.battle={false,{true,0}};
    WorldMapSnapshot snapshot;snapshot.group_manager={&group,{true,0}};snapshot.map={&map,{true,0}};
    for(auto& part:snapshot.identity)part={0,{true,0}};snapshot.identity[0].value=100;snapshot.identity[1].value=200;
    auto a=identity.Observe(frame,snapshot);Check(a && a->key=="hsr-map-v1:100:200:0:0:0:0" && a->incarnation==1 && roots.size()==4);
    auto b=identity.Observe(frame,snapshot);Check(b && b->incarnation==1 && retains==4);
    // Neither an unavailable runtime ID nor an unrelated battle frame creates
    // a generation or invents a missing persistent-key component.
    frame.battle.value=true;frame.world=reinterpret_cast<GameWorld*>(&other_world);
    Check(!identity.Observe(frame,snapshot) && roots.size()==4);
    frame.battle.value=false;frame.world=reinterpret_cast<GameWorld*>(&world);
    snapshot.identity[3].read.returned=false;Check(!identity.Observe(frame,snapshot) && !identity.Error().empty());
    snapshot.identity[3].read.returned=true;Check(identity.Observe(frame,snapshot)->incarnation==1);
    fail_target=true;Check(!identity.Observe(frame,snapshot));fail_target=false;
    Check(identity.Observe(frame,snapshot)->incarnation==1);
    // Official ordinary initialization creates a new manager even for the
    // same MapDef/world. This is a new incarnation, not a new saved map key.
    snapshot.group_manager.value=&other_group;b=identity.Observe(frame,snapshot);
    Check(b && b->incarnation==2 && b->key==a->key && roots.size()==4);
    snapshot.group_manager.read.returned=false;Check(!identity.Observe(frame,snapshot) && roots.size()==4);
    snapshot.group_manager.read.returned=true;Check(identity.Observe(frame,snapshot)->incarnation==2);
    snapshot.group_manager.value=nullptr;Check(!identity.Observe(frame,snapshot) && roots.empty());
    snapshot.group_manager.value=&group;Check(identity.Observe(frame,snapshot)->incarnation==3);
    // Failed replacement retains its receipt, releases partial roots and does
    // not discard the prior known identity or consume an incarnation number.
    snapshot.map.value=&other_world;fail_retain_at=retains+2;
    Check(!identity.Observe(frame,snapshot) && roots.size()==4);fail_retain_at=0;
    snapshot.map.value=&map;Check(identity.Observe(frame,snapshot)->incarnation==3);
    snapshot.identity[1].value=201;Check(identity.Observe(frame,snapshot)->incarnation==4);
    frame.disposing.value=true;Check(!identity.Observe(frame,snapshot) && roots.empty());
    frame.disposing.value=false;Check(identity.Observe(frame,snapshot)->incarnation==5);
    identity.Close();Check(identity.Closed() && roots.empty() && !identity.Observe(frame,snapshot));
    // Unknown release is recorded, never decremented twice, and cannot block
    // an independently retained replacement map generation.
    SceneIdentity uncertain(std::make_unique<hksr::api::ManagedRoots>(methods));Check(uncertain.Observe(frame,snapshot).has_value());
    fail_free=true;snapshot.group_manager.value=&other_group;Check(uncertain.Observe(frame,snapshot)->incarnation==2);
    const auto attempts=frees;fail_free=false;
    Check(uncertain.Observe(frame,snapshot)->incarnation==2 && frees==attempts && uncertain.PendingRoots()==8);
    uncertain.Close();Check(!uncertain.Closed() && uncertain.PendingRoots()==4 && roots.size()==4);
    roots.clear(); // only the local fixture process is ending; no runtime retry
    std::cout<<"Scene identities passed: retained generations, missing versus unread map, zero fields, independent old-root cleanup.\n";
}
