#include <hksr/framework/presentation/VisualEntityMotion.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace hksr::framework {
namespace {
using Key=std::pair<std::uintptr_t,std::uint64_t>;
Key KeyOf(api::EffectReference ref){return {reinterpret_cast<std::uintptr_t>(ref.parameters),ref.id};}
Key KeyOf(api::EntityToken ref){return {reinterpret_cast<std::uintptr_t>(ref.entity),ref.generation};}
api::EffectControlResult Result(api::EffectControlState state,const std::string& text={}){return {state,{},true,text};}
bool Applied(const api::EffectControlResult& r){return r.state==api::EffectControlState::Applied;}
}
struct VisualEntityMotion::State:std::enable_shared_from_this<State> {
    struct Binding {
        api::EffectReference effect;api::EntityOrigin owner;std::vector<api::EntityOrigin> targets;
        std::vector<api::EntityDisposalSubscription> subscriptions;
        bool changing=false;std::string last_error;
    };
    api::EntityLifecycle& lifecycle;VisualEntityMotionServices services;
    std::map<Key,std::shared_ptr<Binding>> bindings;
    std::map<Key,Key> occupied;
    State(api::EntityLifecycle& l,VisualEntityMotionServices s):lifecycle(l),services(std::move(s)){}
    bool Live(api::EntityOrigin origin,bool retiring=false) const {
        return origin.token.entity && origin.token.generation && origin.world &&
            lifecycle.Current(origin.token.entity).generation==origin.token.generation &&
            !lifecycle.IsDisposed(origin.token) && (retiring || !lifecycle.IsRetiring(origin.token));
    }
    static std::vector<void*> Targets(const Binding& binding) {
        std::vector<void*> out;for(const auto& t:binding.targets)out.push_back(t.token.entity);return out;
    }
    void Log(const Binding& b,const char* event,const std::string& detail={}) noexcept {
        try {
            if(!services.trace)return;
            std::ostringstream out;out<<"visual-motion event="<<event<<" request="<<b.effect.parameters<<" request_id="<<b.effect.id
                <<" owner="<<b.owner.token.entity<<" generation="<<b.owner.token.generation;
            for(const auto& t:b.targets)out<<" target="<<t.token.entity<<":"<<t.token.generation;
            if(!detail.empty())out<<" detail=["<<detail<<"]";
            services.trace(out.str());
        }catch(...){}
    }
    void Report(Binding& b,const std::string& detail) noexcept {
        try {if(detail!=b.last_error){b.last_error=detail;Log(b,"failed",detail);}}catch(...){}
    }
    void Forget(Key key) {
        const auto found=bindings.find(key);if(found==bindings.end())return;
        const auto binding=found->second;
        for(auto id:binding->subscriptions)lifecycle.CancelBeforeDispose(id);
        for(const auto& t:binding->targets) {
            const auto taken=occupied.find(KeyOf(t.token));
            if(taken!=occupied.end() && taken->second==key)occupied.erase(taken);
        }
        bindings.erase(found);
    }
    api::EffectControlResult Release(api::EffectReference ref) {
        const auto key=KeyOf(ref);const auto found=bindings.find(key);
        if(found==bindings.end())return Result(api::EffectControlState::Applied);
        const auto binding=found->second;
        if(binding->changing) {
            const std::string detail="effect motion cleanup reentered an in-flight change";
            Report(*binding,detail);return Result(api::EffectControlState::Failed,detail);
        }
        binding->changing=true;struct Exit{bool& flag;~Exit(){flag=false;}} exit{binding->changing};
        try {
            const auto observed=services.read(ref);
            if(observed.result.state==api::EffectControlState::Ended){Log(*binding,"ended");Forget(key);return observed.result;}
            if(!Applied(observed.result)){Report(*binding,observed.result.detail);return observed.result;}
            if(observed.targets.empty()){Log(*binding,"already-cleared");Forget(key);return Result(api::EffectControlState::Applied);}
            const auto allowed=Targets(*binding);
            for(auto* target:observed.targets)if(std::find(allowed.begin(),allowed.end(),target)==allowed.end())
                throw std::runtime_error("effect target list replaced by another owner; not clearing it");
            // Retiring is allowed INSIDE the pre-disposal callback. Once the
            // original has finished, do not invoke Clear on stale targets.
            for(const auto& t:binding->targets)if(!Live(t,true))
                throw std::runtime_error("target ended before effect motion was detached");
            const auto cleared=services.set(ref,observed.targets,{},[this,binding] {
                for(const auto& t:binding->targets)if(!Live(t,true))return false;
                return true;
            });
            if(Applied(cleared.result) && cleared.targets.empty()) {Log(*binding,"cleared");Forget(key);return cleared.result;}
            if(cleared.result.state==api::EffectControlState::Ended){Forget(key);return cleared.result;}
            Report(*binding,cleared.result.detail);return cleared.result;
        }catch(const std::exception& e){Report(*binding,e.what());return Result(api::EffectControlState::Failed,e.what());}
    }
    void Watch(const std::shared_ptr<Binding>& binding,api::EntityToken token) {
        const auto ref=binding->effect;std::weak_ptr<State> weak=shared_from_this();
        binding->subscriptions.push_back(lifecycle.BeforeDispose(token,[weak,ref](auto disposing){
            if(auto state=weak.lock()) {
                const auto found=state->bindings.find(KeyOf(ref));
                if(found!=state->bindings.end())state->Log(*found->second,"before-dispose",std::to_string(disposing.generation));
                const auto result=state->Release(ref);
                if(!Applied(result) && result.state!=api::EffectControlState::Ended)
                    throw std::runtime_error("effect motion pre-disposal cleanup failed: "+result.detail);
            }
        }));
    }
};
VisualEntityMotion::VisualEntityMotion(api::EntityLifecycle& lifecycle,VisualEntityMotionServices services)
    :state_(std::make_shared<State>(lifecycle,std::move(services))) {
    if(!state_->services.read || !state_->services.set || !state_->services.target || !state_->services.eligible)
        throw std::invalid_argument("effect entity motion services incomplete");
}
VisualEntityMotion::~VisualEntityMotion() {
    for(const auto& [key,binding]:state_->bindings) {
        (void)key;for(auto id:binding->subscriptions)state_->lifecycle.CancelBeforeDispose(id);
        state_->Report(*binding,"owner destroyed before motion binding drained");
    }
}
api::EffectControlResult VisualEntityMotion::Bind(api::EffectReference ref,api::EntityOrigin owner,const std::vector<api::EntityOrigin>& targets) {
    auto& s=*state_;const auto key=KeyOf(ref);
    if(targets.empty())return s.Release(ref);
    if(!ref.parameters || !s.Live(owner))return Result(api::EffectControlState::Failed,"effect motion owner is not live");
    const auto prior=s.bindings.find(key);
    if(prior!=s.bindings.end()) {
        const auto retained=prior->second;const auto& current=*retained;
        bool same=KeyOf(current.owner.token)==KeyOf(owner.token) && current.owner.world==owner.world && current.targets.size()==targets.size();
        for(std::size_t i=0;same && i<targets.size();++i)
            same=KeyOf(current.targets[i].token)==KeyOf(targets[i].token) && current.targets[i].world==targets[i].world;
        if(!same)return Result(api::EffectControlState::Failed,"release previous motion binding before selecting different targets");
        const auto observed=s.services.read(ref);
        if(Applied(observed.result) && observed.targets!=State::Targets(current))
            return Result(api::EffectControlState::Failed,"effect motion binding no longer matches its receipt");
        return observed.result; // unchanged binding never resets its accumulator
    }
    std::shared_ptr<State::Binding> binding;bool registered=false;
    try {
        if(!s.services.eligible(owner))throw std::runtime_error("effect motion owner is outside this client's loaded entities");
        std::set<Key> unique;
        for(const auto& t:targets) {
            if(!s.Live(t) || t.world!=owner.world || !s.services.eligible(t))
                throw std::runtime_error("effect motion target is not a loaded owned entity in this world");
            if(!unique.insert(KeyOf(t.token)).second)throw std::runtime_error("duplicate effect motion target");
            if(s.occupied.count(KeyOf(t.token)))throw std::runtime_error("target motion is controlled by another effect");
            const auto native=s.services.target(t.token.entity);
            if(!Applied(native.result))return native.result;
            if(native.attached)throw std::runtime_error("target already has official attached motion outside this binding");
        }
        const auto observed=s.services.read(ref);
        if(!Applied(observed.result))return observed.result;
        if(!observed.targets.empty())throw std::runtime_error("effect already has targets without this context's ownership");
        binding=std::make_shared<State::Binding>();binding->effect=ref;binding->owner=owner;binding->targets=targets;
        if(!s.bindings.emplace(key,binding).second)throw std::runtime_error("effect motion request acquired during preparation");
        registered=true;
        // Reserve before any native write and retain partial subscription setup.
        for(const auto& t:targets)if(!s.occupied.emplace(KeyOf(t.token),key).second)
            throw std::runtime_error("target motion acquired during preparation");
        binding->subscriptions.reserve(targets.size()+1);s.Watch(binding,owner.token);
        for(const auto& t:targets)if(KeyOf(t.token)!=KeyOf(owner.token))s.Watch(binding,t.token);
        if(!s.Live(owner))throw std::runtime_error("effect motion owner retired during preparation");
        for(const auto& t:targets)if(!s.Live(t))throw std::runtime_error("effect motion target retired during preparation");
        binding->changing=true;
        api::EffectEntityFollowObservation result;
        auto valid=[&s,binding] {
            if(!s.Live(binding->owner))return false;
            for(const auto& t:binding->targets)if(!s.Live(t))return false;
            return true;
        };
        try{result=s.services.set(ref,{},State::Targets(*binding),valid);}catch(...){binding->changing=false;throw;}
        binding->changing=false;
        if(Applied(result.result) && valid()){s.Log(*binding,"bound");return result.result;}
        if(Applied(result.result))result.result=Result(api::EffectControlState::Failed,"effect motion target retired during the native write");
        // A failed native call may have written some targets. The exact
        // readback is used to clear only those this attempt reserved.
        s.Release(ref);return result.result;
    }catch(const std::exception& e) {
        if(registered){binding->changing=false;s.Release(ref);}
        return Result(api::EffectControlState::Failed,e.what());
    }
}
api::EffectControlResult VisualEntityMotion::Release(api::EffectReference ref){return state_->Release(ref);}
void VisualEntityMotion::RequestEnded(api::EffectReference ref) {
    const auto it=state_->bindings.find(KeyOf(ref));if(it==state_->bindings.end())return;
    const auto binding=it->second;
    for(const auto& t:binding->targets)if(state_->Live(t)) {
        const auto observed=state_->services.target(t.token.entity);
        if(!Applied(observed.result) || observed.attached)
            state_->Report(*binding,"effect ended without a confirmed cleared target motion flag");
    }
    state_->Log(*binding,"request-ended");state_->Forget(KeyOf(ref));
}
bool VisualEntityMotion::Contains(api::EffectReference ref) const{return state_->bindings.count(KeyOf(ref))!=0;}
bool VisualEntityMotion::Empty() const{return state_->bindings.empty();}
}
