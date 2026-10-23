#include <hksr/framework/presentation/VisualEntityMotion.hpp>
#include <hksr/framework/presentation/VisualProgram.hpp>
#include <map>
#include <stdexcept>

namespace {
void CheckAt(bool ok,int line){if(!ok)throw std::runtime_error("visual entity motion test failed at "+std::to_string(line));}
#define Check(x) CheckAt((x),__LINE__)
std::map<int,void*> motion_roots;int motion_root_id=0;
int __fastcall MotionKeep(void* value,int,int,void*){motion_roots[++motion_root_id]=value;return motion_root_id;}
void* __fastcall MotionTarget(int id,void*){return motion_roots.at(id);}
void __fastcall MotionFree(int id,void*){motion_roots.erase(id);}
template<class F>hksr::api::Method MotionMethod(F f){return {reinterpret_cast<void*>(f),nullptr};}
}
void TestVisualEntityMotion() {
    using namespace hksr;using namespace hksr::framework;
    int world_storage,other_world_storage,owner_storage,a_storage,b_storage,first_storage,second_storage;
    auto* world=reinterpret_cast<api::GameWorld*>(&world_storage);
    api::EntityLifecycle lifecycle;
    const api::EntityOrigin owner{lifecycle.Track(reinterpret_cast<api::GameEntity*>(&owner_storage)),world};
    api::EntityOrigin a{lifecycle.Track(reinterpret_cast<api::GameEntity*>(&a_storage)),world};
    const api::EntityOrigin b{lifecycle.Track(reinterpret_cast<api::GameEntity*>(&b_storage)),world};
    const api::EffectReference first{1,0,&first_storage},second{2,0,&second_storage};
    std::map<void*,std::vector<void*>> lists;
    std::map<void*,bool> attached;
    bool animation_motion=false,other_motion=false;
    unsigned binds=0,clears=0;bool loading=false,partial=false,deny_reads=false;
    std::vector<std::string> errors;
    VisualEntityMotionServices services;
    services.eligible=[&](auto origin){return origin.world==world && origin.token.entity!=nullptr;};
    services.target=[&](void* entity){return api::EffectMotionTargetObservation{{api::EffectControlState::Applied},attached[entity],animation_motion,other_motion};};
    services.read=[&](auto ref){Check(!deny_reads);return api::EffectEntityFollowObservation{{loading?api::EffectControlState::NotLoaded:api::EffectControlState::Applied},lists[ref.parameters]};};
    services.set=[&](auto ref,const auto& expected,const auto& desired,auto current) {
        Check(current());
        Check(expected==lists[ref.parameters]);
        for(auto* entity:expected)attached[entity]=false;
        lists[ref.parameters]=desired;for(auto* entity:desired)attached[entity]=true;
        if(desired.empty())++clears;else ++binds;
        return api::EffectEntityFollowObservation{{partial && !desired.empty()?api::EffectControlState::Failed:api::EffectControlState::Applied},desired};
    };
    services.trace=[&](const auto& text){errors.push_back(text);};
    VisualEntityMotion motion(lifecycle,services);
    auto ok=[](const auto& result){return result.state==api::EffectControlState::Applied;};
    loading=true;Check(motion.Bind(first,owner,{a}).state==api::EffectControlState::NotLoaded && motion.Empty());loading=false;
    attached[a.token.entity]=true;Check(!ok(motion.Bind(first,owner,{a})) && binds==0);attached[a.token.entity]=false;
    animation_motion=true;Check(!ok(motion.Bind(first,owner,{a})) && binds==0);animation_motion=false;
    other_motion=true;Check(!ok(motion.Bind(first,owner,{a})) && binds==0);other_motion=false;
    auto wrong_world=a;wrong_world.world=reinterpret_cast<api::GameWorld*>(&other_world_storage);
    Check(!ok(motion.Bind(first,owner,{wrong_world})) && !ok(motion.Bind(first,owner,{a,a})) && motion.Empty());
    Check(ok(motion.Bind(first,owner,{a,b})) && binds==1);
    Check(ok(motion.Bind(first,owner,{a,b})) && binds==1); // no accumulated-motion reset
    Check(!ok(motion.Bind(second,owner,{a})) && binds==1);
    Check(!ok(motion.Bind(first,owner,{b})) && binds==1);
    const auto notice=lifecycle.DisposalStarting(a.token);
    Check(notice.invoked==1 && notice.failed==0 && clears==1 && motion.Empty());
    Check(!attached[a.token.entity] && !attached[b.token.entity] && !lifecycle.IsDisposed(a.token));
    Check(!ok(motion.Bind(second,owner,{a}))); // retiring target cannot be acquired again
    lifecycle.Disposed(a.token);lifecycle.Forget(a.token);
    const auto old=a;a.token=lifecycle.Track(a.token.entity);
    Check(!ok(motion.Bind(first,owner,{old})) && ok(motion.Bind(first,owner,{a})));
    Check(ok(motion.Release(first)) && clears==2 && motion.Empty());
    partial=true;Check(!ok(motion.Bind(first,owner,{a})) && motion.Empty() && !attached[a.token.entity]);partial=false;
    Check(clears==3);
    Check(ok(motion.Bind(first,owner,{a})));
    lists[first.parameters]={b.token.entity};Check(!ok(motion.Release(first)) && clears==3 && !errors.empty());
    lists[first.parameters]={a.token.entity};Check(ok(motion.Release(first)) && clears==4);
    Check(ok(motion.Bind(first,owner,{a})));
    // Official natural plugin disposal clears its own motion before request
    // collection. RequestEnded must not read through the pooled parameters.
    attached[a.token.entity]=false;lists[first.parameters].clear();deny_reads=true;
    motion.RequestEnded(first);Check(motion.Empty());deny_reads=false;
    Check(ok(motion.Bind(second,owner,{b})));
    const auto owner_notice=lifecycle.DisposalStarting(owner.token);
    Check(owner_notice.invoked==1 && motion.Empty() && !attached[b.token.entity]);
    lifecycle.Forget(owner.token);lifecycle.Forget(a.token);lifecycle.Forget(b.token);

    // The shared SDK motion lease is retained while ordinary motion drains;
    // neither a repeated Bind nor cancellation creates a second component.
    const api::EntityOrigin new_owner{lifecycle.Track(owner.token.entity),world};
    const api::EntityOrigin target{lifecycle.Track(a.token.entity),world};
    bool root=true,callback=true,pending=true;int acquisitions=0;int native_component=0;
    services.acquire=[&](api::EntityOrigin t) {
        ++acquisitions;api::MovementHostServices h;
        h.references={MotionMethod(MotionKeep),MotionMethod(MotionTarget),MotionMethod(MotionFree),{}, {}};
        h.current=[&]{return !lifecycle.IsRetiring(target.token);};h.alive=[&]{return !lifecycle.IsDisposed(target.token);};
        h.lookup=[&]{return api::ObjectResult{{true,0},&native_component};};
        h.create=[]()->api::ObjectResult{throw std::runtime_error("unexpected duplicate movement component");};
        h.initialize=[](void*){return api::CallResult{true,0};};h.remove=[](void*){Check(false);return api::CallResult{};};
        h.observe=[&,t](void*){api::AnimationMovementObservation o;o.read.returned=true;o.owner=t.token.entity;
            o.transform=&world_storage;o.animator_source=&owner_storage;o.attached=attached[t.token.entity];return o;};
        h.read_root_motion=[&]{return api::Observed<bool>{root,{true,0}};};
        h.write_root_motion=[&](bool v){root=v;return api::CallResult{true,0};};
        h.read_pending_motion=[&]{return api::Observed<bool>{pending,{true,0}};};
        h.read_callback=[&](void*){return api::Observed<bool>{callback,{true,0}};};
        h.write_callback=[&](void*,bool v){callback=v;return api::CallResult{true,0};};
        auto lease=std::make_shared<api::OwnedMovementHost>(h,t.token);Check(lease->Prepare());return lease;
    };
    VisualEntityMotion leased(lifecycle,services);
    Check(leased.Bind(first,new_owner,{target}).state==api::EffectControlState::Pending && !root && acquisitions==1);
    Check(leased.Bind(first,new_owner,{target}).state==api::EffectControlState::Pending && acquisitions==1);
    Check(ok(leased.Release(first)) && root && callback && leased.Empty() && motion_roots.empty());
    pending=false;Check(ok(leased.Bind(first,new_owner,{target})) && acquisitions==2);
    Check(ok(leased.Bind(first,new_owner,{target})) && acquisitions==2);
    attached[target.token.entity]=false;lists[first.parameters].clear();deny_reads=true;
    leased.RequestEnded(first);Check(leased.Empty() && root && callback && motion_roots.empty());deny_reads=false;
    pending=true;Check(leased.Bind(first,new_owner,{target}).state==api::EffectControlState::Pending);
    const auto pending_retirement=lifecycle.DisposalStarting(target.token);
    Check(pending_retirement.invoked==1 && pending_retirement.failed==0 && leased.Empty() && root && callback && motion_roots.empty());
    lifecycle.Forget(new_owner.token);lifecycle.Forget(target.token);

    const auto program=ParseVisualProgram(R"({"format":"hksr.visual-program","id":"motion","source":"config","scope":"scene",
      "effects":{},"actions":{"leave":{"state":"Leave","enter":[
        {"op":"follow","effect":"ground","targets":["Target"],"source":"bind"},
        {"op":"follow","effect":"ground","targets":[],"source":"clear"}]}}})");
    Check(std::get<VisualFollow>(program.actions.at("leave").enter.at(0).operation).targets==std::vector<std::string>{"Target"});
    Check(std::get<VisualFollow>(program.actions.at("leave").enter.at(1).operation).targets.empty());
}
