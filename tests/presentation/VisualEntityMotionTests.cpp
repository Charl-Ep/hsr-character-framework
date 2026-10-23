#include <hksr/framework/presentation/VisualEntityMotion.hpp>
#include <hksr/framework/presentation/VisualProgram.hpp>
#include <map>
#include <stdexcept>

namespace {
void CheckAt(bool ok,int line){if(!ok)throw std::runtime_error("visual entity motion test failed at "+std::to_string(line));}
#define Check(x) CheckAt((x),__LINE__)
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

    const auto program=ParseVisualProgram(R"({"format":"hksr.visual-program","id":"motion","source":"config","scope":"scene",
      "effects":{},"actions":{"leave":{"state":"Leave","enter":[
        {"op":"follow","effect":"ground","targets":["Target"],"source":"bind"},
        {"op":"follow","effect":"ground","targets":[],"source":"clear"}]}}})");
    Check(std::get<VisualFollow>(program.actions.at("leave").enter.at(0).operation).targets==std::vector<std::string>{"Target"});
    Check(std::get<VisualFollow>(program.actions.at("leave").enter.at(1).operation).targets.empty());
}
