#include <hksr/framework/presentation/VisualProgramRuntime.hpp>
#include <hksr/presentation/PresentationResources.hpp>
#include <hksr/model/ModelServices.hpp>
#include <hksr/entity/LifecycleHooks.hpp>
#include <hksr/entity/EntityAccess.hpp>
#include <hksr/entity/EntityOwner.hpp>
#include <hksr/world/WorldPlacement.hpp>
#include <stdexcept>
#include <sstream>
#include "VisualProgramCallbacks.hpp"

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
    struct Inputs {
        std::weak_ptr<VisualEffectContext> context;
        std::map<std::string,std::uint64_t> targets;
        VisualSpatialAnchors anchors;
        std::map<std::string,api::EntityOrigin> resolved;
        bool restored=false;
        std::optional<api::Position3> movement_origin;
    };
    std::map<VisualEffectContext*,Inputs> context_inputs;
    struct Channel {std::vector<VisualEventRoutes::RuleId> rules;api::ManagedRootId receiver=0;};
    std::map<std::string,Channel> channels;
    std::shared_ptr<VisualProgramPlayer> player;
    std::unique_ptr<VisualPartners> partners;
    std::unique_ptr<api::ManagedRoots> roots;
    std::vector<api::ManagedRootId> releasing;
    std::string last_trace;
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
        [this](const auto& source,const auto& task){return sequences_->Track(source,task);},trace,
        [this](const auto& trigger){return api_.ReadEffectTriggerPath(trigger);}),trace_(std::move(trace)) {
    auto callbacks=detail::ConnectVisualProgramCallbacks(routes_,[this](void* text){
        const auto file=api_.Resources().Components().ReadText(text);
        if(!file.read.returned)throw std::runtime_error("visual event source path unread: "+file.error);
        return file.utf8;
    });
    sequences_=api_.CreateSequenceObserver(std::move(callbacks.sequence));
    constructions_=api_.CreateEventConstructionObserver(std::move(callbacks.before_construction),
        std::move(callbacks.tasks),std::move(callbacks.after_construction));
    effects_=api_.CreateEffectObserver(std::move(callbacks.before_effect),std::move(callbacks.after_effect));
}
VisualProgramRuntime::~VisualProgramRuntime()=default;
void VisualProgramRuntime::Activate() {
    if(closing_)return;
    sequences_->ActivateOnCurrentThread();constructions_->ActivateOnCurrentThread();effects_->ActivateOnCurrentThread();
}
void VisualProgramRuntime::BeginFrame(const api::WorldFrame& frame) {
    frame_=frame;
    const auto scenes=scenes_;
    for(const auto& scene:scenes)if(scene->partners)scene->partners->BeginFrame(frame);
}
VisualPartners& VisualProgramRuntime::Partners(const std::shared_ptr<Scene>& scene) {
    if(!scene->partners) {
        std::weak_ptr<Scene> weak=scene;
        VisualPartnerServices services;
        services.create=api_.Partners();
        services.controls=[this,owner=scene->owner](api::EntityToken partner){return api_.PartnerControls(partner,owner.token,owner.world);};
        services.target=[this,weak](auto& context,const std::string& name) {
            const auto current=weak.lock();if(!current)throw std::runtime_error("partner parent scene ended");
            return ResolveTarget(*current,context,name);
        };
        services.trace=trace_;
        services.animations=api_.Animations();services.objects=&objects_;
        scene->partners=std::make_unique<VisualPartners>(scene->owner,scene->input.program->partners,std::move(services));
        if(frame_)scene->partners->BeginFrame(*frame_);
    }
    return *scene->partners;
}
std::shared_ptr<VisualProgramPlayer> VisualProgramRuntime::Create(api::EntityOrigin owner,const VisualProgramInput& input,
    const std::shared_ptr<const PresentationProfile>& profile) {
    if(closing_ || !Alive(owner) || !input.program)throw std::invalid_argument("visual program requires a live creation and program");
    auto scene=std::make_shared<Scene>();scene->owner=owner;scene->input=input;scene->roots=api_.CreateRoots();
    scenes_.push_back(scene); // own partial declaration/setup before any later operation can fail
    VisualProgramServices services;services.effects=api_.EffectServices();services.choices=input.choices;
    std::weak_ptr<Scene> weak=scene;
    auto current=[weak]{auto value=weak.lock();if(!value)throw std::logic_error("visual scene ended");return value;};
    services.read_phase=[this,current](const std::string& name) {
        const auto scene=current();const auto& program=scene->input.program;
        auto& cache=phase_cache_[program.get()];cache.program=program;
        if(const auto found=cache.values.find(name);found!=cache.values.end())return found->second;
        if(!phase_reader_)throw std::runtime_error("phase data reader unavailable");
        auto value=std::make_shared<const api::PhaseVisualConfigSources>(phase_reader_(program->source,program->phases.at(name)));
        if(!value->visuals.context_available) {
            std::string why="phase source unread: "+name;
            for(const auto& issue:value->issues)why+="; "+issue.error;
            throw std::runtime_error(why);
        }
        if(value->visuals.character.phase_present && !value->standby.available)
            throw std::runtime_error("phase standby unread: "+name+"; "+value->standby.error);
        if(trace_)for(const auto& issue:value->issues)trace_("visual phase "+name+": "+issue.operation+": "+issue.error);
        cache.values.emplace(name,value);return value;
    };
    services.select_inputs=[current] {
        auto value=current();auto targets=value->input.target_characters;
        auto anchors=value->input.anchors;
        auto choices=value->input.choices;
        if(value->input.read_parameters) {
            auto parameters=value->input.read_parameters(value->input.character);
            for(auto& [key,id]:parameters.targets)targets[key]=id;
            for(auto& [key,choice]:parameters.choices)choices[key]=std::move(choice);
            for(const auto& [key,pose]:parameters.anchors)anchors[key]=pose;
        }
        return VisualProgramParameters{std::move(targets),std::move(choices),std::move(anchors)};
    };
    services.available_targets=[current](const VisualProgramParameters& inputs) {
        const auto scene=current();std::map<std::string,api::EntityOrigin> targets=scene->input.targets;
        for(const auto& [name,id]:inputs.targets) {
            if(targets.count(name))continue; // same precedence as ResolveTarget
            if(scene->input.resolve_character)targets[name]=scene->input.resolve_character(id);
        }
        targets["Caster"]=scene->owner;std::vector<std::string> result;
        for(const auto& [name,target]:targets)
            if(Alive(target) && target.world==scene->owner.world)result.push_back(name);
        return result;
    };
    services.create=[this,current](auto& context,const auto& key,const auto& value){return CreateEffect(*current(),context,key,value);};
    services.partner=[this,current](auto context,const auto& op){return Partners(current()).Execute(std::move(context),op);};
    services.end_partners=[current](auto context){const auto scene=current();if(scene->partners)scene->partners->End(context);};
    services.partners_drained=[current]{const auto scene=current();return !scene->partners || scene->partners->Drained();};
    services.partner_scope_drained=[current](auto context){const auto scene=current();return !scene->partners || scene->partners->Drained(context);};
    services.partner_animation=[this,current](const auto& key){return Partners(current()).Animation(key);};
    services.prepare_inputs=[current](auto context,const VisualProgramParameters& inputs,bool restored) {
        auto scene=current();
        scene->context_inputs[context.get()]={context,inputs.targets,inputs.anchors,{},restored};
    };
    services.capture_inputs=[current](const auto& context) {
        const auto scene=current();VisualProgramParameters result;
        const auto found=scene->context_inputs.find(const_cast<VisualEffectContext*>(&context));
        if(found==scene->context_inputs.end())throw std::runtime_error("visual context inputs unavailable");
        const auto& inputs=found->second;
        for(const auto& [name,target]:inputs.resolved)
            if(name!="Caster" && !inputs.restored && scene->input.targets.count(name))
                throw std::runtime_error("visual target has no persistent character identity: "+name);
        result.targets=inputs.targets;result.anchors=inputs.anchors;return result;
    };
    services.restore_inputs_ready=[this,current](const VisualProgramParameters& inputs) {
        const auto scene=current();
        for(const auto& [name,id]:inputs.targets) {
            if(!scene->input.resolve_character)return VisualStepResult{VisualStepState::Failed,"visual target resolver unavailable"};
            if(scene->input.restore_target) {
                const auto readiness=scene->input.restore_target(id);
                if(readiness.state!=VisualStepState::Done)return readiness;
            }
            const auto origin=scene->input.resolve_character(id);
            if(!Alive(origin))return VisualStepResult{VisualStepState::Pending,"waiting for restored target "+name+" character="+std::to_string(id)};
            if(origin.world!=scene->owner.world)return VisualStepResult{VisualStepState::Failed,"restored visual target belongs to another world: "+name};
            const auto state=api_.Models()->Entities().Read(origin.token.entity);
            if(!state.loaded.read.returned || !state.loaded.value)
                return VisualStepResult{VisualStepState::Pending,"waiting for target model: "+name};
        }
        return VisualStepResult{VisualStepState::Done,{}};
    };
    services.context_started=[this,current](const std::string& name,auto context) {
        Attach(*current(),context,name);
    };
    services.context_stopped=[this,current](const std::string& name){Detach(*current(),name);};
    services.effect_state=[this,current](auto& context,auto id,const VisualEffectState& op) {
        const auto scene=current();
        if(!Alive(scene->owner))return VisualStepResult{VisualStepState::Failed,"visual owner ended"};
        const auto result=context.SetAnimatorState(id,op.state,op.transition,op.restart);
        return VisualStepResult{result.state==api::EffectControlState::Applied?VisualStepState::Done:
            result.state==api::EffectControlState::NotLoaded?VisualStepState::Pending:VisualStepState::Failed,result.detail};
    };
    services.follow=[this,current](auto& context,auto id,const VisualFollow& op) {
        const auto scene=current();std::vector<api::EntityOrigin> targets;
        for(const auto& name:op.targets)targets.push_back(ResolveTarget(*scene,context,name));
        context.UseEntityMotion(Motion(),scene->owner);
        const auto result=context.BindTargets(id,targets);
        return VisualStepResult{result.state==api::EffectControlState::Applied?VisualStepState::Done:
            (result.state==api::EffectControlState::NotLoaded || result.state==api::EffectControlState::Pending)?VisualStepState::Pending:VisualStepState::Failed,result.detail};
    };
    services.attach=[this,current](auto context){Attach(*current(),std::move(context));};
    services.hit=[this,current](VisualEffectContext& context,const std::string& target_name,const HitInput& command)->std::optional<HitDelivery> {
        const auto scene=current();
        if(!Alive(scene->owner))throw std::runtime_error("hit source scene ended");
        const auto target=ResolveTarget(*scene,context,target_name);
        if(!scene->input.resolve_hit_receiver)throw std::runtime_error("owned hit receiver resolver unavailable");
        const auto receiver=scene->input.resolve_hit_receiver(target);
        if(!receiver)return {};
        if(!Alive(scene->owner) || !Alive(target) || receiver->Generation()!=target.token.generation)
            throw std::runtime_error("hit receiver generation changed");
        return receiver->Post(command);
    };
    services.face=[this,current](VisualEffectContext& context,const VisualFace& command) {
        const auto scene=current();
        if(closing_ || !Alive(scene->owner))throw std::runtime_error("facing scene ended");
        if(!api_.ReadAnimator(scene->owner.token))return VisualStepResult{VisualStepState::Pending,"waiting for owned model"};
        const auto target=ResolveTarget(*scene,context,command.target);
        const auto pose=api_.ReadEntityPlacement(target.token,target.world);
        if(!pose.ready)throw std::runtime_error("facing target unreadable: "+command.target+"; "+pose.error);
        const auto call=api_.FaceEntity(scene->owner.token,scene->owner.world,pose.position,
            command.ratio,command.angle_offset,command.duration);
        return VisualStepResult{call.returned?VisualStepState::Done:VisualStepState::Failed,
            call.returned?"":"owned facing submission failed"};
    };
    services.movement=[this,current](VisualEffectContext& context,const VisualMovement& command)->std::optional<MovementRequestEndpoint> {
        const auto scene=current();
        if(closing_ || !Alive(scene->owner))throw std::runtime_error("movement scene ended");
        if(!api_.ReadAnimator(scene->owner.token))return {};
        const auto from=api_.ReadEntityPlacement(scene->owner.token,scene->owner.world);
        if(!from.ready)throw std::runtime_error("movement origin unreadable: "+from.error);
        auto found=scene->context_inputs.find(&context);
        if(found==scene->context_inputs.end())throw std::logic_error("movement scope inputs not captured");
        auto& inputs=found->second;
        if(!inputs.movement_origin)inputs.movement_origin=from.position;
        const auto plan=ResolveVisualMovement(command,from.position,*inputs.movement_origin,[&](auto kind,const std::string& name) {
            if(kind==MovementDestination::Anchor) {
                const auto anchor=inputs.anchors.find(name);
                if(anchor==inputs.anchors.end())throw std::runtime_error("explicit movement anchor missing: "+name);
                return anchor->second.position;
            }
            const auto target=ResolveTarget(*scene,context,name);
            const auto position=api_.ReadEntityPlacement(target.token,target.world);
            if(!position.ready)throw std::runtime_error("movement target unreadable: "+name+"; "+position.error);
            return position.position;
        });
        return BindMovementRequest(scene->owner.token.generation,api_.StartMovement(scene->owner.token,scene->owner.world,command.state,plan));
    };
    services.detach=[this,current]{Detach(*current());};
    services.remove_in_flight=[this](auto context,const auto& key,auto options){return routes_.RemoveInFlight(std::move(context),key,options);};
    const auto trace=trace_;
    ConnectVisualTimelines(services,std::make_shared<VisualTimelines>([this,owner]{return api_.CreateTimeline(owner.token,owner.world);},
        [trace,owner](const auto& text){if(trace){std::ostringstream out;out<<"visual owner="<<owner.token.entity<<" generation="<<owner.token.generation<<" "<<text;trace(out.str());}}));
    scene->player=std::make_shared<VisualProgramPlayer>(input.program,std::move(services));
    try {
        if(profile && profile->source==input.program->source)for(const auto& rule:profile->native_events) {
            if(rule.scope==input.program->scope)scene->channels[""].rules.push_back(routes_.Declare(owner,rule));
            else for(const auto& [name,context]:input.program->contexts)if(!context.event_scope.empty() && rule.scope==context.event_scope)
                scene->channels[name].rules.push_back(routes_.Declare(owner,rule));
        }
    }catch(...) {scene->player->End();throw;}
    return scene->player;
}
void VisualProgramRuntime::Attach(Scene& scene,std::shared_ptr<VisualEffectContext> context,const std::string& name) {
    Detach(scene,name);
    auto& channel=scene.channels[name];
    if(channel.rules.empty())return;
    auto* receiver=api_.ReadEventReceiver(scene.owner.token,scene.owner.world);
    if(!receiver)throw std::runtime_error("current model event receiver unavailable");
    channel.receiver=scene.Keep(receiver);
    try {for(auto rule:channel.rules)routes_.Attach(rule,receiver,context);}
    catch(...){Detach(scene,name);throw;}
}
void VisualProgramRuntime::Detach(Scene& scene,const std::string& name) {
    const auto found=scene.channels.find(name);if(found==scene.channels.end())return;
    auto& channel=found->second;
    for(auto rule:channel.rules)routes_.Detach(rule);
    if(channel.receiver){scene.releasing.push_back(channel.receiver);channel.receiver=0;}
}
api::EntityOrigin VisualProgramRuntime::ResolveTarget(Scene& scene,VisualEffectContext& context,const std::string& name) {
    if(name.compare(0,8,"partner:")==0) {
        if(!scene.partners)throw std::runtime_error("partner target has not been created: "+name);
        return scene.partners->Resolve(name.substr(8));
    }
    const auto snapshot=scene.context_inputs.find(&context);
    auto* inputs=snapshot==scene.context_inputs.end()?nullptr:&snapshot->second;
    api::EntityOrigin target=scene.owner;
    if(name!="Caster") {
        const auto resolved=inputs?inputs->resolved.find(name):std::map<std::string,api::EntityOrigin>::iterator{};
        const auto found=scene.input.targets.find(name);
        if(inputs && resolved!=inputs->resolved.end())target=resolved->second;
        else if(found!=scene.input.targets.end() && (!inputs || !inputs->restored))target=found->second;
        else {
            const auto& targets=inputs?inputs->targets:scene.input.target_characters;
            const auto character=targets.find(name);
            if(character==targets.end() || !scene.input.resolve_character)
                throw std::runtime_error("explicit visual target missing: "+name);
            target=scene.input.resolve_character(character->second);
        }
    }
    if(!Alive(scene.owner) || !Alive(target) || target.world!=scene.owner.world)
        throw std::runtime_error("visual target belongs to an ended generation or another world");
    const auto entities=api_.Models();
    const auto state=entities->Entities().Read(target.token.entity);
    if(!Alive(scene.owner) || !Alive(target) || !state.owner_world.read.returned || state.owner_world.value!=target.world ||
        !state.loaded.read.returned || !state.loaded.value)throw std::runtime_error("visual target not loaded in its owner world");
    if(inputs)inputs->resolved.emplace(name,target);
    return target;
}
std::shared_ptr<VisualEntityMotion> VisualProgramRuntime::Motion() {
    if(motion_)return motion_;
    // Resolve only when a program actually uses entity-follow, not at startup
    // for every character or on each frame.
    if(!entity_follow_)entity_follow_=api_.CreateEffectEntityFollow();
    VisualEntityMotionServices services;
    services.read=[this](auto ref){return entity_follow_->Read(ref);};
    services.set=[this](auto ref,const auto& expected,const auto& desired,auto current){return entity_follow_->Set(ref,expected,desired,std::move(current));};
    services.target=[this](void* entity){return entity_follow_->ReadTarget(entity);};
    services.acquire=[this](api::EntityOrigin origin){return api_.AcquireMovementHost(origin.token,origin.world);};
    services.eligible=[this](api::EntityOrigin origin) {
        if(!Alive(origin) || api_.Owner().OwnedToken(origin.token.entity).generation!=origin.token.generation)return false;
        const auto state=api_.Models()->Entities().Read(origin.token.entity);
        return Alive(origin) && state.owner_world.read.returned && state.owner_world.value==origin.world &&
            state.loaded.read.returned && state.loaded.value;
    };
    services.trace=trace_;
    motion_=std::make_shared<VisualEntityMotion>(api::TrackedEntities(),std::move(services));return motion_;
}
VisualEffectId VisualProgramRuntime::CreateEffect(Scene& scene,VisualEffectContext& context,const std::string& key,const VisualEffectInput& input) {
    api::EntityOrigin target;std::optional<api::SpatialEffectOptions> spatial;
    if(!input.anchor.empty()) {
        if(!input.target.empty())throw std::invalid_argument("effect has both entity and spatial targets");
        const auto snapshot=scene.context_inputs.find(&context);
        const auto& anchors=snapshot==scene.context_inputs.end()?scene.input.anchors:snapshot->second.anchors;
        const auto found=anchors.find(input.anchor);
        if(found==anchors.end())throw std::runtime_error("explicit visual anchor missing: "+input.anchor);
        const auto& pose=found->second;
        spatial=api::SpatialEffectOptions{pose.position,pose.rotation,input.options.unique_name,input.options.scale,
            input.options.fade_in,input.options.simulate_immediately,input.options.resident};
    }else target=ResolveTarget(scene,context,input.target);
    const auto path=objects_.NewStringUtf8(input.path.c_str());
    if(!path.call.returned || !path.object)throw std::runtime_error("visual effect path allocation failed");
    const auto pin=scene.Keep(path.object);scene.releasing.push_back(pin);
    if(!Alive(scene.owner) || (!spatial && !Alive(target)))throw std::runtime_error("visual owner ended while preparing effect");
    const auto id=spatial?context.StartSpatial(key,scene.owner.token.entity,path.object,*spatial):
        context.StartEntity(key,scene.owner.token.entity,target.token.entity,path.object,input.options);
    if(trace_) {
        std::ostringstream out;out<<"visual-submit owner="<<scene.owner.token.entity<<" generation="<<scene.owner.token.generation
            <<" target="<<target.token.entity<<" target_generation="<<target.token.generation
            <<" key="<<key<<" receipt="<<id;
        if(spatial)out<<" anchor="<<input.anchor<<" position="<<spatial->position.x<<","<<spatial->position.y<<","<<spatial->position.z
            <<" rotation="<<spatial->rotation.x<<","<<spatial->rotation.y<<","<<spatial->rotation.z<<","<<spatial->rotation.w;
        trace_(out.str());
    }
    return id;
}
void VisualProgramRuntime::Collect() {
    if(motion_)motion_->Collect();
    sequences_->Collect();routes_.Collect();
    std::map<std::uint64_t,Scene*> readers;
    for(const auto& scene:scenes_)if(scene->input.take_signals)readers[scene->input.character]=scene.get();
    for(auto it=scenes_.begin();it!=scenes_.end();) {
        auto& scene=**it;
        // A retiring scene must not consume a newer creation's mailbox.
        if(scene.player && scene.input.take_signals && readers.at(scene.input.character)==&scene)
        for(const auto& signal:scene.input.take_signals(scene.input.character)) {
            std::uint64_t receipt=0;std::string error;
            try {
                if(!Alive(scene.owner) || signal.generation!=scene.owner.token.generation)
                    throw std::runtime_error("visual signal targets an ended creation");
                receipt=scene.player->Signal(signal.intent,signal.name);
                if(!receipt)error="visual signal token is no longer active";
            }catch(const std::exception& e){error=e.what();}
            if(trace_) {
                std::ostringstream out;out<<"visual-signal character="<<scene.input.character
                    <<" generation="<<signal.generation<<" intent="<<signal.intent<<" name="<<signal.name<<" receipt="<<receipt;
                if(!error.empty())out<<" error=["<<error<<"]";
                trace_(out.str());
            }
        }
        if(scene.player && (closing_ || !Alive(scene.owner)) && !scene.player->Status().closing)scene.player->End();
        if(scene.player && scene.player->Status().closing)scene.player->Service();
        if(scene.partners && frame_)scene.partners->Service(*frame_);
        scene.ReleasePending();
        for(auto input=scene.context_inputs.begin();input!=scene.context_inputs.end();) {
            if(input->second.context.expired())input=scene.context_inputs.erase(input);else ++input;
        }
        if(trace_ && scene.player) {
            const auto& status=scene.player->Status();std::ostringstream out;
            out<<"visual-program owner="<<scene.owner.token.entity<<" generation="<<scene.owner.token.generation
                <<" program="<<scene.input.program->id<<" intent="<<status.intent<<" playback="<<status.playback<<" model="<<status.model
                <<" action="<<status.action<<" state="<<status.state<<" prepared="<<status.prepared<<" playing="<<status.playing
                <<" finished="<<status.finished<<" completing="<<status.completing<<" completed="<<status.completed
                <<" closing="<<status.closing<<" drained="<<scene.player->Drained();
            out<<" signal="<<status.signal<<" signal_submitted="<<status.last_signal<<" signal_completed="<<status.completed_signal;
            for(const auto& [name,token]:scene.player->ActiveContexts())out<<" context=["<<name<<":"<<token<<"]";
            for(const auto& value:status.waiting)out<<" waiting=["<<value<<"]";
            for(const auto& value:status.issues)out<<" issue=["<<value<<"]";
            for(const auto& value:status.notes)out<<" note=["<<value<<"]";
            const auto text=out.str();
            if(text!=scene.last_trace){trace_(text);scene.last_trace=text;}
        }
        if(!scene.player || scene.player->Drained()) {
            for(const auto& [name,channel]:scene.channels)Detach(scene,name);
            scene.ReleasePending();
            if(scene.releasing.empty()) {
                for(const auto& [name,channel]:scene.channels)for(auto rule:channel.rules)routes_.Unbind(rule);
                it=scenes_.erase(it);continue;
            }
        }
        ++it;
    }
}
bool VisualProgramRuntime::PrepareModelChange(api::EntityOrigin origin) {
    return !motion_ || motion_->ReleaseParticipant(origin);
}
bool VisualProgramRuntime::Close() {
    closing_=true;Collect();if(!scenes_.empty())return false;
    constructions_->Stop();effects_->Stop();
    if(!sequences_->Close())return false;
    if(motion_){motion_->Collect();if(!motion_->Empty())return false;}
    if(entity_follow_ && !entity_follow_->Close())return false;
    routes_.Clear();return true;
}
}
