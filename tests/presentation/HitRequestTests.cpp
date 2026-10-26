#include <hksr/framework/presentation/ActionPlayback.hpp>
#include <hksr/framework/tasks/TaskPresentation.hpp>
#include <Windows.h>
#include <array>
#include <cstring>
#include <map>
#include <stdexcept>

namespace {
using namespace hksr;
using namespace hksr::framework;
int entity,world,model,animator,other_animator,style,model_type,style_type,motion_type,emotion_type,info,next_root;
auto* owner=reinterpret_cast<api::GameEntity*>(&entity);
auto* own_world=reinterpret_cast<api::GameWorld*>(&world);
std::map<int,void*> roots;
std::vector<std::unique_ptr<std::string>> strings;
struct Layer {int state=10,next=0;float time=0,weight=0.25f;bool transition=false;};
std::array<Layer,4> layers;
std::map<std::string,int> names{{"Attack",10},{"Idle",11},{"Hit",20},{"Heavy",21},{"Face",30},{"Neutral",40},{"ChangedFace",41}};
int alive_state=1;
struct Motion {float clock=0;} motion;
bool motion_present=false;
float motion_height=0;
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
int __fastcall EntityType(void* value,void*){Check(value==owner);return 1;}
int __fastcall Alive(void* value,void*){Check(value==owner);return alive_state;}
float __fastcall Height(void* value,float,void*){Check(value==&motion);return motion_height;}
float __fastcall FallHeight(void* value,void*){Check(value==&motion);return motion_height;}
void* __fastcall Component(void* value,void* type,void*){
    Check(value==owner);if(type==&motion_type)return motion_present?&motion:nullptr;
    if(type==&emotion_type)return nullptr;
    return type==&model_type?static_cast<void*>(&model):&style;
}
std::string visual_layer="Visual";
void* __fastcall LayerName(void*,int layer,void*){Check(layer==0);return &visual_layer;}
int __fastcall TextLength(std::string* value,void*){return static_cast<int>(value->size());}
std::uint16_t __fastcall TextChar(std::string* value,int index,void*){return static_cast<unsigned char>(value->at(index));}
void* __fastcall Animator(void*,void*){return replaced?&other_animator:&animator;}
bool __fastcall Changing(void*,void*){return changing;}
int __fastcall Hash(std::string* name,void*){return names.at(*name);}
bool __fastcall Has(void*,int layer,int hash,void*){return (hash!=20 || hit_exists) && ((hash!=30 && hash!=40 && hash!=41) || layer==3);}
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
    api::ObjectResult ManagedType(const char*,const char* name) override{
        const std::string type=name;
        return {{true,0},type=="CharacterModelComponent"?&model_type:type=="BGOBCKLPEMK"?&motion_type:type=="PEPNFLNCDHG"?&emotion_type:&style_type};
    }
    api::ObjectResult NewStringUtf8(const char* value) override{strings.push_back(std::make_unique<std::string>(value));return {{true,0},strings.back().get()};}
    api::ObjectResult AllocateObject(const char*,const char*) override{throw std::runtime_error("unexpected allocation");}
};
HitCommand Additive(){return {{{"Add","Hit",0.75f},{"Override","Hit",0.75f}}};}
}
void TestHitRequests() {
    api::AnimationControlMethods methods;methods.hash=M(Hash);methods.has_state=M(Has);methods.play=M(Play);methods.free_style_state=M(Body);
    methods.cross_fade=M(Fade);methods.layer_count=M(Count);methods.layer_index=M(Index);methods.layer_weight=M(Weight);methods.set_layer_weight=M(SetWeight);
    methods.state_info=M(Info);methods.in_transition=M(Transition);methods.layer_name=M(LayerName);
    methods.hit_motion={M(Height),M(FallHeight),offsetof(Motion,clock),true};
    api::ModelAccessMethods models;models.animator=M(Animator);models.changing=M(Changing);
    api::ProfileInputMethods profiles;profiles.animator_initialized=M(Yes);
    const api::ReferenceMethods refs{M(Keep),M(Target),M(Free),{}, {}};
    const auto token=api::TrackedEntities().Track(owner);
    api::EntityAccessMethods entity_methods{M(Id),M(World),M(Yes),M(Component),{},{},{},{},M(EntityType),M(Alive)};
    auto session=std::make_unique<api::ActorAnimationSession>(token,refs,api::EntityAccess(entity_methods),
        api::ModelAccess(models),api::AnimationControl(methods),api::ProfileInputs(profiles));
    api::ComponentConfigMethods text_methods;text_methods.string_length=M(TextLength);text_methods.string_character=M(TextChar);
    api::ComponentConfig text(text_methods);
    ActionPlayback player(token,own_world,std::move(session),{},&text);
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
    layers[3].state=10;expressive.expression_seconds=.5f;
    hit=player.QueueHit(expressive);tick();tick();tick();Check(layers[3].state==30);
    frame.delta_time=.25f;tick();Check(layers[3].state==30);
    player.Service(frame,true,objects);Check(layers[3].state==30); // same frame does not advance timer twice
    tick();Check(layers[3].state==10 && !hit->released);frame.delta_time=0;
    player.ReleaseHit(hit->serial);tick();

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
    // The production policy assembles inputs from this receiver's catalog,
    // control sources and actual animation session, not an injected HitCommand.
    player.ConfigureHitResolver({});player.SetPresentationDomain(true);
    auto character=std::make_shared<api::CharacterHitInput>();character->loaded=true;
    character->source="receiver";character->resilience={true,3,{}};character->repeat_occur_at={true,-1,{}};
    character->phase_present={true,false,{}};
    auto weights=std::make_shared<api::HitAnimationDefaults>();weights->resilience_weights={true,{.25f,.5f,.75f,1.f},{}};
    weights->avatar_weight={true,.6f,{}};weights->minimum_frame_interval={true,2,{}};
    auto defaults=std::make_shared<api::VisualReactionInput>();defaults->present=true;
    defaults->hit={true,"Hit",{}};defaults->hit_h={true,"Heavy",{}};
    auto presentation=std::make_shared<api::ReadyAnimationDefaults>();presentation->hit_additive_layer={true,"Add",{}};
    presentation->hit_override_layer={true,"Override",{}};presentation->hit_expression={true,"Face",{}};
    presentation->default_expression={true,"Neutral",{}};
    auto visuals=std::make_shared<api::VisualConfigSources>();visuals->context_available=true;
    visuals->defaults.loaded=true;visuals->defaults.behaviors.enumerated=true;
    visuals->expression_transition={true,.2f,{}};
    api::VisualBehaviorInput return_visual;return_visual.flag={true,99,{}};
    return_visual.emotion={true,"ChangedFace",{}};return_visual.loop.resolved_state={true,"Idle",{}};
    visuals->defaults.behaviors.rows.push_back(return_visual);
    bool available=false;HitControlInputs controls;
    player.ConfigureReceiverPolicy([&](bool visual)->std::optional<HitCatalogInputs> {
        if(!available)return {};return HitCatalogInputs{character,weights,defaults,presentation,visual?visuals:nullptr};
    },[&](bool){return controls;});
    auto actual=inbox->Post({"DamageHit"});tick();Check(!actual.status->delivered && actual.status->error.empty());
    available=true;tick();tick();
    if(!actual.status->error.empty())throw std::runtime_error("receiver delivery: "+actual.status->error);
    if(actual.status->playback && !actual.status->playback->error.empty())throw std::runtime_error("receiver playback: "+actual.status->playback->error);
    Check(actual.status->playback && actual.status->playback->accepted);
    Check(layers[1].weight==.6f && layers[3].state==30);
    auto repeated=inbox->Post({"DamageHit"});tick();Check(repeated.status->delivered && repeated.status->ignored);
    tick();tick();controls.force_heavy=true;
    auto forced=inbox->Post({"DamageHit"});tick();tick();tick();
    Check(forced.status->playback && forced.status->playback->entered && layers[0].state==21 && layers[1].weight==0);
    Check(forced.status->playback->body_end_reached && !forced.status->playback->clip_complete);
    player.SetCompletionState("Idle");
    // Expression returns to the latest source, not its pre-hit snapshot. The
    // body remains independent and does not return just because its clip ended.
    controls.visual_flag=99;frame.delta_time=.5f;tick();frame.delta_time=0;
    Check(forced.status->playback->expression_released && layers[3].state==41 && !forced.status->playback->released);
    layers[0].time=2;tick();tick();Check(!forced.status->playback->body_returned);
    layers[0].state=10;tick();tick();Check(!forced.status->playback->released); // unrelated successor is not reset
    layers[0].state=11;motion_present=true;motion_height=.05f;tick();Check(!forced.status->playback->released);
    motion_height=0;alive_state=3;tick();Check(!forced.status->playback->released);
    const auto returned_body_plays=body_plays;alive_state=1;tick();
    Check(forced.status->playback->body_returned && forced.status->playback->released && !forced.status->playback->cancelled);
    Check(body_plays==returned_body_plays && layers[0].state==11); // observe native successor, do not replay it
    motion_present=false;controls={};
    tick();tick();auto return_wait=inbox->Post({"DamageHit"});tick();tick();tick();
    available=false;frame.delta_time=.5f;tick();Check(layers[3].state==30 && !return_wait.status->playback->expression_released);
    available=true;tick();frame.delta_time=0;
    Check(layers[3].state==40 && return_wait.status->playback->expression_released);
    layers[1].state=layers[2].state=10;tick();tick();
    Check(return_wait.status->playback->released && layers[0].state==11);
    player.CancelPending();tick();controls={};
    character->resilience.available=false;auto unread=inbox->Post({"DamageHit"});const auto writes=weight_writes;tick();
    Check(!unread.status->error.empty() && weight_writes==writes);
    character->resilience.available=true;
    // Active-skill recovery uses the same phase/visual source, but not the
    // receiving-hit height test. Queue admission permits reset before playback.
    player.CancelPending();tick();available=true;controls={};motion_present=true;motion_height=.4f;
    AnimationCommand skill_body{"Attack",0,0,0,.58f};skill_body.track=AnimationTrack{3,0,{},false};
    auto skill=player.QueueAnimation(skill_body);tick();tick();layers[0].time=.7f;
    controls.visual_flag=99;
    auto recovery=player.QueueSkillRecovery();const auto recovery_plays=body_plays;tick();
    if(!recovery->error.empty())throw std::runtime_error("skill recovery: "+recovery->error);
    Check(recovery->accepted && !recovery->complete && layers[3].state==41 && body_plays==recovery_plays);
    tick();Check(!recovery->complete && layers[0].state==10);
    layers[0].time=1.1f;tick();tick();tick();
    Check(recovery->complete && layers[0].state==11 && body_plays==recovery_plays+1);
    player.ReleaseAnimation(skill->serial);player.ReleaseSkillRecovery(recovery->serial);
    Check(layers[3].state==41); // return observer release does not restore the skill face
    auto no_replay=player.QueueSkillRecovery();tick();Check(no_replay->complete && body_plays==recovery_plays+1);
    player.ReleaseSkillRecovery(no_replay->serial);
    motion_present=false;controls={};
    skill=player.QueueAnimation(skill_body);tick();tick();
    available=false;auto waiting_return=player.QueueSkillRecovery();tick();Check(!waiting_return->accepted);
    player.QueueAnimation({"Heavy"});available=true;tick();
    Check(waiting_return->cancelled && waiting_return->released && layers[0].state==21);
    player.CancelPending();tick();
    VisualProgramServices visual_services;
    visual_services.effects=std::make_shared<api::PresentationEffects>(api::EffectRequestServices{{},api::EffectAccess({}),{},{},{}});
    visual_services.create=[](auto&,const auto&,const auto&)->VisualEffectId{throw std::runtime_error("unexpected recovery effect");};
    auto visual_scene=std::make_shared<VisualProgramPlayer>(std::make_shared<VisualProgram>(),visual_services);
    TaskPresentationServices ports;
    std::shared_ptr<const AnimationRequestStatus> recovery_receipt;
    ports.animation=[&](const auto& participant)->std::optional<TaskAnimationEndpoint>{
        Check(participant.name=="body");auto endpoint=BindTaskAnimation(player);
        endpoint.recover=[&]{recovery_receipt=player.QueueSkillRecovery();return recovery_receipt;};return endpoint;
    };
    ControlPerformance control_phase=ControlPerformance::Working;
    ports.control=[&](const auto& changes){for(const auto& change:changes)control_phase=std::get<ControlSetPerformance>(change).phase;};
    auto recovery_plan=std::make_shared<TaskSequence>();
    TaskAnimationCommand body_command{{"body"},"Attack",0,0,0,.58f};body_command.track=AnimationTrack{3,0,{},false};
    recovery_plan->nodes={
        {"body",TaskSubmit{"body",body_command}},
        {"entry-finish",TaskWaitRequest{"body",TaskWaitSignal::Progress,.7}},
        {"after",TaskSubmit{"after",TaskControlCommand{{ControlSetPerformance{ControlPerformance::Resetting}}}}},
        {"return",TaskSubmit{"return",TaskSkillRecovery{}}},
        {"admitted",TaskWaitRequest{"return",TaskWaitSignal::RequestComplete}},
        {"reset",TaskSubmit{"reset",TaskControlCommand{{ControlSetPerformance{ControlPerformance::Ready}}}}},
        {"entry",TaskSerial{{0,1,2,3,4,5}}}};recovery_plan->entry=6;
    TaskPresentation recovery_task({300},token.generation,recovery_plan,visual_scene,{},ports);
    auto service_task=[&]{tick();recovery_task.Service(.01,frame.sequence);};
    recovery_task.Service(0,++frame.sequence);service_task();service_task();
    layers[0].time=.71f;service_task();service_task();service_task();
    if(!recovery_task.Status().issues.empty())throw std::runtime_error("recovery sequence: "+recovery_task.Status().issues.front());
    if(control_phase!=ControlPerformance::Ready || layers[0].state!=10 || recovery_task.Drained()) {
        std::string detail="recovery boundary phase="+std::to_string(static_cast<int>(control_phase))+" body="+std::to_string(layers[0].state)+
            " drained="+std::to_string(recovery_task.Drained());
        for(const auto& waiting:recovery_task.Status().waiting)detail+=" wait="+waiting;
        for(const auto& row:recovery_task.Requests())detail+=" request="+row.name+" accepted="+std::to_string(row.state.request_complete)+" drained="+std::to_string(row.state.drained);
        if(recovery_receipt)detail+=" owned-return="+std::to_string(recovery_receipt->accepted)+" error="+recovery_receipt->error;
        throw std::runtime_error(detail);
    }
    // Releasing the finished task must retain its body record until the
    // registered SkillFinishRecover has consumed that record's end boundary.
    service_task();Check(layers[0].state==10 && !recovery_task.Drained());
    layers[0].time=1.1f;service_task();service_task();service_task();service_task();
    Check(layers[0].state==11 && recovery_task.Drained() && recovery_task.Status().issues.empty());
    visual_scene->End();visual_scene->Service();player.CancelPending();tick();
    hit=player.QueueHit(Additive());tick();Check(player.Close(frame));Check(hit->released && layers[1].weight==0.25f && roots.empty());
    bool closed=false;try{inbox->Post({"DamageHit"});}catch(const std::logic_error&){closed=true;}Check(closed);
}
