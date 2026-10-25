#include <hksr/framework/presentation/ActionPlayback.hpp>
#include <Windows.h>
#include <array>
#include <cstring>
#include <map>
#include <stdexcept>

namespace {
using namespace hksr;
using namespace hksr::framework;
int entity,world,model,animator,other_animator,style,model_type,style_type,info,next_root;
auto* owner=reinterpret_cast<api::GameEntity*>(&entity);
auto* own_world=reinterpret_cast<api::GameWorld*>(&world);
std::map<int,void*> roots;
std::vector<std::unique_ptr<std::string>> strings;
struct Layer {int state=10,next=0;float time=0,weight=0.25f;bool transition=false;};
std::array<Layer,4> layers;
std::map<std::string,int> names{{"Attack",10},{"Hit",20},{"Heavy",21},{"Face",30}};
bool changing=false,replaced=false,override_exists=true,fail_weight=false,hit_exists=true;
std::function<void()> on_weight;
unsigned body_plays=0,layer_plays=0,weight_writes=0;
void CheckAt(bool value,int line){if(!value)throw std::runtime_error("hit request test failed at line "+std::to_string(line));}
#define Check(...) CheckAt((__VA_ARGS__),__LINE__)
template<class T>api::Method M(T f){return {reinterpret_cast<void*>(f),&info};}
int __fastcall Keep(void* value,int,int,void*){roots[++next_root]=value;return next_root;}
void* __fastcall Target(int id,void*){return roots.at(id);}
void __fastcall Free(int id,void*){roots.erase(id);}
unsigned __fastcall Id(void* value,void*){Check(value==owner);return 1;}
api::GameWorld* __fastcall World(void* value,void*){Check(value==owner);return own_world;}
bool __fastcall Yes(void*,void*){return true;}
void* __fastcall Component(void* value,void* type,void*){Check(value==owner);return type==&model_type?static_cast<void*>(&model):&style;}
void* __fastcall Animator(void*,void*){return replaced?&other_animator:&animator;}
bool __fastcall Changing(void*,void*){return changing;}
int __fastcall Hash(std::string* name,void*){return names.at(*name);}
bool __fastcall Has(void*,int layer,int hash,void*){return (hash!=20 || hit_exists) && (hash!=30 || layer==3);}
int __fastcall Count(void*,void*){return 4;}
int __fastcall Index(void*,std::string* name,void*){return *name=="Visual"?0:*name=="Add"?1:*name=="Override" && override_exists?2:-1;}
float __fastcall Weight(void* value,int layer,void*){Check(value==&animator);return layers.at(layer).weight;}
void __fastcall SetWeight(void* value,int layer,float weight,void*){
    Check(value==&animator);layers.at(layer).weight=weight;++weight_writes;
    if(on_weight){auto callback=std::move(on_weight);on_weight={};callback();}
    if(fail_weight){fail_weight=false;RaiseException(0xE0480001,0,0,nullptr);}
}
void __fastcall Play(void* value,int hash,int layer,float time,void*){Check(value==&animator);layers.at(layer).state=hash;layers.at(layer).time=time;++layer_plays;}
void __fastcall Body(void* value,int hash,float,float time,void*){Check(value==&style);layers[0].state=hash;layers[0].time=time;++body_plays;}
void __fastcall Fade(void* value,int hash,float,int layer,float time,float,void*){Play(value,hash,layer,time,nullptr);}
void __fastcall Info(void* value,int layer,int next,unsigned char* bytes,void*){
    Check(value==&animator);const auto& data=layers.at(layer);const auto hash=next?data.next:data.state;
    std::memset(bytes,0,36);std::memcpy(bytes,&hash,4);std::memcpy(bytes+12,&data.time,4);
}
bool __fastcall Transition(void*,int layer,void*){return layers.at(layer).transition;}
struct Objects:api::ManagedObjects {
    api::ObjectResult ManagedType(const char*,const char* name) override{return {{true,0},std::string(name)=="CharacterModelComponent"?&model_type:&style_type};}
    api::ObjectResult NewStringUtf8(const char* value) override{strings.push_back(std::make_unique<std::string>(value));return {{true,0},strings.back().get()};}
    api::ObjectResult AllocateObject(const char*,const char*) override{throw std::runtime_error("unexpected allocation");}
};
HitCommand Additive(){return {{{"Add","Hit",0.75f},{"Override","Hit",0.75f}}};}
}
void TestHitRequests() {
    api::AnimationControlMethods methods;methods.hash=M(Hash);methods.has_state=M(Has);methods.play=M(Play);methods.free_style_state=M(Body);
    methods.cross_fade=M(Fade);methods.layer_count=M(Count);methods.layer_index=M(Index);methods.layer_weight=M(Weight);methods.set_layer_weight=M(SetWeight);
    methods.state_info=M(Info);methods.in_transition=M(Transition);
    api::ModelAccessMethods models;models.animator=M(Animator);models.changing=M(Changing);
    api::ProfileInputMethods profiles;profiles.animator_initialized=M(Yes);
    const api::ReferenceMethods refs{M(Keep),M(Target),M(Free),{}, {}};
    const auto token=api::TrackedEntities().Track(owner);
    auto session=std::make_unique<api::ActorAnimationSession>(token,refs,api::EntityAccess({M(Id),M(World),M(Yes),M(Component),{},{},{},{}}),
        api::ModelAccess(models),api::AnimationControl(methods),api::ProfileInputs(profiles));
    ActionPlayback player(token,own_world,std::move(session));
    Objects objects;api::WorldFrame frame;frame.world=own_world;frame.disposing={false,{true,0}};
    auto tick=[&]{++frame.sequence;player.Service(frame,true,objects);};
    auto attack=player.QueueAnimation({"Attack"});tick();tick();Check(attack->accepted && attack->entered);
    const auto initial_body=body_plays;
    auto hit=player.QueueHit(Additive());tick();Check(!hit->accepted && layers[1].weight==0.75f && layers[2].weight==0.75f);
    tick();tick();Check(hit->accepted && hit->entered && body_plays==initial_body && !attack->cancelled);
    layers[1].time=layers[2].time=1.5f;tick();tick();
    Check(hit->clip_complete && !hit->state_left && !hit->released); // time=1 is not a logical return
    layers[1].state=layers[2].state=10;layers[2].transition=true;layers[2].next=20;tick();tick();
    Check(!hit->state_left);layers[2].transition=false;tick();tick();
    Check(hit->state_left && hit->released && !hit->cancelled && layers[1].weight==0.25f && layers[2].weight==0.25f);
    Check(body_plays==initial_body && layers[0].state==10);

    // Optional override layer does not prevent an ordinary additive hit.
    override_exists=false;hit=player.QueueHit(Additive());tick();tick();tick();Check(hit->accepted && hit->entered);
    auto replacement=player.QueueHit(Additive());tick();tick();tick();
    Check(hit->released && hit->cancelled && replacement->accepted);
    player.ReleaseHit(hit->serial);tick();Check(!replacement->released && layers[1].weight==0.75f);
    layers[1].weight=0.6f;player.ReleaseHit(replacement->serial,true);tick();Check(layers[1].weight==0.6f);
    override_exists=true;layers[1].weight=0.25f;

    // A missing state must not first expose an old clip by raising its weight.
    hit_exists=false;const auto untouched=weight_writes;hit=player.QueueHit(Additive());tick();
    Check(hit->released && !hit->error.empty() && weight_writes==untouched);hit_exists=true;

    // Partial native failure restores only the writes actually owned.
    fail_weight=true;hit=player.QueueHit(Additive());tick();
    Check(hit->released && hit->cancelled && !hit->error.empty() && layers[1].weight==0.25f && layers[2].weight==0.25f);
    // Invalid replacement leaves the existing receiver request intact.
    hit=player.QueueHit(Additive());tick();tick();
    auto invalid=Additive();invalid.layers.push_back(invalid.layers[0]);bool rejected=false;
    try{player.QueueHit(invalid);}catch(const std::invalid_argument&){rejected=true;}
    Check(rejected && !hit->cancelled);player.ReleaseHit(hit->serial);tick();

    // Expression is layered after body acceptance; its release is independent
    // of the still-playing hit and never resets the body animation.
    auto expressive=Additive();expressive.expression="Face";expressive.expression_transition=0.2f;
    layers[3].state=10;layers[3].time=0.4f;
    hit=player.QueueHit(expressive);tick();tick();tick();Check(layers[3].state==30);
    player.ReleaseHitExpression(hit->serial);tick();Check(layers[3].state==10 && !hit->released);
    tick();Check(layers[3].state==10);player.ReleaseHit(hit->serial);tick();
    hit=player.QueueHit(expressive);tick();tick();tick();
    auto face=player.QueueAnimation({"Face",3});tick();tick();
    player.ReleaseHit(hit->serial);tick();Check(layers[3].state==30 && face->entered && !face->cancelled);

    HitCommand heavy;heavy.layers={{"Add","",0},{"Override","",0}};heavy.body=HitBody{"Visual","Heavy"};
    hit=player.QueueHit(heavy);tick();tick();tick();Check(hit->accepted && hit->entered && attack->cancelled && layers[0].state==21);
    layers[0].time=2;tick();tick();Check(hit->clip_complete && !hit->released && layers[0].state==21);
    const auto heavy_plays=body_plays;player.ReleaseHit(hit->serial);tick();
    Check(hit->released && body_plays==heavy_plays && layers[1].weight==0.25f); // no fabricated StandBy
    // A new body command owns the successor; old retirement must not cancel it.
    hit=player.QueueHit(heavy);tick();tick();auto successor=player.QueueAnimation({"Attack"});tick();tick();
    Check(hit->released && hit->cancelled && successor->entered && !successor->cancelled && layers[0].state==10);

    // Reentrant cancellation while writing a weight is drained before another
    // layer/animation can be submitted. Servicing the player cannot recurse.
    on_weight=[&]{player.CancelPending();player.Service(frame,true,objects);};
    const auto before_layers=layer_plays;hit=player.QueueHit(Additive());tick();
    Check(hit->released && hit->cancelled && layer_plays==before_layers && layers[1].weight==0.25f);
    hit=player.QueueHit(Additive());tick();replaced=true;const auto before_weights=weight_writes;tick();
    Check(hit->released && hit->cancelled && weight_writes==before_weights);replaced=false;
    layers[1].weight=layers[2].weight=0.25f;
    hit=player.QueueHit(Additive());tick();player.CancelPending();player.ServiceEffects(frame);
    Check(hit->released && layers[1].weight==0.25f);
    hit=player.QueueHit(Additive());tick();auto foreign=frame;foreign.world=reinterpret_cast<api::GameWorld*>(1);
    const auto own_weights=weight_writes;player.Service(foreign,true,objects);
    Check(hit->cancelled && !hit->released && weight_writes==own_weights);
    player.ServiceEffects(frame);Check(hit->released && layers[1].weight==0.25f);
    // Cross-actor producers post values; this receiver resolves and plays them
    // only on its own frame. Admission is not a claim of visual completion.
    player.ConfigureHitResolver([](const HitInput& input){Check(input.animation=="DamageHit");return HitResolution{true,Additive()};});
    auto inbox=player.IncomingHits();auto delivered=inbox->Post({"DamageHit"});
    player.Service(foreign,true,objects);Check(!delivered.status->cancelled && !delivered.status->delivered);
    tick();Check(delivered.status->delivered && delivered.status->playback);
    tick();tick();Check(delivered.status->playback->accepted && delivered.status->playback->entered);
    delivered.cancel();tick();Check(!delivered.status->playback->cancelled);
    player.ReleaseHit(delivered.status->playback->serial);tick();Check(delivered.status->playback->released);
    hit=player.QueueHit(Additive());tick();Check(player.Close(frame));Check(hit->released && layers[1].weight==0.25f && roots.empty());
    bool closed=false;try{inbox->Post({"DamageHit"});}catch(const std::logic_error&){closed=true;}Check(closed);
}
