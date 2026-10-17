#include <hksr/framework/presentation/VisualProgramRuntime.hpp>
#include <hksr/presentation/PresentationResources.hpp>
#include <hksr/model/ModelServices.hpp>
#include <hksr/entity/LifecycleHooks.hpp>
#include <hksr/entity/EntityAccess.hpp>
#include <stdexcept>

namespace hksr::framework {
namespace {
bool Alive(api::EntityOrigin origin) {
    return origin.token.entity && origin.token.generation && origin.world &&
        !api::TrackedEntities().IsDisposed(origin.token) &&
        api::TrackedEntities().Current(origin.token.entity).generation==origin.token.generation;
}
}
struct VisualProgramRuntime::Scene {
    api::EntityOrigin owner;
    VisualProgramInput input;
    std::vector<VisualEventRoutes::RuleId> rules;
    std::shared_ptr<VisualProgramPlayer> player;
    std::unique_ptr<api::ManagedRoots> roots;
    api::ManagedRootId receiver=0;
    std::vector<api::ManagedRootId> releasing;
    api::ManagedRootId Keep(void* object) {
        const auto id=roots->Keep(object);
        if(!roots->Status(id).ready){releasing.push_back(id);throw std::runtime_error("visual input retention failed");}
        return id;
    }
    void ReleasePending() {
        for(auto it=releasing.begin();it!=releasing.end();) {
            if(roots->Release(*it).complete){roots->Forget(*it);it=releasing.erase(it);}else ++it;
        }
    }
};
VisualProgramRuntime::VisualProgramRuntime(api::PresentationContext& context,api::ManagedObjects& objects,VisualEventRoutes::Trace trace)
    :api_(context),objects_(objects),routes_(
        [this](const auto& source){return api_.ReadEventDefinitions(source);},
        [this](const auto& source,const auto& task){return sequences_->Track(source,task);},std::move(trace),
        [this](const auto& trigger){return api_.ReadEffectTriggerPath(trigger);}) {
    sequences_=api_.CreateSequenceObserver([this](const auto& value){routes_.Observe(value);});
    constructions_=api_.CreateEventConstructionObserver(
        [this](const auto& source){
            if(!routes_.Interested(source.origin) || !source.file)return;
            const auto file=api_.Resources().Components().ReadText(source.file);
            if(!file.read.returned)throw std::runtime_error("visual event source path unread: "+file.error);
            routes_.Before(source,file.utf8);
        },[this](const auto& source,const auto& task){routes_.Built(source,task);},
        [this](const auto& source,bool){routes_.After(source);});
    effects_=api_.CreateEffectObserver([this](const auto& value){routes_.Submitted(value);},{});
}
VisualProgramRuntime::~VisualProgramRuntime()=default;
void VisualProgramRuntime::Activate() {
    if(closing_)return;
    sequences_->ActivateOnCurrentThread();constructions_->ActivateOnCurrentThread();effects_->ActivateOnCurrentThread();
}
std::shared_ptr<VisualProgramPlayer> VisualProgramRuntime::Create(api::EntityOrigin owner,const VisualProgramInput& input,
    const std::shared_ptr<const PresentationProfile>& profile) {
    if(closing_ || !Alive(owner) || !input.program)throw std::invalid_argument("visual program requires a live creation and program");
    auto scene=std::make_shared<Scene>();scene->owner=owner;scene->input=input;scene->roots=api_.CreateRoots();
    scenes_.push_back(scene); // own partial declaration/setup before any later operation can fail
    VisualProgramServices services;services.effects=api_.EffectServices();services.choices=input.choices;
    std::weak_ptr<Scene> weak=scene;
    auto current=[weak]{auto value=weak.lock();if(!value)throw std::logic_error("visual scene ended");return value;};
    services.create=[this,current](auto& context,const auto& key,const auto& value){return CreateEffect(*current(),context,key,value);};
    services.attach=[this,current](auto context){Attach(*current(),std::move(context));};
    services.detach=[this,current]{Detach(*current());};
    services.remove_in_flight=[this](auto context,const auto& key,auto options){return routes_.RemoveInFlight(std::move(context),key,options);};
    ConnectVisualTimelines(services,std::make_shared<VisualTimelines>([this,owner]{return api_.CreateTimeline(owner.token,owner.world);}));
    scene->player=std::make_shared<VisualProgramPlayer>(input.program,std::move(services));
    try {
        if(profile && profile->source==input.program->source)for(const auto& rule:profile->native_events)
            if(rule.scope==input.program->scope)scene->rules.push_back(routes_.Declare(owner,rule));
    }catch(...) {scene->player->End();throw;}
    return scene->player;
}
void VisualProgramRuntime::Attach(Scene& scene,std::shared_ptr<VisualEffectContext> context) {
    Detach(scene);
    if(scene.rules.empty())return;
    auto* receiver=api_.ReadEventReceiver(scene.owner.token,scene.owner.world);
    if(!receiver)throw std::runtime_error("current model event receiver unavailable");
    scene.receiver=scene.Keep(receiver);
    try {for(auto rule:scene.rules)routes_.Attach(rule,receiver,context);}
    catch(...){Detach(scene);throw;}
}
void VisualProgramRuntime::Detach(Scene& scene) {
    for(auto rule:scene.rules)routes_.Detach(rule);
    if(scene.receiver){scene.releasing.push_back(scene.receiver);scene.receiver=0;}
}
VisualEffectId VisualProgramRuntime::CreateEffect(Scene& scene,VisualEffectContext& context,const std::string& key,const VisualEffectInput& input) {
    api::EntityOrigin target=scene.owner;
    if(input.target!="Caster") {
        const auto found=scene.input.targets.find(input.target);
        if(found==scene.input.targets.end())throw std::runtime_error("explicit visual target missing: "+input.target);
        target=found->second;
    }
    if(!Alive(scene.owner) || !Alive(target) || target.world!=scene.owner.world)
        throw std::runtime_error("visual target belongs to an ended generation or another world");
    const auto entities=api_.Models();
    const auto state=entities->Entities().Read(target.token.entity);
    if(!Alive(scene.owner) || !Alive(target) || !state.owner_world.read.returned || state.owner_world.value!=target.world ||
        !state.loaded.read.returned || !state.loaded.value)throw std::runtime_error("visual target not loaded in its owner world");
    const auto path=objects_.NewStringUtf8(input.path.c_str());
    if(!path.call.returned || !path.object)throw std::runtime_error("visual effect path allocation failed");
    const auto pin=scene.Keep(path.object);scene.releasing.push_back(pin);
    if(!Alive(scene.owner) || !Alive(target))throw std::runtime_error("visual owner ended while preparing effect");
    return context.StartEntity(key,scene.owner.token.entity,target.token.entity,path.object,input.options);
}
void VisualProgramRuntime::Collect() {
    sequences_->Collect();routes_.Collect();
    for(auto it=scenes_.begin();it!=scenes_.end();) {
        auto& scene=**it;
        if(scene.player && (closing_ || !Alive(scene.owner)) && !scene.player->Status().closing)scene.player->End();
        if(scene.player && scene.player->Status().closing)scene.player->Service();
        scene.ReleasePending();
        if(!scene.player || scene.player->Drained()) {
            Detach(scene);scene.ReleasePending();
            if(scene.releasing.empty()) {
                for(auto rule:scene.rules)routes_.Unbind(rule);
                it=scenes_.erase(it);continue;
            }
        }
        ++it;
    }
}
bool VisualProgramRuntime::Close() {
    closing_=true;Collect();if(!scenes_.empty())return false;
    constructions_->Stop();effects_->Stop();
    if(!sequences_->Close())return false;
    routes_.Clear();return true;
}
}
