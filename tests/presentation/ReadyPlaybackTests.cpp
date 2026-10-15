#include <hksr/framework/presentation/ActionPlayback.hpp>
#include <Windows.h>
#include <map>
#include <memory>
#include <cstring>
#include <stdexcept>
#include <functional>
#include <cmath>
#include <array>

namespace {
using namespace hksr;
using namespace hksr::framework;
int entity,world,model,animator,style,model_type,style_type,parameter,info,next_root=0;
auto* owner=reinterpret_cast<api::GameEntity*>(&entity);
auto* own_world=reinterpret_cast<api::GameWorld*>(&world);
std::map<int,void*> roots;
std::vector<std::unique_ptr<std::string>> strings;
std::map<std::string,int> hashes{{"PoseA",11},{"PoseB",12},{"VariantB",13},{"EdgeAB",21},{"EdgeBA",22},{"Absent",99}};
std::vector<std::string> presentation_calls;
std::function<void()> evaluate_callback;
float hit_weight=0;
bool presentation=false;
int current=11,next=0,last_play=0,plays=0;float time=0,next_time=0,speed=0.8f;
bool transition=false,changing=false,setter_fault=false;
void CheckAt(bool value,int line){if(!value)throw std::runtime_error("ready playback test failed at line "+std::to_string(line));}
#define Check(...) CheckAt((__VA_ARGS__),__LINE__)
template<class T>api::Method M(T f){return {reinterpret_cast<void*>(f),&info};}
int __fastcall Keep(void* value,int,int,void*){roots[++next_root]=value;return next_root;}
void* __fastcall Target(int id,void*){return roots.at(id);}
void __fastcall Free(int id,void*){roots.erase(id);}
unsigned __fastcall Id(void*,void*){return 1;}
api::GameWorld* __fastcall World(void*,void*){return own_world;}
bool __fastcall Loaded(void*,void*){return true;}
void* __fastcall Component(void*,void* type,void*){return type==&model_type?static_cast<void*>(&model):&style;}
void* __fastcall Animator(void*,void*){return &animator;}
bool __fastcall Changing(void*,void*){return changing;}
int __fastcall Hash(void* text,void*){return hashes.at(*static_cast<std::string*>(text));}
bool __fastcall Has(void*,int layer,int hash,void*) {
    if(layer==0)return hash!=99;
    return (hash==101 && layer==2) || (hash==102 && (layer==1 || layer==3));
}
int __fastcall LayerCount(void*,void*){return 4;}
int __fastcall LayerIndex(void*,void* name,void*) {
    const auto& text=*static_cast<std::string*>(name);
    return text=="AddFixture"?1:text=="OverrideFixture"?3:-1;
}
float __fastcall Weight(void*,int,void*){return hit_weight;}
void __fastcall WeightSet(void*,int layer,float value,void*){Check(value==0);presentation_calls.push_back("weight"+std::to_string(layer));}
void __fastcall Evaluate(void*,float dt,void*){Check(dt==0);presentation_calls.push_back("update");if(evaluate_callback)evaluate_callback();}
void __fastcall Fade(void*,int hash,float duration,int layer,float offset,float progress,void*) {
    Check(progress==0);
    if(hash==102)Check(duration==0 && std::isinf(offset) && offset<0 && (layer==1 || layer==3));
    else Check(hash==101 && duration==0.2f && offset==0 && layer==2);
    presentation_calls.push_back("fade"+std::to_string(layer));
}
void __fastcall Play(void*,int hash,float,float,void*){last_play=hash;++plays;if(presentation)presentation_calls.push_back("body");}
void __fastcall Npc(void*,unsigned,void*){last_play=88;++plays;}
void __fastcall State(void*,int,int index,unsigned char* bytes,void*) {
    const int hash=index?next:current;const float progress=index?next_time:time;
    std::memset(bytes,0,36);std::memcpy(bytes,&hash,4);std::memcpy(bytes+12,&progress,4);
}
bool __fastcall Transition(void*,int,void*){return transition;}
int __fastcall ReadyParameter(std::int16_t value,void*){Check(value==2);return 61;}
int __fastcall Count(void*,void*){return 1;}
void* __fastcall Parameter(void*,int,void*){return &parameter;}
int __fastcall ParameterHash(void*,void*){return 61;}
int __fastcall ParameterType(void*,void*){return 1;}
float __fastcall GetFloat(void*,int,void*){return speed;}
void __fastcall SetFloat(void*,int,float value,void*){speed=value;if(setter_fault){setter_fault=false;RaiseException(0xE0670001,0,0,nullptr);}}
struct Objects:api::ManagedObjects {
    api::ObjectResult NewStringUtf8(const char* value)override{strings.push_back(std::make_unique<std::string>(value));return {{true,0},strings.back().get()};}
    api::ObjectResult ManagedType(const char*,const char* value)override{return {{true,0},std::string(value)=="CharacterModelComponent"?static_cast<void*>(&model_type):&style_type};}
    api::ObjectResult AllocateObject(const char*,const char*)override{throw std::runtime_error("unexpected allocation");}
};
api::ReadyTransitionInput Edge(int from,int to,std::string name,float reverse=0) {
    api::ReadyTransitionInput out;out.from={true,from,{}};out.to={true,to,{}};out.state={true,std::move(name),{}};
    out.transition_in={true,0.2f,{}};out.reverse_outro={true,reverse,{}};return out;
}
struct EffectParam {unsigned id=0;bool released=false;void* callback=nullptr;};
std::vector<std::unique_ptr<EffectParam>> effect_params;
std::vector<std::unique_ptr<std::array<unsigned char,128>>> effect_delegates;
int effect_manager,effect_instance,effect_submits=0,effect_removes=0;
void* __fastcall MapEffect(void* target,void* name,void*){Check(target==owner);return name;}
bool __fastcall EmptyEffectPath(void*,void*){return false;}
api::EffectReference __fastcall AcquireEffect(void*,void*) {
    auto p=std::make_unique<EffectParam>();p->id=static_cast<unsigned>(effect_params.size());
    const api::EffectReference ref{p->id,0,p.get()};effect_params.push_back(std::move(p));return ref;
}
unsigned __fastcall EffectId(EffectParam* p,void*){return p->id;}
bool __fastcall EffectReleased(EffectParam* p,void*){return p->released;}
int __fastcall EffectState(EffectParam*,void*){return 3;}
void* __fastcall EffectInstance(EffectParam*,void*){return &effect_instance;}
void __fastcall SubmitEffect(void*,EffectParam* p,void*){Check(p->callback!=nullptr);++effect_submits;}
bool __fastcall RemoveEffect(void*,api::EffectReference r,bool,void*){++effect_removes;static_cast<EffectParam*>(r.parameters)->released=true;return true;}
void __fastcall ReleaseEffect(EffectParam* p,void*){p->released=true;}
api::EffectRequestServices Effects(api::ReferenceMethods refs) {
    return {refs,api::EffectAccess({M(MapEffect),M(AcquireEffect),M(SubmitEffect),M(RemoveEffect),M(ReleaseEffect),
        M(EffectId),M(EffectReleased),M(EffectState),M(EffectInstance),{},M(EmptyEffectPath)}),
        {[]{auto d=std::make_unique<std::array<unsigned char,128>>();d->fill(0);auto* p=d->data();effect_delegates.push_back(std::move(d));return api::ObjectResult{{true,0},p};},{24,40,64}},
        []{return api::ObjectResult{{true,0},&effect_manager};},
        [](void* p,void* target,void* callback){Check(target==owner);static_cast<EffectParam*>(p)->callback=callback;
            return api::EffectInputResult{{true,0},{true,0}};}};
}
}
void TestReadyPlayback() {
    api::ActionConfigInput config;config.path="base";config.transitions.enumerated=true;
    api::SkillInput a;a.source_index=3;a.name={true,"A",{}};a.ready_state={true,"PoseA",{}};a.use_transition={true,true,{}};
    a.ready_effect={true,"Effects/Fixture",{}};a.ready_wait={true,0.125f,{}};
    a.entry_ability={true,"AbilityNotNamedLikePose",{}};a.prepare_ability={true,"PrepareOnly",{}};
    a.child_skills={true,{"NestedSkill"},{}};
    api::SkillInput b=a;b.source_index=8;b.name.value="B";b.ready_state.value="PoseB";
    b.variants.rows.push_back({4,{true,"VariantB",{}},{true,true,{}},{true,100,{}}});
    config.skills.rows={a,b};config.transitions.rows={Edge(4,9,"EdgeAB",0.6f),Edge(4,9,"Absent"),Edge(9,4,"EdgeBA")};
    api::ReadyAnimationDefaults defaults;defaults.transition_in={true,0.1f,{}};
    defaults.transitions={Edge(4,9,"WrongDefault"),Edge(9,4,"WrongDefault")};
    auto set=std::make_shared<ReadyActions>(BuildReadyActions(config,defaults));
    Check(set->poses.size()==3 && set->poses[1].skill_index==8 && set->poses[2].variant_index==4);
    Check(set->poses[2].effect_path.value=="Effects/Fixture" && set->poses[2].wait.value==0.125f);
    Check(set->poses[2].entry_ability.value=="AbilityNotNamedLikePose" && set->poses[2].prepare_ability.value=="PrepareOnly" &&
        set->poses[2].child_skills.values==std::vector<std::string>{"NestedSkill"});
    {
        auto prepared=*set;
        auto path=[](int index){return std::vector<api::ConfigStep>{{"RPG.GameCore.TurnBasedAbilityConfig","OnStart",api::ConfigSelection::Index,index,{}}};};
        api::AbilitySources graph;
        graph.skill_mappings.push_back({"setup",path(1),{true,"B",{}},{true,"Caster",{}},{true,13,{}},{true,true,{}}});
        graph.skill_continuations.push_back({"setup",path(7),{true,13,{}},{true,-1,{}},{true,true,{}}});
        api::AnimationTaskSource pose;pose.ability="setup";pose.path=path(4);pose.state={true,"UnrelatedPreparedPose",{}};
        pose.target_alias={true,"Caster",{}};pose.enabled={true,true,{}};graph.animations.push_back(pose);
        ResolveReadyPreparations(prepared,{{"setup","official-root"}},{{"setup",graph}});
        Check(prepared.poses[1].state=="UnrelatedPreparedPose" && prepared.poses[1].preparation_source=="setup");
        Check(prepared.poses[0].state=="PoseA" && prepared.poses[2].state=="VariantB");
        graph.animations[0].state.value="SecondCandidate";graph.animations.push_back(pose);
        prepared=*set;ResolveReadyPreparations(prepared,{{"setup","official-root"}},{{"setup",graph}});
        Check(prepared.poses[1].state=="PoseB"); // no first-candidate guess
        graph.animations.resize(1);graph.skill_continuations[0].control.value=14;
        ResolveReadyPreparations(prepared,{{"setup","official-root"}},{{"setup",graph}});Check(prepared.poses[1].state=="PoseB");
    }
    Check(SelectReadyEdge(*set,3,set->poses[1]).state=="EdgeAB"); // original indices, first match
    Check(SelectReadyEdge(*set,8,set->poses[2]).state.empty()); // variant is not a new runtime skill
    auto altered=*set;altered.transitions.rows[0].state.value="";
    Check(SelectReadyEdge(altered,3,altered.poses[1]).state.empty()); // explicit empty suppresses fallback
    altered.transitions.rows.clear();altered.transitions.enumerated=false;
    Check(SelectReadyEdge(altered,3,altered.poses[1]).source=="direct");
    altered.transitions.enumerated=true;Check(SelectReadyEdge(altered,3,altered.poses[1]).state=="WrongDefault");
    altered.transitions.rows.push_back({});Check(SelectReadyEdge(altered,3,altered.poses[1]).state.empty());altered.transitions.rows.clear();
    altered.poses[1].use_transition.value=false;Check(SelectReadyEdge(altered,3,altered.poses[1]).state.empty());
    api::AnimationControlMethods methods;methods.hash=M(Hash);methods.has_state=M(Has);methods.free_style_state=M(Play);
    methods.npc_motion=M(Npc);methods.state_info=M(State);methods.in_transition=M(Transition);
    methods.ready_parameter_hash=M(ReadyParameter);methods.parameter_count=M(Count);methods.parameter_at=M(Parameter);
    methods.parameter_hash=M(ParameterHash);methods.parameter_type=M(ParameterType);methods.float_get=M(GetFloat);methods.float_set=M(SetFloat);
    methods.layer_count=M(LayerCount);methods.layer_index=M(LayerIndex);methods.layer_weight=M(Weight);
    methods.set_layer_weight=M(WeightSet);methods.update=M(Evaluate);methods.cross_fade=M(Fade);
    api::ModelAccessMethods models;models.animator=M(Animator);models.changing=M(Changing);
    api::ProfileInputMethods profiles;profiles.animator_initialized=M(Loaded);
    const api::ReferenceMethods refs{M(Keep),M(Target),M(Free),{}, {}};
    auto token=api::TrackedEntities().Track(owner);
    auto animations=[=](api::EntityToken token_value){return std::make_unique<api::ActorAnimationSession>(token_value,refs,
        api::EntityAccess({M(Id),M(World),M(Loaded),M(Component),{},{},{},{}}),api::ModelAccess(models),api::AnimationControl(methods),api::ProfileInputs(profiles));};
    ActionPlayback playback(token,own_world,animations(token));
    Objects objects;api::WorldFrame frame;frame.world=own_world;frame.disposing={false,{true,0}};
    auto step=[&]{++frame.sequence;playback.Service(frame,true,objects);};
    auto select=[&](std::size_t pose){playback.CancelPending();playback.Queue(ReadyState{set,pose});step();};
    select(0);Check(last_play==11);current=11;step();
    const auto initial_plays=plays;select(0);Check(plays==initial_plays); // not restarted
    select(1);Check(last_play==21 && speed==1);current=21;time=0.3f;step();
    select(0);Check(last_play==21 && speed==-1); // no cut, reverse existing edge
    select(1);Check(speed==1 && last_play==21); // changed mind, resume original destination
    select(0);Check(speed==-1);time=0;step();Check(last_play==11 && speed==0.8f);
    current=11;step();select(1);current=21;time=1.1f;step();Check(last_play==12);
    current=12;time=0.2f;step();select(2);Check(last_play==13 && speed==0.8f);
    current=13;step();playback.Queue(NpcMotion{5});step();Check(last_play==88 && speed==0.8f);
    select(0);current=11;step();select(1);current=21;time=0.2f;step();
    speed=0.25f;playback.Queue(NpcMotion{5});step();Check(speed==0.25f); // external writer preserved
    speed=0.8f;select(0);current=11;step();setter_fault=true;select(1);
    Check(playback.Status().last->outcome==ActionOutcome::CallFailed && speed==0.8f); // partial write cleaned
    select(1);current=12;step();select(0);Check(speed==1);changing=true;step();changing=false;
    Check(speed==0.8f); // same live Animator before changing; only own parameter restored
    select(0);current=11;step();select(1);current=21;time=0.2f;step();setter_fault=true;select(0);
    Check(playback.Status().last->outcome==ActionOutcome::CallFailed && speed==0.8f); // reverse setter partial failure
    select(0);current=11;time=0.2f;step();
    current=700;step(); // controller-owned successor with no naming relationship
    const auto loop_plays=plays;select(0);Check(plays==loop_plays);
    select(1);Check(last_play==21); // retained logical A, not unknown source/direct B
    current=700;next=21;next_time=0.3f;time=1.4f;transition=true;step();
    select(0);Check(speed==-1 && last_play==21); // next-state time is inside authored reverse window
    select(1);Check(speed==1);select(0);next_time=0;step();
    Check(last_play==11 && speed==0.8f); // reverse edge boundary observed in next state
    transition=false;current=11;step();select(1);current=21;time=0.7f;step();
    next=701;next_time=0.1f;transition=true;step();
    transition=false;current=701;step(); // edge's authored successor must not be replaced by PoseB
    const auto successor_plays=plays;select(1);Check(plays==successor_plays);
    select(0);Check(last_play==22); // retains logical B after its own controller successor
    playback.Queue(NpcMotion{5});step();select(1);Check(last_play==12); // explicit domain action releases old intent
    // Sources differ from official names, and layer order is not assumed.
    playback.Queue(NpcMotion{5});step();
    set->defaults.default_expression={true,"DefaultFixture",{}};
    set->defaults.empty_layer_state={true,"EmptyFixture",{}};
    set->defaults.hit_additive_layer={true,"AddFixture",{}};
    set->defaults.hit_override_layer={true,"OverrideFixture",{}};
    hashes["DefaultFixture"]=101;hashes["EmptyFixture"]=102;
    hit_weight=0.8f;presentation=true;presentation_calls.clear();select(0);
    Check(presentation_calls==std::vector<std::string>{"update","fade1","weight1","update","fade3","weight3","fade2","body"});
    // Same ready selection does not reset another layer or restart body/face.
    current=11;step();presentation_calls.clear();select(0);Check(presentation_calls.empty());
    hit_weight=0;select(1);
    Check(presentation_calls==std::vector<std::string>{"weight1","weight3","fade2","body"});
    // Update(0) can dispatch an event changing the model: no further old writes.
    hit_weight=1;presentation_calls.clear();evaluate_callback=[] {changing=true;};select(0);
    Check(playback.Status().last->outcome==ActionOutcome::Cancelled);
    Check(presentation_calls==std::vector<std::string>{"update"});changing=false;
    // A newer input during zero-time evaluation is serviced next frame, not overwritten.
    evaluate_callback=[&] {playback.Queue(NpcMotion{5});};presentation_calls.clear();select(1);
    Check(playback.Status().last->outcome==ActionOutcome::Cancelled && playback.Status().pending);
    Check(presentation_calls==std::vector<std::string>{"update"});evaluate_callback={};step();Check(last_play==88);
    // A missing named hit layer does not block independent default-face/body work.
    set->defaults.hit_additive_layer.value="NoSuchLayer";hit_weight=0;presentation_calls.clear();select(0);
    Check(presentation_calls==std::vector<std::string>{"weight3","fade2","body"});
    Check(playback.Status().ready_note.find("NoSuchLayer absent")!=std::string::npos);
    Check(playback.Close(frame) && roots.empty());
    {
        // Whole ready consumer + real SDK call wrappers, not string/source checks.
        presentation=false;hit_weight=0;changing=false;transition=false;current=11;time=0;speed=0.8f;
        ActionPlayback effects(token,own_world,animations(token),std::make_shared<api::PresentationEffects>(Effects(refs)));
        auto tick=[&]{++frame.sequence;effects.Service(frame,true,objects);};
        auto choose=[&](std::size_t pose){effects.CancelPending();effects.Queue(ReadyState{set,pose});tick();};
        choose(0);Check(effect_submits==1);choose(0);Check(effect_submits==1 && effect_removes==0);
        choose(1);Check(effect_submits==2);current=21;time=0.3f;tick();Check(effect_removes==1);
        choose(0);Check(speed==-1 && effect_submits==3);
        choose(1);Check(speed==1 && effect_submits==4); // resume must restore target effect, too
        choose(0);Check(effect_submits==5);time=0;tick();Check(last_play==11 && effect_submits==5); // internal edge completion is NOT a fresh request
        effects.Queue(NpcMotion{1});tick();Check(effect_removes==5);
        choose(1);Check(effect_submits==6);changing=true;tick();changing=false;tick();Check(effect_removes==6);
        choose(0);Check(effect_submits==7);
        effects.CancelPending();effects.ServiceEffects(frame);Check(effect_removes==7); // handoff cleanup without ready model
        choose(1);Check(effect_submits==8);
        api::TrackedEntities().Disposed(token);effects.ServiceEffects(frame);
        Check(effect_removes==8); // natural owner loss does not need a usable old Animator
        Check(effects.Close(frame) && roots.empty());
    }
    api::TrackedEntities().Forget(token);strings.clear();effect_params.clear();effect_delegates.clear();
}
