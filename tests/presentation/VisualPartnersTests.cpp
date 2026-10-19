#include <hksr/framework/presentation/VisualPartners.hpp>
#include <hksr/animation/AnimationControl.hpp>
#include <hksr/model/ProfileInputs.hpp>
#include <cstring>
#include <array>
#include <functional>
#include <map>
#include <stdexcept>

namespace {
using namespace hksr::api;using namespace hksr::framework;
void CheckAt(bool ok,int line){if(!ok)throw std::runtime_error("visual partners test failed at "+std::to_string(line));}
#define Check(x) CheckAt((x),__LINE__)
int world_data,other_world_data,manager_data,info,definition,row;
auto* world=reinterpret_cast<GameWorld*>(&world_data);
struct Group {void* header[2]{};GameEntity* owner=nullptr;};
struct Actor {bool loaded=false;std::array<Group,6> components;bool animation=true;float progress=0;};
Actor parent;std::vector<std::unique_ptr<Actor>> actors;
GameEntity* E(Actor& a){return reinterpret_cast<GameEntity*>(&a);}
unsigned factories=0,kills=0,names=0,attachments=0,animations=0,visibilities=0;
int root_id=0;std::map<int,void*> roots;std::array<int,6> types{};
unsigned idle_requests=0;
std::function<void()> on_create,on_name;
bool delay_disposal=false;
std::vector<EntityToken> disposals;
template<class T>Method M(T f){return {reinterpret_cast<void*>(f),&info};}
int __fastcall Retain(void* obj,int,int,void*){roots[++root_id]=obj;return root_id;}
void* __fastcall Target(int id,void*){return roots.at(id);}
void __fastcall Free(int id,void*){roots.erase(id);}
GameWorld* __fastcall Owner(GameEntity*,void*){return world;}
bool __fastcall Loaded(GameEntity* e,void*){return reinterpret_cast<Actor*>(e)->loaded;}
unsigned __fastcall Id(GameEntity*,void*){return 1;}
void __fastcall Immediate(GameEntity*,bool,void*){}
void Dispose(EntityToken token){TrackedEntities().DisposalStarting(token);TrackedEntities().Disposed(token);}
void __fastcall Kill(GameWorld* w,GameEntity* e,GameEntity*,void*) {
    Check(w==world);++kills;auto token=TrackedEntities().Current(e);
    if(delay_disposal)disposals.push_back(token);else Dispose(token);
}
GameEntity* __fastcall MapNpc(EntityManager*,MapNpcDefinition*,NpcDataRow*,EntityCallback*,bool,void*){return E(parent);}
GameEntity* __fastcall Partner(EntityManager*,GameEntity* summoner,void* model,void* config,void*) {
    Check(summoner==E(parent) && *static_cast<std::string*>(model)=="partner.prefab" && *static_cast<std::string*>(config)=="partner.json");
    ++factories;
    auto actor=std::make_unique<Actor>();for(auto& component:actor->components)component.owner=E(*actor);
    auto* result=E(*actor);actors.push_back(std::move(actor));
    EntityCreationScope::FactoryCompleted(world,result);
    auto anchor=std::make_unique<Actor>();auto* auxiliary=E(*anchor);actors.push_back(std::move(anchor));
    EntityCreationScope::FactoryCompleted(world,auxiliary);
    if(on_create)on_create();return result;
}
void* __fastcall Component(GameEntity* e,void* type,void*) {
    for(std::size_t i=0;i<types.size();++i)if(type==&types[i]) {
        auto* actor=reinterpret_cast<Actor*>(e);
        return i==4 && !actor->animation?nullptr:&actor->components[i];
    }
    throw std::runtime_error("unknown component type");
}
GameEntity* __fastcall Leader(void*,void*){return E(parent);}
void __fastcall Name(void*,void* name,void*){Check(*static_cast<std::string*>(name)=="member");++names;if(on_name)on_name();}
void __fastcall Attach(void*,void*,void*){++attachments;}
void __fastcall Cancel(void*,void*){}
void __fastcall Anim(void*,GameEntity*,void*){++animations;}
void __fastcall Follow(void*,GameEntity*,VisibilityFollow,void*){++visibilities;}
void __fastcall Visible(void*,bool,const void*,void*){++visibilities;}
void __fastcall Idle(void*,int logic,void* state,float start,float transition,float end,int extra,unsigned request,bool flag,void*) {
    Check(logic==1 && *static_cast<std::string*>(state)=="Loop" && start==0 && transition==0.25f && end==1 && !extra && !request && !flag);
    ++idle_requests;
}
void* __fastcall Animator(Group* model,void*){return model->owner;}
bool __fastcall Changing(void*,void*){return false;}
bool __fastcall Initialized(void*,void*){return true;}
int __fastcall Hash(std::string* name,void*){Check(*name=="Loop");return 42;}
bool __fastcall Has(void* actor,int layer,int hash,void*){Check(actor!=E(parent) && layer==0 && hash==42);return true;}
void __fastcall StateInfo(Actor* actor,int layer,int index,unsigned char* bytes,void*) {
    Check(actor!=&parent && layer==0 && index==0);std::memset(bytes,0,36);int hash=42;
    std::memcpy(bytes,&hash,4);std::memcpy(bytes+12,&actor->progress,4);
}
bool __fastcall Transition(void*,int,void*){return false;}
struct Objects:ManagedObjects {
    std::vector<std::unique_ptr<std::string>> strings;
    ObjectResult NewStringUtf8(const char* s)override{strings.push_back(std::make_unique<std::string>(s));return {{true,0},strings.back().get()};}
    ObjectResult AllocateObject(const char*,const char*)override{throw std::runtime_error("no object allocation expected");}
    ObjectResult ManagedType(const char*,const char* name)override {
        const std::array<std::string,6> type_names{"OLABFALNLJM","TransformComponent","EAKGBLEMBLN","CharacterVisibleComponent","CFFPEAALCPH","CharacterModelComponent"};
        for(std::size_t i=0;i<type_names.size();++i)if(name==type_names[i])return {{true,0},&types[i]};
        throw std::runtime_error("unknown type");
    }
};
}
void TestVisualPartners() {
    using namespace hksr::api;using namespace hksr::framework;
    Objects objects;EntityOwner owner;
    for(auto& component:parent.components)component.owner=E(parent);
    const ReferenceMethods refs{M(Retain),M(Target),M(Free),{}, {}};
    EntityAccessMethods access;access.owner_world=M(Owner);access.runtime_id=M(Id);access.loaded=M(Loaded);
    access.kill_immediately=M(Immediate);access.make_die=M(Kill);
    EntityCreationSession main(owner,M(MapNpc),access,refs);
    main.Create(NpcCreateInput{world,reinterpret_cast<EntityManager*>(&manager_data),reinterpret_cast<MapNpcDefinition*>(&definition),reinterpret_cast<NpcDataRow*>(&row),false});
    const EntityOrigin origin{owner.OwnedToken(E(parent)),world};
    const auto main_roots=roots.size();
    PartnerControlMethods control;control.component=M(Component);control.owner_world=M(Owner);control.component_owner=16;
    control.group_leader=M(Leader);control.member_name=M(Name);control.attach=M(Attach);control.cancel_smooth_follow=M(Cancel);
    control.animation_follow=M(Anim);control.visibility_follow=M(Follow);control.visibility=M(Visible);
    control.idle_animation=M(Idle);
    VisualPartnerServices services;
    services.create=[&]{return std::make_unique<EntityCreationSession>(owner,Method{},Method{},&objects,access,refs,EntityCreationScope::BirthObserver{},M(Partner));};
    services.controls=[&](EntityToken child){return std::make_unique<PartnerControlSession>(owner,child,origin.token,world,control,refs,objects);};
    services.target=[&](auto&,const auto& name){Check(name=="Caster");return origin;};
    services.objects=&objects;
    services.animations=[&](EntityToken child) {
        auto entity_access=access;entity_access.component=M(Component);
        ModelAccessMethods model;model.animator=M(Animator);model.changing=M(Changing);
        AnimationControlMethods animation;animation.hash=M(Hash);animation.has_state=M(Has);
        animation.state_info=M(StateInfo);animation.in_transition=M(Transition);
        ProfileInputMethods inputs;inputs.animator_initialized=M(Initialized);
        return std::make_unique<ActorAnimationSession>(child,refs,EntityAccess(entity_access),ModelAccess(model),AnimationControl(animation),ProfileInputs(inputs));
    };
    auto effects=std::make_shared<PresentationEffects>(EffectRequestServices{refs,EffectAccess({}),{},{},{}});
    auto context=std::make_shared<VisualEffectContext>(effects),other=std::make_shared<VisualEffectContext>(effects);
    VisualPartners partners(origin,{{"helper",{"partner.prefab","partner.json","member"}}},services);
    WorldFrame frame;frame.world=world;frame.manager={reinterpret_cast<EntityManager*>(&manager_data),{true,0}};frame.disposing={false,{true,0}};
    const VisualPartnerOperation create{"helper",VisualPartnerCreate{}};
    Check(partners.Execute(context,create).state==VisualStepState::Pending && factories==0);
    partners.Service(frame);
    Check(partners.Execute(context,create).state==VisualStepState::Done && factories==1 && names==1);
    Check(partners.Execute(context,create).state==VisualStepState::Done && factories==1 && names==1);
    Check(partners.Execute(context,{"helper",VisualPartnerAnimationFollow{"Caster"}}).state==VisualStepState::Pending && animations==0);
    Check(partners.Execute(context,{"helper",PartnerIdleAnimation{"Loop",0,0.25f,1}}).state==VisualStepState::Pending && idle_requests==0);
    Check(partners.Execute(other,create).state==VisualStepState::Failed && factories==1);
    actors[0]->loaded=true;partners.Service(frame);
    Check(partners.Execute(context,create).state==VisualStepState::Done && factories==1);
    Check(partners.Resolve("helper").token.entity==E(*actors[0]));
    Check(partners.Execute(context,{"helper",VisualPartnerAttach{"Caster","EffectScale","Head",false,false,VisibilityFollow::None}}).state==VisualStepState::Done && attachments==1);
    parent.animation=false;
    const auto missing_source=partners.Execute(context,{"helper",VisualPartnerAnimationFollow{"Caster"}});
    Check(missing_source.state==VisualStepState::Failed && animations==0);
    parent.animation=true;
    Check(partners.Execute(context,{"helper",VisualPartnerAnimationFollow{"Caster"}}).state==VisualStepState::Done && animations==1);
    Check(partners.Execute(context,{"helper",PartnerVisibility{"show",false,true,false,false}}).state==VisualStepState::Done && visibilities==1);
    Check(partners.Execute(context,{"helper",PartnerIdleAnimation{"Loop",0,0.25f,1}}).state==VisualStepState::Done && idle_requests==1);
    auto endpoint=partners.Animation("helper");Check(endpoint && endpoint->entity_generation==partners.Resolve("helper").token.generation);
    auto playback=endpoint->submit({"Loop",0,0.25f});Check(!playback->accepted);
    ++frame.sequence;partners.Service(frame);Check(playback->accepted && !playback->entered && idle_requests==2);
    actors[0]->progress=0.4f;++frame.sequence;partners.Service(frame);Check(playback->entered && !playback->complete);
    actors[0]->progress=1.2f;++frame.sequence;partners.Service(frame);Check(playback->complete);
    endpoint->release(playback->serial);Check(playback->released);
    delay_disposal=true;partners.End(context);partners.Service(frame);Check(kills==2 && !partners.Drained());
    Check(!partners.Drained(context) && partners.Drained(other));
    partners.Service(frame);Check(kills==2);for(auto token:disposals)Dispose(token);disposals.clear();
    partners.Service(frame);Check(partners.Drained() && roots.size()==main_roots);delay_disposal=false;
    // Cancellation inside a factory still retains both new entities for removal.
    on_create=[&]{partners.End(context);};partners.Execute(context,create);on_create={};partners.Service(frame);
    Check(factories==2 && kills==4 && partners.Drained() && roots.size()==main_roots);
    // Changing worlds never sends an old-world command or retirement. Actual
    // disposal observations may still close the old receipt in the new world.
    partners.Execute(context,create);Check(factories==3);
    const auto child_token=owner.OwnedToken(E(*actors[4])),anchor_token=owner.OwnedToken(E(*actors[5]));
    auto next=frame;next.world=reinterpret_cast<GameWorld*>(&other_world_data);partners.Service(next);
    Check(partners.Execute(context,create).state==VisualStepState::Failed);
    partners.End(context);partners.Service(next);Check(kills==4 && !partners.Drained());
    Dispose(child_token);Dispose(anchor_token);partners.Service(next);Check(partners.Drained());
    // Parent retirement cancels loading children without waiting for a model.
    partners.Service(frame);partners.Execute(context,create);Dispose(origin.token);partners.Service(frame);
    Check(kills==6 && partners.Drained());main.Service(frame);Check(main.Status().closed && roots.empty());
}
