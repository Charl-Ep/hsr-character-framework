#include "../support/ModelServicesFixture.hpp"
#include <hksr/presentation/PresentationBindings.hpp>
#include <hksr/model/ModelProfileInputs.hpp>
#include <hksr/model/ModelProfileRequest.hpp>
#include <hksr/framework/presentation/ModelActionHandoff.hpp>
#include <hksr/framework/catalog/SourceCatalog.hpp>
#include <hksr/framework/character/CharacterSession.hpp>
#include <hksr/framework/character/SceneCharacters.hpp>
#include <hksr/framework/layout/SceneLayout.hpp>
#include <hksr/framework/host/FrameworkHost.hpp>
#include <Windows.h>
#include <cstddef>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <cstring>
#include <thread>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<hksr::api::ModelProfileInputs>);
static_assert(!std::is_copy_constructible_v<hksr::api::ModelProfileRequest>);

namespace {
using namespace hksr;
struct Value { std::string text; std::vector<void*> items; void* child=nullptr; } baseline_path,baseline_entity_config,baseline_events,baseline_layers,string_type,model,operation;
struct Consumer { void* unused[3]{}; void* paths=&baseline_events; void* layers=&baseline_layers; } consumer;
std::vector<std::unique_ptr<Value>> values;
std::map<int,void*> roots;
int info,next=0,initializes=0;
bool constructor_fault=false,array_fault=false,null_config=false;
int value_applies=0,value_restores=0,value_active=0,value_ended=0;
bool value_fail=false,require_value_on_die=false;
std::string value_path;
struct ValueLease:api::EventValueLease {
    bool active=false;std::string error="fixture value apply failure";
    bool Apply(void* component,const std::string& path)override {CheckInput(component,path);active=true;++value_active;++value_applies;value_path=path;return !value_fail;}
    static void CheckInput(void* component,const std::string& path){if(!component || path.empty())throw std::runtime_error("value fixture missing source");}
    bool Restore()override {if(active){active=false;--value_active;++value_restores;}return true;}
    bool Close(bool ended)override {if(ended && active){active=false;--value_active;++value_ended;}else Restore();return true;}
    const std::string& Error()const override{return error;}
};
bool keep_fail=false;
int model_type,style_type,event_type,data_type,style,npc_manager,npc_definition,npc_row;
int gaze_controller_type,gaze_driver_type,gaze_type,gaze_component,gaze_head;
Value gaze_components;
bool gaze_enabled=false,gaze_active=false,gaze_automatic=true,gaze_fading=false;
int gaze_follow_calls=0;
void* current_event_component=nullptr;
bool controller_installed=false;
void* current_path=&baseline_path;
void* current_config=&baseline_entity_config;
void CheckAt(bool value,int line) { if(!value) {std::cerr<<"model profile input test line "<<line<<'\n';throw std::runtime_error("model profile input transaction failed");} }
#define Check(...) CheckAt((__VA_ARGS__),__LINE__)
template<class T> api::Method M(T f) { return {reinterpret_cast<void*>(f),&info}; }
bool Rooted(void* object) { for(const auto& item:roots) if(item.second==object) return true; return false; }
int __fastcall Keep(void* object,int zero,int kind,void*) {
    Check(!zero && kind==2);
    if(keep_fail) { keep_fail=false; return 0; }
    roots[++next]=object; return next;
}
void* __fastcall Target(int id,void*) { return roots.at(id); }
void __fastcall Free(int id,void*) { roots.erase(id); }
void* __fastcall Path(void* object,void*) { Check(object==&model && Rooted(object)); return current_path; }
void* __fastcall Config(void* object,void*) { Check(object==&model); return null_config?nullptr:current_config; }
void __fastcall Initialize(void* object,void*) {
    Check(object==&operation && Rooted(object)); ++initializes;
    if(constructor_fault) RaiseException(0xE06C0001,0,0,nullptr);
}
bool __fastcall Initialized(void* object,void*) { Check(object==&model); return true; }
void* __fastcall Array(void* type,int size,void*) {
    Check(type==&string_type && Rooted(type)); values.push_back(std::make_unique<Value>());
    auto* value=values.back().get(); value->items.resize(size); return value;
}
void __fastcall Set(Value* array,Value* element,int index,void*) {
    Check(Rooted(array) && Rooted(element)); array->items.at(index)=element;
    if(array_fault) RaiseException(0xE06C0002,0,0,nullptr);
}
struct Objects:api::ManagedObjects {
    api::ObjectResult NewStringUtf8(const char* text) override {
        values.push_back(std::make_unique<Value>()); values.back()->text=text;
        return {{true,0},values.back().get()};
    }
    api::ObjectResult ManagedType(const char* ns,const char* name) override {
        if(std::string(ns)=="RPG.GameCore") {
            if(std::string(name)=="CharacterModelComponent") return {{true,0},&model_type};
            if(std::string(name)=="FreeStyleComponent") return {{true,0},&style_type};
            if(std::string(name)=="AdventureCharacterDataComponent") return {{true,0},&data_type};
            if(std::string(name)=="AdventureCharacterController") return {{true,0},&gaze_controller_type};
        }
        if(std::string(ns)=="RPG.Client" && std::string(name)=="LookAtIK") return {{true,0},&gaze_type};
        if(std::string(ns).empty() && std::string(name)=="PKBOEALAKCM") return {{true,0},&gaze_driver_type};
        if(std::string(ns).empty() && std::string(name)=="LGBHONLHNPN") return {{true,0},&event_type};
        Check(std::string(ns)=="System" && std::string(name)=="String"); return {{true,0},&string_type};
    }
    api::ObjectResult AllocateObject(const char* ns,const char* name) override {
        Check(std::string(ns).empty() && std::string(name)=="HLCPCNFMECN"); return {{true,0},&operation};
    }
};
struct Resolver:api::MethodResolver {
    bool missing_animator=false;
    api::Method Resolve(const char* ns,const char* type,const char* name,const char* result,
        std::initializer_list<const char*> args,api::MethodKind kind) override {
        Check(args.size()==0 && kind==api::MethodKind::Instance);
        const std::string method=name;
        if(method==".ctor") {
            Check(std::string(ns).empty() && std::string(type)=="HLCPCNFMECN" && std::string(result)=="System.Void"); return M(Initialize);
        }
        if(method=="get_isInitialized") {
            Check(std::string(ns)=="UnityEngine" && std::string(type)=="Animator" && std::string(result)=="System.Boolean");
            if(missing_animator) throw std::runtime_error("missing Animator declaration"); return M(Initialized);
        }
        Check(std::string(ns)=="RPG.GameCore" && std::string(type)=="CharacterModelComponent" && std::string(result)=="System.String");
        if(method=="get_CharAssetPath") return M(Path);
        Check(method=="get_ConfigEntityPath"); return M(Config);
    }
};
int entity,world,animator,art;
auto* entity_ptr=reinterpret_cast<api::GameEntity*>(&entity);
auto* world_ptr=reinterpret_cast<api::GameWorld*>(&world);
auto* model_ptr=reinterpret_cast<api::CharacterModelComponent*>(&model);
int load_state=5,begins=0,cancels=0,restores=0,event_sets=0;
bool changing=false,animator_present=true,animator_ready=true,can_change=true;
bool begin_fault=false,cancel_fault=false,event_noop=false;
bool resolved_alias=false,submit_wrong=false;
bool dispose_on_begin=false;
bool async_requested=false,async_pending=false,pending_fault=false;
void* async_path=nullptr;
Value alias_path;
void __fastcall Begin(void* op,void* owner,api::ModelInputs inputs,bool asynchronous,void* callback,void*) {
    Check(!controller_installed);
    Check(op==&operation && owner==entity_ptr && Rooted(op) && Rooted(owner) && asynchronous==async_requested && !callback);
    if(asynchronous){
        ++begins;async_pending=true;async_path=inputs.model_path;
        if(begin_fault)RaiseException(0xE06C0010,0,0,nullptr);
        return;
    }
    ++begins; current_path=submit_wrong?static_cast<void*>(&baseline_path):(resolved_alias?&alias_path:inputs.model_path);
    current_config=nullptr;
    if(dispose_on_begin) api::TrackedEntities().Disposed(api::TrackedEntities().Current(entity_ptr));
    // Deliberately leaves events untouched: same-path / empty-array behavior
    // means successful Begin cannot stand in for post-load input adoption.
    if(begin_fault) RaiseException(0xE06C0010,0,0,nullptr);
}
void __fastcall Cancel(void* op,void*) {
    Check(op==&operation); ++cancels;
    if(cancel_fault) RaiseException(0xE06C0011,0,0,nullptr);
    async_pending=false;async_path=nullptr;
}
bool __fastcall Pending(void* op,void*) {
    Check(op==&operation && Rooted(op));
    if(pending_fault)RaiseException(0xE06C0012,0,0,nullptr);
    return async_pending;
}
bool __fastcall CanChange(void* m,void* path,void** resolved,void*) {
    Check(m==&model); *resolved=resolved_alias?&alias_path:path; return can_change;
}
void __fastcall Restore(void* m,void* path,void* config,void* prepared_root,void*) {
    Check(!controller_installed);
    Check(m==&model && !prepared_root); ++restores; current_path=path; current_config=config;
}
bool __fastcall Equals(void* a,void* b,void*) { return static_cast<Value*>(a)->text==static_cast<Value*>(b)->text; }
void* __fastcall Art(void*,void*) { return &art; }
void* __fastcall Animator(void*,void*) { return animator_present?&animator:nullptr; }
int __fastcall LoadState(void*,void*) { return load_state; }
bool __fastcall Changing(void*,void*) { return changing; }
bool __fastcall Alive(void*,void*) { return true; }
bool __fastcall AnimatorReady(void*,void*) { return animator_ready; }
api::GameEntity* __fastcall Create(void*,void*,void*,void*,bool,void*) { return entity_ptr; }
unsigned __fastcall EntityId(void*,void*) { return 55; }
api::GameWorld* __fastcall EntityWorld(void*,void*) { return world_ptr; }
void* __fastcall Component(void* e,void* type,void*) {
    Check(e==entity_ptr);
    if(type==&model_type) return &model;
    if(type==&event_type) return current_event_component;
    if(type==&gaze_controller_type || type==&gaze_driver_type)return e;
    Check(type==&style_type); return &style;
}
void __fastcall Kill(void* e,bool immediate,void* metadata) { Check(e==entity_ptr && immediate && metadata==&info); }
void __fastcall Die(void* w,void* e,void* killer,void* metadata) {
    if(require_value_on_die)Check(value_active==1);
    Check(w==world_ptr && e==entity_ptr && !killer && metadata==&info && !controller_installed);
    api::TrackedEntities().Disposed(api::TrackedEntities().Current(entity_ptr));
}
int npc_plays=0,battle_plays=0,animator_plays=0,controller_sets=0,controller_clears=0;
bool state_exists=true,requeue_battle=false;
bool requeue_on_controller=false;
bool default_absent=false;
int observed_hash=0,state_reads=0;
float observed_time=0;
framework::ModelActionHandoff* active_handoff=nullptr;
int __fastcall Hash(Value* name,void*) { Check(name->text=="SomeState" || name->text=="OtherState"); return name->text=="SomeState"?37:41; }
bool __fastcall Has(void* a,int layer,int hash,void*) { Check(a==&animator && layer==0 && (hash==37 || hash==41)); return state_exists && !(default_absent && hash==41); }
void __fastcall ObserveState(void* a,int layer,int index,unsigned char* bytes,void* metadata) {
    Check(a==&animator && layer==0 && index==0 && metadata==&info); ++state_reads;
    std::memset(bytes,0,36); std::memcpy(bytes,&observed_hash,4);
    std::memcpy(bytes+12,&observed_time,4);
}
void __fastcall NpcPlay(void* object,unsigned id,void*) { Check(object==&style && id==18); ++npc_plays; }
void __fastcall AnimatorPlay(void* a,int,int,float,void*) { Check(a==&animator); ++animator_plays; }
void __fastcall BattlePlay(void* object,int hash,float first,float second,void*) {
    Check(object==&style && (hash==37 || hash==41) && first==0 && second==0); ++battle_plays;
    if(requeue_battle) { requeue_battle=false; active_handoff->Queue(framework::ActionDomain::Exploration,framework::NpcMotion{18}); }
}
void __fastcall ControllerPath(void* m,Value* path,void* key,void*) {
    Check(m==&model && !key);
    if(path) {
        Check(path->text=="Controller/exploration"); ++controller_sets; controller_installed=true;
        if(requeue_on_controller) { requeue_on_controller=false; active_handoff->Queue(framework::ActionDomain::Exploration,framework::NpcMotion{18}); }
    }
    else { ++controller_clears; controller_installed=false; }
}
int map_group,map_definition,map_phase;
unsigned map_floor=2;
void* __fastcall Group(void*,void*) {return &map_group;}
void* __fastcall Map(void*,void*) {return &map_definition;}
unsigned __fastcall Key(void*,void*) {return map_floor;}
}
__declspec(noinline) void TestGazeHandoff(const api::WorldFrame& frame,const api::NpcCreateInput& create,
    const framework::CharacterSessionServices& session_services,const api::ReferenceMethods& references,
    const api::EntityAccessMethods& entity_methods,const framework::CharacterPackage& package,
    framework::AppearanceServices appearance,api::AnimationInputTypes types) {
    using Domain=framework::ActionDomain;using Handoff=framework::HandoffPhase;
    auto& assets=appearance.assets;const auto& visuals=appearance.visuals;
    const auto& config=appearance.config;const auto& components=appearance.components;
    // Use the real SDK controls and real registry, including its public
    // action selection path. A pending official fade must not reject a
    // domain switch or re-submit the old look target on the next frame.
    current_path=&baseline_path;current_config=&baseline_entity_config;
    consumer.paths=&baseline_events;consumer.layers=&baseline_layers;
    model.child=&string_type;load_state=5;changing=false;animator_present=true;animator_ready=true;
    observed_hash=41;observed_time=0;auto current=frame;current.sequence=900;
    current.manager={create.manager,{true,0}};current.battle={false,{true,0}};
    current.adventure_phase={&map_phase,{true,0}};
    api::PresentationBindings controls;controls.references=references;controls.entity=entity_methods;controls.lifetime.alive=M(Alive);
    controls.lifetime.destroy=M(+[](void*,void*){});
    controls.placement.player=M(+[](void*,void*)->void*{return entity_ptr;});
    controls.placement.owner_world=M(EntityWorld);
    controls.placement.game_object=M(+[](void*,void*)->void*{return &art;});
    controls.placement.transform=M(+[](void* value,void*)->void*{return value;});
    controls.placement.position=M(+[](void*,api::Position3* out,void*){*out={};});
    controls.placement.rotation=M(+[](void*,api::Rotation4* out,void*){*out={0,0,0,1};});
    controls.actor_control.mono=M(+[](void*,void*)->void*{return nullptr;});
    controls.actor_control.model_attach_point=M(+[](void*,void*,bool,void*)->void*{return &gaze_head;});
    controls.actor_control.ik_head=M(+[](void*,void*)->void*{return &gaze_head;});
    controls.actor_control.ik_follow=M(+[](void*,void*,bool,bool,void*,void*){gaze_active=true;++gaze_follow_calls;});
    controls.actor_control.ik_stop=M(+[](void*,bool,void*){gaze_active=gaze_fading;});
    controls.actor_control.ik_active=M(+[](void*,void*){return gaze_active;});
    controls.actor_control.behaviour_enabled=M(+[](void*,void*){return gaze_enabled;});
    controls.actor_control.set_behaviour_enabled=M(+[](void*,bool enabled,void*){gaze_enabled=enabled;});
    controls.actor_control.look_enabled=M(+[](void*,void*){return gaze_automatic;});
    controls.actor_control.set_look_enabled=M(+[](void*,bool enabled,void*){gaze_automatic=enabled;});
    controls.arrays.length=M(+[](Value* value,void*){return static_cast<int>(value->items.size());});
    controls.arrays.get=M(+[](Value* value,int index,void*)->void*{return value->items.at(index);});
    gaze_components.items={&gaze_component};
    api::SceneObjectMethods gaze_scene_methods;gaze_scene_methods.string_equals=M(Equals);
    gaze_scene_methods.components=M(+[](void*,void*,bool,void*)->void*{return &gaze_components;});
    api::SceneObjects gaze_scene(gaze_scene_methods);
    framework::AppearanceServices with_gaze{assets,gaze_scene,visuals,config,components};
    framework::SceneCharacters registry(session_services,[&](auto& session,const auto&,const auto&){session.Create(create);});
    api::PresentationContext control_context(assets.objects,controls);
    registry.ConfigureControls(control_context.Controls(),control_context.Player());
    framework::CharacterPlan plan;plan.package=package;
    plan.alternate=framework::BattlePresentation{{"battle.prefab",{},{}},{true,"OtherState",{}}};
    const framework::SceneAddress address{"gaze-map",1};
    const auto id=registry.Add(address.key,plan,framework::PlacedAction{Domain::Alternate,framework::FreeStyleState{"SomeState"}});
    registry.Service(current,address,with_gaze,types);++current.sequence;registry.Service(current,address,with_gaze,types);
    if(registry.Status(id).handoff->phase!=Handoff::Ready)std::cerr<<"gaze fixture handoff: "<<registry.Status(id).handoff->error<<" phase="<<static_cast<int>(registry.Status(id).handoff->phase)<<" action="<<registry.Status(id).action->detail<<'\n';
    Check(registry.Status(id).handoff->phase==Handoff::Ready);
    registry.Look(id,{framework::LookMode::Player});++current.sequence;registry.Service(current,address,with_gaze,types);
    Check(registry.Status(id).controls.has_value());
    if(!gaze_enabled || !gaze_active || gaze_automatic)std::cerr<<"gaze fixture controls: "<<registry.Status(id).controls->gaze_error<<" phase="<<static_cast<int>(registry.Status(id).controls->gaze_phase)<<'\n';
    Check(gaze_enabled && gaze_active && !gaze_automatic);
    gaze_fading=true;const auto calls_before=gaze_follow_calls;const auto plays_before=npc_plays;
    const auto first=registry.Select(id,{Domain::Exploration,framework::NpcMotion{18}});
    Check(registry.Status(id).action->id==first && registry.Status(id).action->phase==framework::ActionIntentPhase::Waiting);
    ++current.sequence;registry.Service(current,address,with_gaze,types);
    Check(npc_plays==plays_before && gaze_follow_calls==calls_before && gaze_enabled);
    // Replacing a pending request must not dispatch its predecessor later.
    const auto replacement=registry.Select(id,{Domain::Exploration,framework::AnimatorState{"SomeState"}});
    const auto animator_before=animator_plays;
    gaze_active=false;gaze_fading=false;
    ++current.sequence;registry.Service(current,address,with_gaze,types);
    ++current.sequence;registry.Service(current,address,with_gaze,types);
    Check(registry.Status(id).action->id==replacement && registry.Status(id).action->phase==framework::ActionIntentPhase::Returned);
    Check(npc_plays==plays_before && animator_plays==animator_before+1);
    Check(registry.Status(id).handoff->ready_domain==Domain::Exploration);
    registry.Shutdown();registry.Service(current,address,with_gaze,types);
    Check(registry.Closed() && roots.empty() && !gaze_enabled && gaze_automatic);
}
void TestModelProfileInputs() {
    Resolver resolver; resolver.missing_animator=true;
    const auto partial=api::ResolveProfileInputMethods(resolver);
    Check(partial.model_path.code && partial.operation_constructor.code && !partial.animator_initialized.code && partial.binding_errors.size()==1);
    resolver.missing_animator=false;
    api::ProfileInputs access(api::ResolveProfileInputMethods(resolver));
    Check(access.AnimatorInitialized(&model).value);
    std::vector<api::DeclaredField> declarations{
        {"","Consumer","KGGGDHNMDMK","System.String[]",offsetof(Consumer,paths),6,true},
        {"","Consumer","NBNJJDIMDKE","System.String[]",offsetof(Consumer,layers),6,true}};
    auto static_copy=declarations.front(); static_copy.flags=0x16; declarations.push_back(static_copy);
    const auto fields=api::ResolveProfileEventFields(declarations);
    Check(fields.has_paths && fields.has_layers);
    declarations.push_back(declarations.front());
    const auto ambiguous=api::ResolveProfileEventFields(declarations);
    Check(!ambiguous.has_paths && ambiguous.has_layers);
    const auto read=api::ReadProfileEventArrays(&consumer,ambiguous);
    Check(!read.paths.call.returned && read.layers.object==&baseline_layers);
    api::ReferenceMethods references{M(Keep),M(Target),M(Free),{}, {}};
    api::ManagedArrays arrays({M(Array),{},{},M(Set),{}});
    Objects objects;
    const api::AvatarCatalogRow source{777,"角色/战斗.json","角色/战斗.prefab"};
    const api::PresentationConfigInput source_config{source.character_config,{true,"StandBy",{}},
        {true,{"表情.json","附件.json"},{}},{true,{"Scene"},{}}};
    const auto description=framework::SourceCatalog::Presentation(source,source_config).model;
    {
        api::ModelProfileInputs inputs(references);
        inputs.Prepare(objects,arrays,access,fields,&model,&consumer,description);
        const auto& s=inputs.Status();
        Check(s.prepared && initializes==1 && s.baseline.model_path==&baseline_path && s.baseline_entity_config==&baseline_entity_config);
        Check(s.baseline.event_paths==&baseline_events && s.baseline.excluded_layers==&baseline_layers);
        Check(!s.target.adventure_config && !s.baseline.adventure_config);
        Check(static_cast<Value*>(s.target.model_path)->text==description.model_path);
        auto* events=static_cast<Value*>(s.target.event_paths);
        Check(events->items.size()==2 && static_cast<Value*>(events->items[1])->text=="附件.json");
        Check(inputs.Close().complete && roots.empty());
    }
    {
        consumer.layers=nullptr; null_config=true;
        api::ModelProfileInputs inputs(references);
        inputs.Prepare(objects,arrays,access,fields,&model,&consumer,{"battle.prefab",{}, {}});
        const auto& s=inputs.Status();
        Check(s.prepared && !s.baseline_entity_config && s.baseline.excluded_layers &&
            static_cast<Value*>(s.baseline.excluded_layers)->items.empty() && static_cast<Value*>(s.target.event_paths)->items.empty());
        Check(inputs.Close().complete && roots.empty()); consumer.layers=&baseline_layers; null_config=false;
    }
    {
        constructor_fault=true;
        api::ModelProfileInputs inputs(references);
        inputs.Prepare(objects,arrays,access,fields,&model,&consumer,description);
        Check(!inputs.Status().prepared && inputs.Status().operation.object==&operation && inputs.Status().initialize.exception==0xE06C0001);
        Check(Rooted(&operation) && inputs.Close().complete && roots.empty()); constructor_fault=false;
    }
    {
        array_fault=true; const auto before=initializes;
        api::ModelProfileInputs inputs(references);
        inputs.Prepare(objects,arrays,access,fields,&model,&consumer,description);
        Check(!inputs.Status().prepared && initializes==before && inputs.Close().complete && roots.empty()); array_fault=false;
    }
    std::cout<<"Model profile input transaction passed: official getters/default constructor, retained baseline arrays, event-only target and partial-failure roots; local substitutes only.\n";

    // Same typed inputs, now consumed by the real request implementation.
    baseline_path.text="npc.prefab"; baseline_entity_config.text="npc.entity"; alias_path.text="resolved/battle.prefab";
    api::ModelAccessMethods model_methods{M(Art),M(Animator),M(LoadState),M(Changing),M(Alive),M(Alive),offsetof(Value,child),true,{}};
    auto input_methods=api::ResolveProfileInputMethods(resolver); input_methods.animator_initialized=M(AnimatorReady);
    struct EventComponent { void* consumer; } component_view{&consumer};
    api::AnimationEventMethods event_methods; event_methods.has_consumer=true; event_methods.consumer_offset=offsetof(EventComponent,consumer);
    // Use actual memory identity for the event consumer getter, as in SDK.
    auto* event_object=&component_view;
    api::ModelMethods profile_methods{M(Begin),M(Cancel),{},{},M(CanChange),M(Restore)};
    // Event setter below is redirected to the same official signature but
    // accepts this synthetic outer component rather than its old placeholder.
    profile_methods.events=M(+[](void* c,void* paths,void* layers,void*) {
        auto* object=static_cast<EventComponent*>(c); Check(object->consumer==&consumer); ++event_sets;
        if(!event_noop) { consumer.paths=paths; consumer.layers=layers; }
    });
    api::ModelRequestServices services{api::ModelProfile(profile_methods),api::ModelAccess(model_methods),api::ProfileInputs(input_methods),
        api::SceneObjects({{},{},{},M(Equals),{}}),api::AnimationEvents(event_methods),api::ModelLoadCompleted};
    api::WorldFrame frame; frame.world=world_ptr; frame.disposing={false,{true,0}};
    auto foreign=frame; foreign.world=reinterpret_cast<api::GameWorld*>(1);
    std::string value_source;
    auto make=[&]() {
        current_path=&baseline_path; current_config=&baseline_entity_config;
        consumer.paths=&baseline_events; consumer.layers=&baseline_layers;
        model.child=&string_type; load_state=5; changing=false; animator_present=true; animator_ready=true;
        auto inputs=std::make_unique<api::ModelProfileInputs>(references);
        inputs->Prepare(objects,arrays,access,fields,&model,&consumer,{"battle.prefab",{}, {},value_source});
        Check(inputs->Status().prepared);
        return std::make_unique<api::ModelProfileRequest>(api::TrackedEntities().Current(entity_ptr),world_ptr,std::move(inputs),references,services);
    };
    using Phase=api::ModelRequestPhase; using Destination=api::ModelDestination;
    const auto token=api::TrackedEntities().Track(entity_ptr);
    {
        value_source="Config/Test.json";services.event_values=[] {return std::make_unique<ValueLease>();};
        auto request=make();request->Submit(frame,model_ptr,Destination::Alternate);request->Observe(frame,model_ptr,true);
        request->BindEvents(frame,model_ptr,event_object,fields);
        Check(request->Status().phase==Phase::EventsReady && value_active==1 && value_applies==1 && value_path==value_source);
        request->BindEvents(frame,model_ptr,event_object,fields);Check(value_applies==1);
        request->Submit(frame,model_ptr,Destination::Baseline);Check(value_active==1);
        request->Observe(frame,model_ptr,true);request->BindEvents(frame,model_ptr,event_object,fields);
        Check(request->Status().phase==Phase::EventsReady && value_active==0 && value_restores==1);
        Check(request->Close(frame).complete && roots.empty());
        value_fail=true;request=make();request->Submit(frame,model_ptr,Destination::Alternate);request->Observe(frame,model_ptr,true);
        request->BindEvents(frame,model_ptr,event_object,fields);Check(request->Status().phase==Phase::Failed);
        const auto attempts=value_applies;value_fail=false;
        request->Submit(frame,model_ptr,Destination::Alternate);request->Observe(frame,model_ptr,true);
        request->BindEvents(frame,model_ptr,event_object,fields);
        Check(request->Status().phase==Phase::Failed && value_applies==attempts); // retained failed lease cannot silently bypass preparation
        Check(request->Close(frame).complete && value_active==0 && roots.empty());value_source.clear();
    }
    {
        auto request=make(); const int before=begins,event_before=event_sets,restore_before=restores;
        request->Submit(foreign,model_ptr,Destination::Alternate); Check(begins==before);
        resolved_alias=true;
        request->Submit(frame,model_ptr,Destination::Alternate);
        Check(request->Status().phase==Phase::AwaitingModel && begins==before+1);
        // Both root/Art liveness getters are true while load state is pending.
        load_state=4; request->Observe(frame,model_ptr,true); Check(request->Status().phase==Phase::AwaitingModel);
        load_state=5; changing=true; request->Observe(frame,model_ptr,true); Check(request->Status().phase==Phase::AwaitingModel);
        changing=false; animator_ready=false; request->Observe(frame,model_ptr,true); Check(request->Status().phase==Phase::AwaitingModel);
        animator_ready=true; request->Observe(frame,model_ptr,true); Check(request->Status().phase==Phase::ModelReady);
        request->BindEvents(frame,model_ptr,event_object,fields); Check(request->Status().phase==Phase::EventsReady && event_sets==event_before+1);
        request->BindEvents(frame,model_ptr,event_object,fields); Check(event_sets==event_before+1);
        const int cancel_before=cancels;
        Check(request->Cancel(frame) && cancels==cancel_before+1 && current_path==&alias_path); // cancel is NOT restore
        resolved_alias=false; request->Submit(frame,model_ptr,Destination::Baseline);
        Check(restores==restore_before+1 && current_path==&baseline_path && current_config==&baseline_entity_config);
        animator_present=false; request->Observe(frame,model_ptr,false); Check(request->Status().phase==Phase::ModelReady);
        request->BindEvents(frame,model_ptr,event_object,fields); Check(request->Status().phase==Phase::EventsReady && consumer.paths==&baseline_events);
        Check(request->Close(frame).complete && roots.empty());
    }
    {
        auto request=make(); can_change=false;
        request->Submit(frame,model_ptr,Destination::Alternate); Check(request->Status().phase==Phase::Failed);
        const int before=event_sets;
        request->Submit(frame,model_ptr,Destination::Baseline); request->Observe(frame,model_ptr,false);
        request->BindEvents(frame,model_ptr,event_object,fields);
        Check(request->Status().phase==Phase::EventsReady && event_sets==before); // already installed
        can_change=true; Check(request->Close(frame).complete && roots.empty());
    }
    {
        auto request=make(); begin_fault=true;
        request->Submit(frame,model_ptr,Destination::Alternate);
        Check(request->Status().phase==Phase::Failed && request->Status().operation_in_use && request->Status().submit.exception==0xE06C0010);
        begin_fault=false; Check(request->Close(frame).complete && roots.empty());
    }
    {
        auto request=make(); request->Submit(frame,model_ptr,Destination::Alternate);
        cancel_fault=true; const int before=cancels;
        Check(!request->Close(frame).complete); Check(!request->Close(frame).complete && cancels==before+1);
        request->Submit(frame,model_ptr,Destination::Baseline); Check(cancels==before+1);
        cancel_fault=false; api::TrackedEntities().Disposed(token);
        Check(request->Close(foreign).complete && roots.empty() && cancels==before+1);
    }
    api::TrackedEntities().Forget(token);
    const auto next_token=api::TrackedEntities().Track(entity_ptr);
    {
        auto request=make(); request->Submit(frame,model_ptr,Destination::Alternate);
        request->Observe(frame,model_ptr,true); event_noop=true;
        request->BindEvents(frame,model_ptr,event_object,fields);
        Check(request->Status().phase==Phase::Failed && request->Status().event_replace.returned);
        const int before=event_sets;
        request->BindEvents(frame,model_ptr,event_object,fields); Check(event_sets==before);
        event_noop=false; Check(request->Close(frame).complete && roots.empty());
    }
    {
        auto request=make(); submit_wrong=true;
        request->Submit(frame,model_ptr,Destination::Alternate); Check(request->Status().phase==Phase::Failed);
        submit_wrong=false; Check(request->Close(frame).complete && roots.empty());
    }
    {
        auto request=make(); request->Submit(frame,model_ptr,Destination::Alternate);
        keep_fail=true; request->Submit(frame,model_ptr,Destination::Baseline);
        Check(request->Status().phase==Phase::Failed);
        request->Submit(frame,model_ptr,Destination::Baseline);
        Check(request->Status().phase==Phase::AwaitingModel && request->Close(frame).complete && roots.empty());
    }
    {
        auto request=make(); dispose_on_begin=true;
        request->Submit(frame,model_ptr,Destination::Alternate);
        Check(request->Status().phase==Phase::OwnerEnded);
        dispose_on_begin=false; const int before=cancels;
        Check(request->Close(foreign).complete && roots.empty() && cancels==before);
    }
    api::TrackedEntities().Forget(next_token);
    {
        const auto async_owner=api::TrackedEntities().Track(entity_ptr);
        const auto original_profile=services.profile;
        services.profile=api::ModelProfile(profile_methods,M(Pending));async_requested=true;
        {
            auto request=make();const auto before_events=event_sets;
            request->Submit(frame,model_ptr,Destination::Alternate);
            Check(request->Status().phase==Phase::AwaitingModel && current_path==&baseline_path && async_pending);
            request->Observe(frame,model_ptr,true);Check(request->Status().phase==Phase::AwaitingModel && event_sets==before_events);
            current_path=async_path;current_config=nullptr;async_pending=false;
            load_state=4;request->Observe(frame,model_ptr,true);Check(request->Status().phase==Phase::AwaitingModel);
            load_state=5;request->Observe(frame,model_ptr,true);Check(request->Status().phase==Phase::ModelReady);
            request->BindEvents(frame,model_ptr,event_object,fields);Check(request->Status().phase==Phase::EventsReady);
            Check(request->Close(frame).complete && roots.empty());
        }
        {
            auto request=make();request->Submit(frame,model_ptr,Destination::Alternate);
            const auto before=cancels;
            request->Submit(frame,model_ptr,Destination::Baseline);
            Check(cancels==before+1 && !async_pending && !async_path && current_path==&baseline_path);
            request->Observe(frame,model_ptr,false);Check(request->Status().phase==Phase::ModelReady);
            Check(request->Close(frame).complete && roots.empty());
        }
        {
            auto request=make();request->Submit(frame,model_ptr,Destination::Alternate);
            async_pending=false; // Official failure completed without replacing the old model.
            request->Observe(frame,model_ptr,true);Check(request->Status().phase==Phase::Failed);
            Check(request->Close(frame).complete && roots.empty());
        }
        {
            auto request=make();begin_fault=true;request->Submit(frame,model_ptr,Destination::Alternate);begin_fault=false;
            Check(request->Status().phase==Phase::Failed && async_pending);
            Check(request->Close(frame).complete && !async_pending && roots.empty());
        }
        {
            auto request=make();request->Submit(frame,model_ptr,Destination::Alternate);pending_fault=true;
            request->Observe(frame,model_ptr,true);Check(request->Status().phase==Phase::Failed);
            pending_fault=false;Check(request->Close(frame).complete && !async_pending && roots.empty());
        }
        {
            auto request=make();request->Submit(frame,model_ptr,Destination::Alternate);
            api::TrackedEntities().Disposed(async_owner);const auto before=cancels;
            Check(request->Close(foreign).complete && cancels==before+1 && !async_pending && roots.empty());
        }
        api::TrackedEntities().Forget(async_owner);async_requested=false;services.profile=original_profile;
    }
    std::cout<<"Official model request tests passed: actual DLL calls, pending loading, cancel vs restoration, empty/same-path event adoption, faults and owner-end closure; no game acceptance.\n";

    // Full coordinator uses the actual CharacterInstance, request, controller
    // binding and ActionPlayback implementations, not a mocked stage port.
    api::EntityAccessMethods entity_methods{M(EntityId),M(EntityWorld),M(Alive),M(Component),{},M(Kill),M(Die),{}};
    api::EntityAccess entities(entity_methods);
    api::AnimationControlMethods action_methods; action_methods.hash=M(Hash); action_methods.has_state=M(Has);
    action_methods.play=M(AnimatorPlay); action_methods.npc_motion=M(NpcPlay); action_methods.free_style_state=M(BattlePlay);
    action_methods.replace_path=M(ControllerPath);
    action_methods.state_info=M(ObserveState);
    action_methods.in_transition=M(+[](void*,int,void*){return false;});
    api::AnimationControl animation(action_methods);
    api::SceneObjects scene({{},{},{},M(Equals),{}});
    api::ControllerBindingServices controller_services{animation,entities,scene,arrays,{},{},{}};
    auto transition=[=](api::EntityToken owner,api::GameWorld* owner_world,std::unique_ptr<api::ModelProfileRequest> request) {
        return std::make_unique<api::ModelTransition>(owner,owner_world,std::move(request),
            api::ModelTransitionServices{references,entities,api::ModelAccess(model_methods),animation,controller_services,fields});
    };
    auto animations=[=](api::EntityToken owner){return std::make_unique<api::ActorAnimationSession>(owner,references,entities,
        api::ModelAccess(model_methods),animation,api::ProfileInputs(input_methods));};
    framework::HandoffServices handoff_services{animations,transition};
    auto create_npc=[=]{return std::make_unique<api::EntityCreationSession>(api::EntityOwner{},M(Create),entity_methods,references);};
    framework::CharacterMethods character_methods{create_npc,TestModelServices(references,{}, {},entity_methods,api::ModelAccess(model_methods))};
    framework::CharacterPackage package; package.id="test.npc";
    api::NpcCreateInput create{world_ptr,reinterpret_cast<api::EntityManager*>(&npc_manager),
        reinterpret_cast<api::MapNpcDefinition*>(&npc_definition),reinterpret_cast<api::NpcDataRow*>(&npc_row),true};
    // Geometry providers intentionally unbound: pure official NPC must not
    // require unrelated external Mesh/texture capabilities to change domains.
    api::MeshAccess mesh({}); api::MeshSkinning skin({}); api::MeshTemplateAccess templates({});
    api::TextureAccess texture({}); api::MaterialAccess material({}); api::ShaderAccess shader({}); api::HierarchyAccess hierarchy({});
    api::VisualObjects visuals({}); api::AdventureConfig config({}); api::ComponentConfig components({});
    framework::ModelAssetServices assets{objects,mesh,skin,templates,texture,material,shader,arrays,hierarchy};
    framework::AppearanceServices appearance{assets,scene,visuals,config,components};
    api::AnimationInputTypes types; types.events={{true,0},&event_type}; current_event_component=event_object;
    framework::CharacterInstance character(world_ptr,package,nullptr,"",character_methods,{});
    character.Create(create); Check(character.Creation().create_returned);
    auto request=make();
    framework::DomainControllers controllers;
    controllers.exploration=api::ControllerSource{api::ControllerSourceKind::ResourcePath,"Controller/exploration"};
    framework::ModelActionHandoff handoff(character,api::TrackedEntities().Current(entity_ptr),world_ptr,
        std::move(request),handoff_services,controllers); active_handoff=&handoff;
    using Domain=framework::ActionDomain; using Handoff=framework::HandoffPhase;
    const auto original_begins=begins,original_restores=restores;
    requeue_on_controller=true;
    handoff.Queue(Domain::Exploration,framework::NpcMotion{18}); handoff.Service(frame,appearance,types);
    Check(npc_plays==0 && controller_sets==1 && handoff.Status().phase==Handoff::Adopting);
    handoff.Service(frame,appearance,types);
    Check(npc_plays==1 && controller_sets==1 && controller_installed && begins==original_begins && restores==original_restores);
    Check(handoff.Status().output && handoff.Status().output->outcome==framework::ActionOutcome::Returned);
    load_state=4; handoff.Queue(Domain::Alternate,framework::FreeStyleState{"SomeState"});
    handoff.Service(frame,appearance,types);
    Check(handoff.Status().phase==Handoff::Loading && begins==original_begins+1 && controller_clears==1 && !controller_installed && battle_plays==0);
    Check(!handoff.Status().output && !handoff.Status().playback_serial); // no prior NPC receipt for the new command
    handoff.Service(frame,appearance,types); Check(begins==original_begins+1 && battle_plays==0);
    // Replace an in-flight request. Cancellation must not restore/kill the NPC;
    // only the newest state is played when the official model really loads.
    const auto replacement_request=handoff.Queue(Domain::Alternate,framework::FreeStyleState{"OtherState"}); load_state=5;
    handoff.Service(frame,appearance,types);
    Check(battle_plays==1 && handoff.Status().phase==Handoff::Ready && !character.Creation().removal_requested);
    Check(handoff.Status().servicing==replacement_request && handoff.Status().output->serial==handoff.Status().playback_serial &&
        std::get<framework::FreeStyleState>(handoff.Status().output->request).name=="OtherState");
    const auto loaded_begins=begins;
    handoff.Queue(Domain::Alternate,framework::FreeStyleState{"SomeState"}); handoff.Service(frame,appearance,types);
    Check(battle_plays==2 && begins==loaded_begins); // same domain: no model reload
    state_exists=false; handoff.Queue(Domain::Alternate,framework::FreeStyleState{"OtherState"}); handoff.Service(frame,appearance,types);
    Check(battle_plays==2 && handoff.Playback().last->outcome==framework::ActionOutcome::StateAbsent);
    state_exists=true; requeue_battle=true;
    handoff.Queue(Domain::Alternate,framework::FreeStyleState{"SomeState"}); handoff.Service(frame,appearance,types);
    Check(battle_plays==3 && npc_plays==1); // callback queues but cannot recursively switch
    handoff.Service(frame,appearance,types);
    Check(npc_plays==2 && restores==original_restores+1 && current_config==&baseline_entity_config && consumer.paths==&baseline_events);
    Check(controller_installed && controller_sets==2 && handoff.Status().ready_domain==Domain::Exploration);
    // Event adoption failure does not delete the character or silently play.
    event_noop=true; handoff.Queue(Domain::Alternate,framework::FreeStyleState{"OtherState"}); handoff.Service(frame,appearance,types);
    Check(handoff.Status().phase==Handoff::Failed && battle_plays==3 && !character.Creation().removal_requested);
    event_noop=false;
    handoff.Queue(Domain::Exploration,framework::AnimatorState{"SomeState"}); handoff.Service(frame,appearance,types);
    Check(animator_plays==1 && handoff.Status().phase==Handoff::Ready && controller_installed);
    handoff.Remove(); handoff.Service(frame,appearance,types);
    Check(handoff.Status().closed && character.Status().closed && !controller_installed && roots.empty());
    {
        framework::CharacterInstance initialized_character(world_ptr,package,nullptr,"",character_methods,{});
        initialized_character.Create(create);
        value_source="Config/Test.json";
        auto initial_request=make();
        framework::ModelActionHandoff initial(initialized_character,api::TrackedEntities().Current(entity_ptr),world_ptr,
            std::move(initial_request),handoff_services,{},"OtherState");
        const auto plays=battle_plays; const auto begin_count=begins;
        frame.sequence=100;
        initial.Queue(Domain::Alternate,framework::FreeStyleState{"SomeState"}); initial.Service(frame,appearance,types);
        Check(initial.Status().phase==Handoff::Initializing && battle_plays==plays+1 && state_reads==0);
        Check(!initial.Status().output && !initial.Status().playback_serial); // initial-state playback is not the requested action
        initial.Service(frame,appearance,types);
        Check(initial.Status().phase==Handoff::Initializing && state_reads==0 && battle_plays==plays+1);
        ++frame.sequence; initial.Service(frame,appearance,types);
        Check(initial.Status().phase==Handoff::Initializing && state_reads==1 && battle_plays==plays+1);
        // Same-domain selection while initializing neither cancels/reloads the
        // model nor repeats its initial request.
        initial.Queue(Domain::Alternate,framework::FreeStyleState{"SomeState"});
        observed_hash=41; ++frame.sequence; initial.Service(frame,appearance,types);
        Check(initial.Status().phase==Handoff::Ready && battle_plays==plays+2 && begins==begin_count+1 && state_reads==2);
        Check(initial.Status().output && std::get<framework::FreeStyleState>(initial.Status().output->request).name=="SomeState");
        const auto requested_output=initial.Status().output->serial;
        observed_hash=37;observed_time=1.1f;++frame.sequence;initial.Service(frame,appearance,types);
        Check(battle_plays==plays+3 && std::get<framework::FreeStyleState>(initial.Playback().last->request).name=="OtherState");
        Check(initial.Status().output->serial==requested_output && std::get<framework::FreeStyleState>(initial.Status().output->request).name=="SomeState");
        observed_time=0;
        const auto before_ended=value_ended;Check(value_active==1);
        require_value_on_die=true;
        initial.Remove(); initial.Service(frame,appearance,types); Check(initial.Status().closed && roots.empty());
        require_value_on_die=false;value_source.clear();
        Check(value_active==0 && value_ended==before_ended+1);
    }
    {
        framework::CharacterInstance absent_character(world_ptr,package,nullptr,"",character_methods,{});
        absent_character.Create(create);
        auto absent_request=make();
        framework::ModelActionHandoff absent(absent_character,api::TrackedEntities().Current(entity_ptr),world_ptr,
            std::move(absent_request),handoff_services,{},"OtherState");
        default_absent=true; const auto plays=battle_plays;
        absent.Queue(Domain::Alternate,framework::FreeStyleState{"SomeState"}); absent.Service(frame,appearance,types);
        Check(absent.Status().phase==Handoff::Ready && battle_plays==plays+1 && !absent.Status().initialization_note.empty());
        default_absent=false; absent.Remove(); absent.Service(frame,appearance,types); Check(absent.Status().closed && roots.empty());
    }
    std::cout<<"Model/action handoff passed: same NPC, controller detach before load, actual model readiness, event adoption, playback, return, replacement, failure and removal; pure-official local fixture only.\n";
    // Source assembly consumes real CharacterInstance/Handoff implementations.
    // An absent/broken battle input must not disable independently usable NPCs.
    auto baseline=[=](api::EntityToken owner,api::GameWorld* owner_world,const api::ProfileDescription& profile) {
        return std::make_unique<api::ModelBaselineCapture>(owner,owner_world,references,entities,services,fields,profile);
    };
    framework::CharacterSessionServices session_services{character_methods,{},handoff_services,baseline};
    auto reset_baseline=[&] {
        current_path=&baseline_path; current_config=&baseline_entity_config;
        consumer.paths=&baseline_events; consumer.layers=&baseline_layers;
        model.child=&string_type; load_state=5; changing=false; animator_present=true; animator_ready=true;
    };
    {
        reset_baseline();
        framework::PreparedSource prepared; prepared.character=package; prepared.finished=true;
        auto plan=framework::CharacterPlan::FromPrepared(prepared);
        Check(plan.action_sources.empty() && !plan.alternate);
        framework::CharacterSession session(world_ptr,std::move(plan),session_services);
        session.Queue(Domain::Exploration,framework::NpcMotion{18}); session.Create(create);
        const auto count=npc_plays,begin_count=begins;
        // Pure NPC does not need an event Type, baseline reader or battle config.
        session.Service(frame,appearance,{});
        Check(session.Status().phase==framework::SessionPhase::Active && npc_plays==count+1 && begins==begin_count);
        Check(!session.Status().baseline_attempted && !session.Handoff()->ModelRequest());
        const auto unsupported=session.Queue(Domain::Alternate,framework::FreeStyleState{"SomeState"}); session.Service(frame,appearance,{});
        Check(!session.Status().errors.empty() && !session.Character().Creation().removal_requested && begins==begin_count);
        Check(session.Status().submitted_action==unsupported && !session.Status().handoff_request && !session.Status().dispatch_error.empty());
        session.Queue(Domain::Exploration,framework::AnimatorState{"SomeState"}); session.Service(frame,appearance,{});
        Check(session.Handoff()->Playback().last->outcome==framework::ActionOutcome::Returned);
        Check(session.Status().dispatch_error.empty() && session.Status().handoff_request==session.Handoff()->Status().servicing);
        session.Remove(); session.Service(frame,appearance,{}); Check(session.Status().closed && roots.empty());
    }
    {
        reset_baseline(); framework::CharacterPlan plan; plan.package=package;
        plan.alternate=framework::BattlePresentation{{"battle.prefab",{},{}},{}};
        auto input=std::make_shared<framework::VisualProgramInput>();
        input->program=std::make_shared<framework::VisualProgram>(framework::ParseVisualProgram(R"({
            "format":"hksr.visual-program","id":"session","source":"battle","scope":"scene","effects":{},
            "actions":{"play":{"state":"SomeState","restart":true,"enter":[
              {"op":"timeline_play","source":"scene","key":"scene","path":"scene.playable"}]}}})"));
        plan.visual_program=input;
        auto configured=session_services;std::shared_ptr<framework::VisualProgramPlayer> player;
        const auto begins_before=begins;int preparations=0;bool scene_alive=false;
        configured.visual_program=[&](api::EntityOrigin owner,const framework::VisualProgramInput& value,const auto&) {
            Check(owner.token.entity==entity_ptr && owner.world==world_ptr && begins==begins_before);++preparations;
            framework::VisualProgramServices callbacks;
            callbacks.effects=std::make_shared<api::PresentationEffects>(api::EffectRequestServices{{},api::EffectAccess({}),{},{},{}});
            callbacks.create=[](auto&,const auto&,const auto&)->framework::VisualEffectId{throw std::logic_error("unexpected effect");};
            callbacks.timeline=[&](auto,const auto&){scene_alive=true;return framework::VisualStepResult{framework::VisualStepState::Done,{}};};
            callbacks.cancel_timelines=[&](auto){scene_alive=false;};callbacks.timelines_drained=[&]{return !scene_alive;};
            player=std::make_shared<framework::VisualProgramPlayer>(value.program,std::move(callbacks));return player;
        };
        framework::CharacterSession session(world_ptr,std::move(plan),configured);
        session.Create(create);session.Queue(Domain::Alternate,framework::FreeStyleState{"SomeState"});
        session.Service(frame,appearance,types);
        Check(preparations==1 && player && player->Status().playing && scene_alive);
        session.Queue(Domain::Exploration,framework::NpcMotion{18});session.Service(frame,appearance,types);
        Check(!scene_alive && preparations==1);
        session.Queue(Domain::Alternate,framework::FreeStyleState{"SomeState"});session.Service(frame,appearance,types);
        Check(scene_alive && preparations==1); // retained player, new model/play receipt
        session.Remove();session.Service(frame,appearance,types);Check(session.Status().closed && player->Drained() && roots.empty());
    }
    {
        reset_baseline();framework::CharacterPlan plan;plan.package=package;
        plan.alternate=framework::BattlePresentation{{"battle.prefab",{},{}},{}};
        plan.visual_program=std::make_shared<framework::VisualProgramInput>();
        auto configured=session_services;int attempts=0;
        configured.visual_program=[&](api::EntityOrigin,const auto&,const auto&)->std::shared_ptr<framework::VisualProgramPlayer>{
            ++attempts;throw std::runtime_error("program assembly failed");
        };
        framework::CharacterSession session(world_ptr,std::move(plan),configured);
        session.Create(create);session.Queue(Domain::Alternate,framework::FreeStyleState{"SomeState"});
        const auto plays=battle_plays;session.Service(frame,appearance,types);
        Check(attempts==1 && battle_plays==plays && !session.Status().errors.empty());
        session.Service(frame,appearance,types);Check(attempts==1 && session.Status().closed && roots.empty());
    }
    {
        reset_baseline(); framework::CharacterPlan plan; plan.package=package;
        plan.alternate=framework::BattlePresentation{{"battle.prefab",{},{}},{}};
        framework::CharacterSession session(world_ptr,std::move(plan),session_services);
        session.Queue(Domain::Alternate,framework::FreeStyleState{"SomeState"}); session.Create(create);
        const auto count=battle_plays,begin_count=begins;
        load_state=4; session.Service(frame,appearance,types);
        Check(session.Status().phase==framework::SessionPhase::PreparingBaseline && begins==begin_count && !session.Status().baseline_attempted);
        load_state=5; session.Service(foreign,appearance,types); Check(!session.Status().baseline_attempted);
        session.Service(frame,appearance,types);
        Check(session.Status().alternate_available && battle_plays==count+1 && begins==begin_count+1);
        session.Queue(Domain::Exploration,framework::NpcMotion{18}); session.Service(frame,appearance,types);
        Check(current_path==&baseline_path && current_config==&baseline_entity_config && consumer.paths==&baseline_events);
        session.Remove(); session.Service(frame,appearance,types); Check(session.Status().closed && roots.empty());
    }
    {
        reset_baseline(); framework::CharacterPlan plan; plan.package=package;
        plan.alternate=framework::BattlePresentation{{"battle.prefab",{},{}},{}};
        framework::CharacterSession session(world_ptr,std::move(plan),session_services);
        session.Create(create); session.Queue(Domain::Exploration,framework::NpcMotion{18});
        constructor_fault=true; const auto count=npc_plays,ctor_count=initializes;
        session.Service(frame,appearance,types);
        Check(session.Status().phase==framework::SessionPhase::Active && !session.Status().alternate_available && npc_plays==count+1);
        Check(!session.Status().errors.empty() && initializes==ctor_count+1);
        session.Service(frame,appearance,types); Check(initializes==ctor_count+1 && npc_plays==count+1);
        constructor_fault=false; session.Remove(); session.Service(frame,appearance,types); Check(session.Status().closed && roots.empty());
    }
    {
        reset_baseline(); framework::CharacterPlan plan; plan.package=package;
        plan.alternate=framework::BattlePresentation{{"battle.prefab",{},{}},{}};
        framework::CharacterSession session(world_ptr,std::move(plan),session_services);
        session.Create(create); load_state=4; session.Service(frame,appearance,types);
        api::TrackedEntities().Disposed(api::TrackedEntities().Current(entity_ptr));
        session.Service(foreign,appearance,types); Check(session.Status().closed && roots.empty());
        load_state=5;
    }
    {
        framework::CharacterPlan plan; plan.package=package;
        framework::CharacterSession session(world_ptr,std::move(plan),session_services);
        session.Remove(); session.Service(frame,appearance,{}); Check(session.Status().closed && roots.empty());
    }
    std::cout<<"CharacterSession passed: prepared sources, no battle prerequisite for NPC, baseline capture before handoff, pending load, independent failure, natural disposal and removal; local fixtures only.\n";
    {
        reset_baseline();framework::CharacterPlan plan;plan.package=package;
        framework::SceneCharacters registry(session_services,[&](auto& session,const auto&,const auto&){session.Create(create);});
        const framework::SceneAddress target_scene{"Map/Targets",1};
        auto current=frame;current.manager={create.manager,{true,0}};current.battle={false,{true,0}};
        const auto target=registry.Add(target_scene.key,plan);
        auto input=std::make_shared<framework::VisualProgramInput>();input->target_characters.emplace("Target",target);
        plan.visual_program=input;
        const auto visual_source=registry.Add(target_scene.key,plan);registry.SetEnabled(visual_source,false);
        const auto resolve_target=registry.Plan(visual_source).visual_program->resolve_character;
        Check(registry.Plan(visual_source).visual_program->character==visual_source && input->character==0);
        Check(resolve_target && !resolve_target(target).token.entity && !resolve_target(999).token.entity);
        registry.Service(current,target_scene,appearance,{});
        const auto first=resolve_target(target);Check(first.token.entity==entity_ptr && first.world==world_ptr);
        registry.Rebuild(target);Check(!resolve_target(target).token.entity);
        registry.Service(current,target_scene,appearance,{});
        const auto second=resolve_target(target);Check(second.token.entity==entity_ptr && second.token.generation!=first.token.generation);
        registry.SetEnabled(target,false);Check(!resolve_target(target).token.entity);
        registry.Service(current,target_scene,appearance,{});
        registry.Delete(target);registry.Delete(visual_source);registry.Service(current,target_scene,appearance,{});
        Check(!resolve_target(target).token.entity && registry.Ids().empty() && roots.empty());
        registry.Shutdown();registry.Service(current,target_scene,appearance,{});Check(registry.Closed());
    }
    {
        reset_baseline();
        auto scene_frame=frame; scene_frame.manager={create.manager,{true,0}}; scene_frame.battle={false,{true,0}};
        auto away=scene_frame; away.world=foreign.world;
        framework::CharacterPlan plan; plan.package=package;
        framework::SceneCharacters registry(session_services,[&](auto& session,const auto& current,const auto& placement) {
            Check(current.world==world_ptr && placement.id==1 && placement.placement.position.x==12);
            session.Create(create);
        });
        const auto id=registry.Add("Map/A",plan,framework::PlacedAction{Domain::Exploration,framework::NpcMotion{18}},{{12,3,4},{0,0,0,1}});
        std::vector<api::OperationTraceReport> timings;
        registry.ConfigureTiming([&](const auto& report){timings.push_back(report);});registry.EnableTiming(true);
        const framework::SceneAddress a{"Map/A",1},b{"Map/B",2},returned{"Map/A",3};
        auto count=npc_plays;
        registry.Service(scene_frame,a,appearance,{});
        Check(registry.Status(id).active && registry.Status(id).creations==1 && npc_plays==count+1);
        Check(registry.Status(id).action->id==1 && registry.Status(id).action->creation==1 && registry.Status(id).action->phase==framework::ActionIntentPhase::Returned);
        registry.Service(scene_frame,std::nullopt,appearance,{});
        const auto timing_count=timings.size();
        registry.Service(scene_frame,framework::SceneAddress{},appearance,{});
        Check(timings.size()==timing_count); // Stable frames do not collect native calls.
        Check(registry.Status(id).active && registry.Status(id).creations==1); // missing identity is not map departure
        const auto superseded=registry.Select(id,{Domain::Exploration,framework::AnimatorState{"OtherState"}});
        const auto action_id=registry.Select(id,{Domain::Exploration,framework::AnimatorState{"SomeState"}});
        Check(action_id>superseded && registry.Status(id).action->id==action_id && registry.Status(id).action->phase==framework::ActionIntentPhase::Waiting);
        registry.Service(scene_frame,a,appearance,{});
        Check(registry.Status(id).action->id==action_id && registry.Status(id).action->phase==framework::ActionIntentPhase::Returned);
        registry.Service(away,b,appearance,{});
        Check(!registry.Status(id).active && registry.Status(id).retiring==1);
        Check(registry.Status(id).action->id==action_id && registry.Status(id).action->phase==framework::ActionIntentPhase::Waiting && !registry.Status(id).action->outcome);
        api::TrackedEntities().Disposed(api::TrackedEntities().Current(entity_ptr));
        registry.Service(away,b,appearance,{}); Check(registry.Status(id).retiring==0 && roots.empty());
        auto animator_count=animator_plays;
        registry.Service(scene_frame,returned,appearance,{});
        Check(registry.Status(id).creations==2 && animator_plays==animator_count+1); // replay latest selected action, from its request start
        Check(std::any_of(timings.begin(),timings.end(),[&](const auto& r){return r.subject==id && r.generation==1 && r.rows[0].stage=="character.retire";}));
        Check(std::any_of(timings.begin(),timings.end(),[&](const auto& r){return r.subject==id && r.generation==2 && r.rows[0].stage=="character.create";}));
        registry.EnableTiming(false);const auto stopped_timings=timings.size();
        Check(registry.Status(id).action->id==action_id && registry.Status(id).action->creation==2 && registry.Status(id).action->phase==framework::ActionIntentPhase::Returned);
        registry.SetEnabled(id,false); registry.Service(scene_frame,returned,appearance,{});
        Check(!registry.Status(id).active && roots.empty());
        registry.Service(away,b,appearance,{}); registry.Service(scene_frame,returned,appearance,{});
        Check(registry.Status(id).creations==2); // manually removed stays absent
        registry.SetEnabled(id,true); registry.Service(scene_frame,returned,appearance,{});
        Check(registry.Status(id).creations==3);
        registry.Rebuild(id); registry.Service(scene_frame,returned,appearance,{});
        Check(registry.Status(id).creations==4 && registry.Status(id).active);
        Check(timings.size()==stopped_timings); // Disabling collection leaves lifecycle behavior unchanged.
        registry.Look(id,{framework::LookMode::Point,0,{8,9,10}});
        registry.Select(id,{Domain::Exploration,framework::NpcMotion{18}});
        auto variant=plan;variant.package.id="another-variant";
        const auto before_npc=npc_plays,before_animator=animator_plays;
        registry.ReplacePlan(id,variant);
        variant.package.id="latest-variant";registry.ReplacePlan(id,variant);
        registry.Service(scene_frame,returned,appearance,{});
        Check(registry.Ids()==std::vector<std::uint64_t>{id} && registry.Status(id).creations==5);
        Check(registry.Status(id).package.id=="latest-variant" && registry.Status(id).scene=="Map/A");
        Check(registry.Status(id).placement.position.x==12 && registry.Status(id).look.point.z==10);
        Check(npc_plays==before_npc && animator_plays==before_animator); // no old-source action replay
        Check(!registry.Status(id).action);
        registry.SetEnabled(id,false);registry.Service(scene_frame,returned,appearance,{});
        registry.ReplacePlan(id,plan);registry.Service(scene_frame,returned,appearance,{});
        Check(!registry.Status(id).enabled && !registry.Status(id).active && registry.Status(id).creations==5);
        registry.SetEnabled(id,true);registry.Service(scene_frame,returned,appearance,{});
        Check(registry.Status(id).creations==6 && registry.Status(id).look.point.z==10);
        registry.Delete(id);
        Check(registry.Status(id).deleting && !registry.Status(id).enabled);
        bool gone=false;try{registry.Rebuild(id);}catch(const std::logic_error&){gone=true;}Check(gone);
        registry.Service(away,b,appearance,{});
        Check(registry.Contains(id) && registry.Status(id).deleting && registry.Status(id).retiring==1);
        registry.Service(scene_frame,returned,appearance,{});
        Check(!registry.Contains(id) && registry.Ids().empty() && roots.empty());
        registry.Service(away,b,appearance,{});registry.Service(scene_frame,returned,appearance,{});
        Check(registry.Ids().empty()); // deletion cannot return on revisiting a map
        registry.Shutdown(); registry.Service(scene_frame,returned,appearance,{});
        Check(registry.Closed() && roots.empty());
    }
    {
        reset_baseline(); auto scene_frame=frame;
        scene_frame.manager={create.manager,{true,0}}; scene_frame.battle={false,{true,0}};
        framework::CharacterPlan plan; plan.package=package;
        bool callback_fault=true,disable_in_callback=false;
        std::uint64_t selected_id=0; framework::SceneCharacters* active_registry=nullptr;
        framework::SceneCharacters registry(session_services,[&](auto& session,const auto&,const auto&) {
            session.Create(create);
            if(callback_fault) throw std::runtime_error("failure after actual creation");
            if(disable_in_callback) active_registry->SetEnabled(selected_id,false);
        }); active_registry=&registry;
        const framework::SceneAddress scene_address{"Map/A",1};
        selected_id=registry.Add("Map/A",plan);
        registry.Service(scene_frame,scene_address,appearance,{});
        Check(!registry.Status(selected_id).active && registry.Status(selected_id).retiring==1 && !registry.Status(selected_id).error.empty());
        Check(registry.Status(selected_id).creation_error=="failure after actual creation");
        registry.Service(scene_frame,scene_address,appearance,{});
        Check(registry.Status(selected_id).creations==1 && registry.Status(selected_id).retiring==0 && roots.empty());
        callback_fault=false; disable_in_callback=true; registry.Rebuild(selected_id);
        Check(registry.Status(selected_id).creation_error.empty());
        registry.Service(scene_frame,scene_address,appearance,{});
        Check(!registry.Status(selected_id).enabled && !registry.Status(selected_id).active && registry.Status(selected_id).retiring==1);
        registry.Service(scene_frame,scene_address,appearance,{}); Check(roots.empty());
        registry.Shutdown(); Check(registry.Closed());
    }
    std::cout<<"Scene character scheduling passed: source/session composition, independent scene intent, return replay, manual removal, explicit rebuild, partial creation ownership and callback cancellation; supplied scene identities, no game bootstrap.\n";
    {
        reset_baseline();
        auto current=frame;current.manager={create.manager,{true,0}};current.battle={false,{true,0}};
        current.adventure_phase={&map_phase,{true,0}};
        api::PresentationBindings bindings;bindings.factory=M(Create);bindings.references=references;
        bindings.entity=entity_methods;bindings.model=model_methods;bindings.profile_inputs=input_methods;
        bindings.profile=profile_methods;bindings.profile_events=fields;bindings.events=event_methods;
        bindings.animation=action_methods;bindings.scene.string_equals=M(Equals);
        bindings.arrays={M(Array),{},{},M(Set),{}};
        bindings.world_map.group_manager=M(Group);bindings.world_map.map=M(Map);
        for(auto& part:bindings.world_map.identity) part=M(Key);
        bindings.placement.player=M(+[](void*,void*)->void*{return entity_ptr;});
        bindings.placement.owner_world=M(EntityWorld);
        bindings.placement.game_object=M(+[](void*,void*)->void*{return &art;});
        bindings.placement.transform=M(+[](void*,void*)->void*{return &art;});
        bindings.placement.position=M(+[](void*,api::Position3* out,void*){*out={10,20,30};});
        bindings.placement.rotation=M(+[](void*,api::Rotation4* out,void*){*out={0,1,0,0};});
        framework::FrameworkHost host;
        int catalog_reads=0,motion_reads=0;
        framework::CatalogReaders catalog;
        catalog.characters=[&]{++catalog_reads;api::CharacterCatalog out;out.npcs.rows={{77,"Npc/config","Npc/model"}};out.npcs.enumerated=true;return out;};
        catalog.document=[](auto kind,const std::string& path) {
            api::ActionConfigInput out;out.kind=kind;out.path=path;out.loaded=true;
            out.states.push_back({path,"state"});return out;
        };
        catalog.npc_motions=[&](void* component){Check(component==&style);++motion_reads;api::NpcMotionSources out;api::NpcMotionRow row;row.id=18;out.motions.push_back(row);return out;};
        host.Prepare(objects,bindings,{true,true,false},[&](auto& session,const auto&,const auto& placement){
            Check(placement.placement.position.x==12 && placement.placement.position.y==20 &&
                placement.placement.position.z==30 && placement.placement.rotation.y==1);
            session.Create(create);
        },catalog);
        framework::CharacterPlan plan;plan.package=package;
        const auto plays=npc_plays;
        std::thread ui([&]{host.Application().Post(framework::PrepareSource{framework::BrowseNpc{77}});});ui.join();
        Check(catalog_reads==0 && !host.Application().Snapshot().catalog);
        host.Frame(current,false);auto selection_replies=host.Application().TakeReplies();
        const auto selection=selection_replies[0].source_request;
        Check(selection_replies[0].accepted && selection && catalog_reads==1 && host.Application().Snapshot().characters.empty());
        host.Frame(current,false);Check(host.Application().Snapshot().catalog->Source(selection)->result.finished);
        // The menu's selected table row now flows through real preparation,
        // plan assembly, command queue, registry and session, not a preset plan.
        const auto before_create_cursor=host.Application().ReadUpdates().changes.cursor;
        std::thread create_ui([&]{host.Application().Post(framework::AddPreparedCharacter{selection,"",framework::PlacedAction{Domain::Exploration,framework::NpcMotion{18}}, {},{}, {},true});});create_ui.join();
        Check(npc_plays==plays && host.Application().Snapshot().characters.empty()); // UI is data only
        Check(!host.Frame(current,false));
        auto replies=host.Application().TakeReplies();Check(replies.size()==1 && replies[0].accepted && replies[0].character==1);
        const auto create_operation=replies[0].operation;
        const auto created_updates=host.Application().ReadUpdates(before_create_cursor);
        Check(!created_updates.changes.reset && !created_updates.changes.events.empty());
        Check(created_updates.changes.events.back().characters==std::vector<std::uint64_t>{1});
        Check(std::find(created_updates.changes.events.back().operations.begin(),created_updates.changes.events.back().operations.end(),create_operation)!=
            created_updates.changes.events.back().operations.end());
        Check(created_updates.snapshot.characters[0].active);
        Check(create_operation==replies[0].serial && host.Application().Snapshot().operations.entries.at(create_operation).phase==framework::OperationPhase::Returned);
        Check(host.Application().Snapshot().scene->key=="hsr-map-v1:2:2:2:2:2:2" && host.Application().Snapshot().characters[0].active && npc_plays==plays+1);
        host.Application().Post(framework::ReadCharacterNpcActions{1});host.Frame(current,false);
        Check(host.Application().TakeReplies()[0].accepted && motion_reads==1 && host.Application().Snapshot().npc_actions.at(1)->source.motions[0].id==18);
        host.Application().Post(framework::PrepareSource{framework::BrowseNpc{77}});
        host.Application().Post(framework::ReleaseSource{selection});
        host.Application().Post(framework::AddPreparedCharacter{selection,"",{},{},{},{}});host.Frame(current,false);
        auto stale_replies=host.Application().TakeReplies();Check(stale_replies[0].accepted && stale_replies[1].accepted && !stale_replies[2].accepted && host.Application().Snapshot().characters.size()==1);
        const auto current_selection=stale_replies[0].source_request;
        host.Application().Post(framework::SelectCharacterAction{999,{Domain::Exploration,framework::NpcMotion{18}}});
        host.Application().Post(framework::SelectCharacterAction{1,{Domain::Exploration,framework::AnimatorState{"SomeState"}}});
        const auto anim=animator_plays;host.Frame(current,false);replies=host.Application().TakeReplies();
        Check(replies.size()==2 && !replies[0].accepted && replies[1].accepted && animator_plays==anim+1);
        Check(replies[1].action_intent && host.Application().Snapshot().characters[0].action->id==replies[1].action_intent &&
            host.Application().Snapshot().characters[0].action->phase==framework::ActionIntentPhase::Returned);
        Check(host.Application().Snapshot().operations.entries.at(replies[0].operation).phase==framework::OperationPhase::Failed);
        Check(host.Application().Snapshot().operations.entries.at(replies[1].operation).phase==framework::OperationPhase::Returned);
        const auto first_action=host.Application().Post(framework::SelectCharacterAction{1,{Domain::Exploration,framework::AnimatorState{"Old"}}});
        const auto last_action=host.Application().Post(framework::SelectCharacterAction{1,{Domain::Exploration,framework::AnimatorState{"SomeState"}}});
        host.Frame(current,false);host.Application().TakeReplies();
        Check(host.Application().Snapshot().operations.entries.at(first_action).phase==framework::OperationPhase::Superseded);
        Check(host.Application().Snapshot().operations.entries.at(last_action).phase==framework::OperationPhase::Returned);
        // Non-menu clients receive only their own value replies. A callback
        // can read snapshots/queue later work without holding exchange_.
        std::vector<framework::CommandReply> controlled;
        const auto control_serial=host.Application().Post(framework::BrowseCharacterActions{1},[&](const auto& reply){
            controlled.push_back(reply);Check(host.Application().Snapshot().prepared);
            host.Application().Post(framework::BrowseCharacterActions{999},[&](const auto& result){controlled.push_back(result);});
        });
        host.Frame(current,false);
        Check(controlled.size()==1 && controlled[0].serial==control_serial && controlled[0].accepted && host.Application().TakeReplies().empty());
        host.Frame(current,false);Check(controlled.size()==2 && !controlled.back().accepted && host.Application().TakeReplies().empty());
        host.Application().Post(framework::BrowseCharacterActions{1},[](const auto&){throw std::runtime_error("test reply fault");});
        host.Application().Post(framework::BrowseCharacterActions{1});host.Frame(current,false);
        Check(host.Application().TakeReplies().size()==1); // reply receiver failure doesn't stop the other command
        // Map input comes through WorldMap/SceneIdentity, not injected addresses.
        map_floor=3;host.Frame(current,false);Check(!host.Application().Snapshot().characters[0].active);
        Check(host.Application().Snapshot().operations.entries.at(create_operation).phase==framework::OperationPhase::Returned);
        map_floor=2;host.Frame(current,false);Check(host.Application().Snapshot().characters[0].active && host.Application().Snapshot().characters[0].creations==2);
        host.Application().Post(framework::EnableCharacter{1,false});host.Frame(current,false);
        Check(!host.Application().Snapshot().characters[0].active);host.Application().Post(framework::RebuildCharacter{1});host.Frame(current,false);
        Check(host.Application().Snapshot().characters[0].creations==3);
        Check(host.Application().TakeReplies().size()==2);
        host.Application().Post(framework::ReplacePreparedCharacter{1,selection,{},{}});host.Frame(current,false);
        Check(!host.Application().TakeReplies().back().accepted && host.Application().Snapshot().characters[0].creations==3);
        const auto previous_npc=npc_plays,previous_animator=animator_plays;
        host.Application().Post(framework::ReplacePreparedCharacter{1,current_selection,{},{}});host.Frame(current,false);
        Check(host.Application().TakeReplies().back().accepted && host.Application().Snapshot().characters.size()==1 && host.Application().Snapshot().characters[0].creations==4);
        Check(host.Application().Snapshot().npc_actions.count(1)==0 && host.Application().Snapshot().catalog->Actions(1));
        Check(npc_plays==previous_npc && animator_plays==previous_animator);
        // Shared service addresses saved actors without changing the source
        // window or the observed model. They are on another map, so accepting
        // an action must not pretend it has already played.
        auto remote_plan=plan;remote_plan.action_sources={{Domain::Exploration,{api::ActionConfigKind::Adventure,"RemoteAction"}}};
        host.Application().Post(framework::AddCharacter{remote_plan,"another-map",{}, {}});host.Frame(current,false);
        const auto remote_id=host.Application().TakeReplies().back().character;
        const auto old_gaze=host.Application().Post(framework::LookCharacter{remote_id,{framework::LookMode::Point,0,{1,2,3}}});
        const auto new_gaze=host.Application().Post(framework::LookCharacter{remote_id,{framework::LookMode::Player}});
        const auto unavailable_place=host.Application().Post(framework::PlaceCharacter{remote_id,{},false});
        host.Frame(current,false);auto controls=host.Application().TakeReplies();
        Check(controls.size()==3 && controls[0].accepted && controls[1].accepted && !controls[2].accepted);
        Check(controls[0].operation==old_gaze && controls[1].operation==new_gaze && controls[2].operation==unavailable_place);
        Check(host.Application().Snapshot().operations.entries.at(old_gaze).phase==framework::OperationPhase::Superseded);
        Check(host.Application().Snapshot().operations.entries.at(new_gaze).phase==framework::OperationPhase::Waiting);
        Check(host.Application().Snapshot().operations.entries.at(unavailable_place).phase==framework::OperationPhase::Failed);
        host.Application().Post(framework::BrowseCharacterActions{remote_id});host.Frame(current,false);host.Application().TakeReplies();
        const auto remote_view=host.Application().Snapshot().catalog->Actions(remote_id);
        Check(remote_view && remote_view->actions.finished);
        const framework::CatalogActionSelection remote_action{remote_view->revision,framework::ConfiguredActionKey{Domain::Exploration,"RemoteAction"}};
        host.Application().Post(framework::SelectCatalogAction{remote_id,remote_action});
        host.Application().Post(framework::SelectCatalogAction{1,remote_action});host.Frame(current,false);
        const auto resolved=host.Application().TakeReplies();Check(resolved.size()==2 && resolved[0].accepted && !resolved[1].accepted);
        Check(resolved[0].action_intent && host.Application().Snapshot().characters.back().action->id==resolved[0].action_intent &&
            host.Application().Snapshot().characters.back().action->phase==framework::ActionIntentPhase::Waiting);
        Check(host.Application().Snapshot().operations.entries.at(resolved[0].operation).phase==framework::OperationPhase::Waiting);
        Check(host.Application().Snapshot().catalog->Source(current_selection)->phase==framework::SourceRequestPhase::Ready && !host.Application().Snapshot().characters.back().active && animator_plays==previous_animator);
        bool control_cancelled=false;
        host.Application().Post(framework::RebuildCharacter{1},[&](const auto& reply){control_cancelled=!reply.accepted && !reply.error.empty();});
        host.Application().Post(framework::RebuildCharacter{1});Check(host.Frame(current,true) && control_cancelled);
        replies=host.Application().TakeReplies();Check(replies.size()==1 && !replies.back().accepted && host.Application().Snapshot().closed && roots.empty());
        Check(host.Application().Snapshot().operations.entries.at(resolved[0].operation).phase==framework::OperationPhase::Cancelled);
        Check(host.Application().Snapshot().operations.entries.at(replies.back().operation).phase==framework::OperationPhase::Cancelled);
        Check(host.Application().Snapshot().operations.entries.at(new_gaze).phase==framework::OperationPhase::Cancelled);
        bool rejected=false;try{host.Application().Post(framework::RebuildCharacter{1});}catch(const std::logic_error&){rejected=true;}Check(rejected);
        // Missing entity notifications are a concrete ownership dependency,
        // but no optional event/texture capability was needed by the test above.
        framework::FrameworkHost missing;
        missing.Prepare(objects,bindings,{},[&](auto&,const auto&,const auto&){Check(false);});
        missing.Application().Post(framework::AddCharacter{plan,"saved",{}, {}});missing.Frame(current,false);
        Check(!missing.Application().TakeReplies()[0].accepted && missing.Application().Snapshot().characters.empty());
        Check(missing.Frame(current,true) && roots.empty());
        std::cout<<"Framework host integration passed: UI value queue, official map getters, session creation/action/return/rebuild, independent command failure and asynchronous drain; local only.\n";
    }
    TestGazeHandoff(frame,create,session_services,references,entity_methods,package,appearance,types);
    {
        reset_baseline(); auto scene_frame=frame;
        scene_frame.manager={create.manager,{true,0}}; scene_frame.battle={false,{true,0}};
        framework::SceneCharacters registry(session_services,[&](auto& session,const auto&,const auto&) { session.Create(create); });
        framework::CharacterPlan invalid; invalid.package=package; invalid.package.source=framework::SourceKind::External;
        const auto bad=registry.Add("A",invalid); // missing payload: construction throws before any game call
        framework::CharacterPlan valid; valid.package=package;
        const auto good=registry.Add("A",valid,framework::PlacedAction{Domain::Exploration,framework::NpcMotion{18}});
        const auto plays=npc_plays;
        registry.Service(scene_frame,framework::SceneAddress{"A",1},appearance,{});
        Check(!registry.Status(bad).error.empty() && registry.Status(good).active && npc_plays==plays+1);
        registry.Shutdown(); registry.Service(scene_frame,framework::SceneAddress{"A",1},appearance,{});
        Check(registry.Closed() && roots.empty());
    }
    {
        reset_baseline();auto current=frame;current.sequence=1000;
        current.manager={create.manager,{true,0}};current.battle={false,{true,0}};
        framework::CharacterPlan plan;plan.package=package;plan.source_intent=framework::BrowseNpc{77};
        plan.alternate=framework::BattlePresentation{{"battle.prefab",{},{}},{true,"OtherState",{}}};
        framework::SceneCharacters registry(session_services,[&](auto& session,const auto&,const auto&){session.Create(create);});
        const framework::SceneAddress layout_scene{"layout-map",1};
        const auto id=registry.Add(layout_scene.key,plan,framework::PlacedAction{Domain::Alternate,framework::FreeStyleState{"SomeState"}});
        observed_hash=41;observed_time=0;
        registry.Service(current,layout_scene,appearance,types);
        ++current.sequence;registry.Service(current,layout_scene,appearance,types);
        Check(registry.Status(id).handoff->phase==Handoff::Ready);
        const auto intent=registry.Status(id).action->id;
        observed_hash=37;observed_time=1.1f;++current.sequence;registry.Service(current,layout_scene,appearance,types);
        Check(std::get<framework::FreeStyleState>(registry.DesiredAction(id)->request).name=="OtherState");
        Check(registry.Status(id).action->id==intent); // historical command identity remains unchanged
        const auto layout=framework::CaptureSceneLayout(registry,layout_scene.key);
        Check(std::get<framework::SavedConfiguredAction>(*layout.characters.front().action).state=="OtherState");
        registry.Select(id,{Domain::Alternate,framework::FreeStyleState{"SomeState"}});
        observed_time=0;++current.sequence;registry.Service(current,layout_scene,appearance,types);
        Check(std::get<framework::FreeStyleState>(registry.DesiredAction(id)->request).name=="SomeState");
        registry.Shutdown();registry.Service(current,layout_scene,appearance,types);
        Check(registry.Closed() && roots.empty());
    }
}
