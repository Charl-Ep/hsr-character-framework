#include <hksr/framework/presentation/ActionPlayback.hpp>
#include <hksr/framework/tasks/TaskPresentation.hpp>
#include <Windows.h>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <limits>
#include <cmath>

namespace {
using namespace hksr;
int entity,world,info,model_type,style_type,style,animator,model,art,next=0;
auto* own=reinterpret_cast<api::GameEntity*>(&entity);
auto* owner_world=reinterpret_cast<api::GameWorld*>(&world);
std::map<int,void*> roots;
std::vector<std::unique_ptr<std::string>> strings;
bool loaded=true,initialized=true,changing=false,exists=true,fault=false,requeue=false,dispose_on_has=false;
bool cancel_on_init=false,cancel_on_has=false;
unsigned reads=0,plays=0,npc_plays=0,state_plays=0,hashes=0;
int current_hash=93,current_full_hash=0;float current_time=0;bool looping=false,transitioning=false;
float face_time=0;
std::string successor_name="OtherPhaseIdle";bool cancel_on_resolve=false;
int expected_layer=0;
float expected_start=0,expected_blend=0;
framework::ActionPlayback* active=nullptr;
api::WorldFrame active_frame;
api::ManagedObjects* active_objects=nullptr;
void CheckAt(bool value,int line) { if(!value) throw std::runtime_error("action playback integration failed at line "+std::to_string(line)); }
#define Check(value) CheckAt((value),__LINE__)
template<class T> api::Method M(T f) { return {reinterpret_cast<void*>(f),&info}; }
int __fastcall Keep(void* value,int,int,void*) { roots[++next]=value; return next; }
void* __fastcall Target(int id,void*) { return roots.at(id); }
void __fastcall Free(int id,void*) { roots.erase(id); }
std::uint32_t __fastcall Id(void* e,void*) { Check(e==own); ++reads; return 11; }
api::GameWorld* __fastcall World(void* e,void*) { Check(e==own); ++reads; return owner_world; }
bool __fastcall Loaded(void* e,void*) { Check(e==own); ++reads; return loaded; }
void* __fastcall Component(void* e,void* type,void*) {
    Check(e==own); ++reads;
    if(type==&model_type) return &model;
    Check(type==&style_type); return &style;
}
void* __fastcall Animator(void* value,void*) { Check(value==&model); return &animator; }
bool __fastcall Changing(void* value,void*) { Check(value==&model); return changing; }
bool __fastcall Initialized(void* value,void*) {
    Check(value==&animator);
    if(cancel_on_init) active->CancelPending();
    return initialized;
}
int __fastcall Hash(std::string* name,void*) { Check(*name=="待机" || *name=="attack_loop" || *name==successor_name); ++hashes; return *name=="待机"?71:*name==successor_name?94:93; }
void* __fastcall ResolveName(void* value,int hash,void*){Check(value==&animator && hash==94);if(cancel_on_resolve)active->CancelPending();return &successor_name;}
int __fastcall NameLength(std::string* text,void*){return static_cast<int>(text->size());}
std::uint16_t __fastcall NameChar(std::string* text,int index,void*){return static_cast<unsigned char>(text->at(index));}
bool __fastcall Has(void* value,int layer,int hash,void*) {
    Check(value==&animator && layer==expected_layer && (hash==71 || hash==93 || hash==94));
    if(dispose_on_has) api::TrackedEntities().Disposed(api::TrackedEntities().Current(own));
    if(cancel_on_has) active->CancelPending();
    return exists;
}
void __fastcall Play(void* value,int hash,int layer,float time,void*) {
    Check(value==&animator && (hash==71 || hash==93) && layer==expected_layer && time==expected_start); ++plays;
    if(requeue) {
        requeue=false; active->Queue(framework::NpcMotion{912});
        active->Service(active_frame,true,*active_objects); // must not recurse
    }
    if(fault) RaiseException(0xE0680001,0,0,nullptr);
}
void __fastcall Npc(void* value,unsigned id,void*) { Check(value==&style && id==912); ++npc_plays; }
void __fastcall State(void* value,int hash,float first,float second,void*) {
    Check(value==&style && (hash==93 || hash==71 || hash==94) && first==expected_blend && second==expected_start); ++state_plays;
}
void __fastcall CrossFade(void* value,int hash,float blend,int layer,float start,float progress,void*) {
    Check(value==&animator && hash==93 && layer==expected_layer && blend==expected_blend && start==expected_start && progress==0); ++plays;
}
void __fastcall StateInfo(void* value,int layer,int index,unsigned char* bytes,void*) {
    Check(value==&animator && (layer==0 || layer==3) && index==0);std::memset(bytes,0,36);
    std::memcpy(bytes+8,&current_full_hash,4);
    std::memcpy(bytes,&current_hash,4);std::memcpy(bytes+12,layer==0?&current_time:&face_time,4);int loop=looping?1:0;std::memcpy(bytes+32,&loop,4);
}
bool __fastcall Transition(void* value,int layer,void*) {Check(value==&animator && (layer==0 || layer==3));return transitioning;}
struct Objects:api::ManagedObjects {
    api::ObjectResult ManagedType(const char* ns,const char* name) override {
        Check(std::string(ns)=="RPG.GameCore");
        if(std::string(name)=="CharacterModelComponent") return {{true,0},&model_type};
        Check(std::string(name)=="FreeStyleComponent"); return {{true,0},&style_type};
    }
    api::ObjectResult NewStringUtf8(const char* value) override {
        strings.push_back(std::make_unique<std::string>(value)); return {{true,0},strings.back().get()};
    }
    api::ObjectResult AllocateObject(const char*,const char*) override { throw std::runtime_error("unexpected allocation"); }
};
}
void TestActionPlayback() {
    const api::ReferenceMethods refs{M(Keep),M(Target),M(Free),{}, {}};
    const api::EntityAccess entities({M(Id),M(World),M(Loaded),M(Component),{},{},{},{}});
    api::ModelAccessMethods model_methods; model_methods.animator=M(Animator); model_methods.changing=M(Changing);
    api::AnimationControlMethods actions; actions.hash=M(Hash); actions.has_state=M(Has); actions.play=M(Play);
    actions.npc_motion=M(Npc); actions.free_style_state=M(State);
    actions.cross_fade=M(CrossFade);
    actions.state_info=M(StateInfo);actions.in_transition=M(Transition);
    api::ProfileInputMethods profile; profile.animator_initialized=M(Initialized);
    Objects objects; active_objects=&objects;
    active_frame.world=owner_world; active_frame.disposing={false,{true,0}};
    auto foreign=active_frame; foreign.world=reinterpret_cast<api::GameWorld*>(1);
    auto token=api::TrackedEntities().Track(own);
    auto animations=[&](api::EntityToken owner){return std::make_unique<api::ActorAnimationSession>(owner,refs,entities,api::ModelAccess(model_methods),api::AnimationControl(actions),api::ProfileInputs(profile));};
    bool missing=false,mismatched=false;
    try{framework::ActionPlayback invalid(token,owner_world,nullptr);}catch(const std::invalid_argument&){missing=true;}
    auto other=token;++other.generation;
    try{framework::ActionPlayback invalid(token,owner_world,animations(other));}catch(const std::invalid_argument&){mismatched=true;}
    Check(missing && mismatched && roots.empty() && reads==0 && plays==0);
    framework::ActionPlayback playback(token,owner_world,animations(token));
    active=&playback;
    playback.Queue(framework::AnimatorState{"待机"});
    playback.Service(foreign,true,objects); playback.Service(active_frame,false,objects);
    Check(reads==0 && plays==0 && playback.Status().pending);
    initialized=false; playback.Service(active_frame,true,objects);
    Check(playback.Status().last->outcome==framework::ActionOutcome::Waiting && playback.Status().pending && hashes==0);
    initialized=true; requeue=true; playback.Service(active_frame,true,objects);
    Check(plays==1 && npc_plays==0 && playback.Status().last->serial==1 && playback.Status().latest_serial==2 && playback.Status().pending);
    playback.Service(active_frame,true,objects); Check(npc_plays==1 && !playback.Status().pending);
    auto before=reads; playback.Service(active_frame,true,objects); Check(reads==before);
    playback.Queue(framework::FreeStyleState{"attack_loop"}); playback.Service(active_frame,true,objects);
    Check(state_plays==1 && playback.Status().last->outcome==framework::ActionOutcome::Returned);
    // Actual current Animator queried every request; old availability is not a cache.
    exists=false; playback.Queue(framework::AnimatorState{"待机"}); playback.Service(active_frame,true,objects);
    Check(plays==1 && playback.Status().last->outcome==framework::ActionOutcome::StateAbsent);
    exists=true; fault=true; playback.Queue(framework::AnimatorState{"待机"}); playback.Service(active_frame,true,objects);
    Check(plays==2 && playback.Status().last->call.exception==0xE0680001);
    playback.Service(active_frame,true,objects); Check(plays==2); fault=false;
    playback.Queue(framework::AnimatorState{"待机"}); playback.Queue(framework::NpcMotion{912});
    initialized=false; playback.Service(active_frame,true,objects); initialized=true;
    Check(npc_plays==2); // NPC motion does not require a guessed Animator sentinel.
    initialized=false; cancel_on_init=true;
    playback.Queue(framework::AnimatorState{"待机"}); playback.Service(active_frame,true,objects);
    Check(!playback.Status().pending && playback.Status().last->outcome==framework::ActionOutcome::Cancelled);
    initialized=true; cancel_on_init=false; cancel_on_has=true;
    playback.Queue(framework::AnimatorState{"待机"}); playback.Service(active_frame,true,objects);
    Check(!playback.Status().pending && plays==2 && playback.Status().last->outcome==framework::ActionOutcome::Cancelled);
    cancel_on_has=false;
    playback.Queue(framework::FreeStyleState{"attack_loop"}); dispose_on_has=true;
    playback.Service(active_frame,true,objects); dispose_on_has=false;
    Check(state_plays==1 && playback.Status().last->outcome==framework::ActionOutcome::OwnerEnded);
    before=reads; playback.Queue(framework::NpcMotion{912}); playback.Service(foreign,true,objects);
    Check(reads==before && playback.Status().last->outcome==framework::ActionOutcome::OwnerEnded);
    Check(playback.Close(foreign) && roots.empty()); api::TrackedEntities().Forget(token);
    token=api::TrackedEntities().Track(own);
    actions.resolve_hash=M(ResolveName);
    api::ComponentConfigMethods text_methods;text_methods.string_length=M(NameLength);text_methods.string_character=M(NameChar);
    const api::ComponentConfig text(text_methods);
    framework::ActionPlayback completion(token,owner_world,animations(token),{},&text);
    active=&completion;active_frame.sequence=1;completion.SetCompletionState("待机");
    const auto preceding=completion.Queue(framework::FreeStyleState{"attack_loop"});completion.Service(active_frame,true,objects);
    auto count=state_plays;current_time=1.2f;
    completion.Service(active_frame,true,objects);Check(state_plays==count); // no engine boundary
    active_frame.sequence=2;current_time=0.4f;completion.Service(active_frame,true,objects);Check(state_plays==count);
    active_frame.sequence=3;current_time=1.2f;completion.Service(active_frame,true,objects);
    Check(state_plays==count+1 && std::get<framework::FreeStyleState>(completion.Status().last->request).name=="待机");
    Check(completion.Status().successor && completion.Status().successor->returned &&
        completion.Status().successor->predecessor==preceding && completion.Status().successor->state=="待机");
    completion.Service(active_frame,true,objects);Check(state_plays==count+1); // exactly once
    completion.Queue(framework::FreeStyleState{"attack_loop"});Check(!completion.Status().successor);completion.Service(active_frame,true,objects);
    count=state_plays;looping=true;++active_frame.sequence;completion.Service(active_frame,true,objects);Check(state_plays==count && !completion.Status().completion_pending);looping=false;
    completion.Queue(framework::FreeStyleState{"attack_loop"});completion.Service(active_frame,true,objects);
    ++active_frame.sequence;current_time=0.5f;completion.Service(active_frame,true,objects);
    count=state_plays;transitioning=true;++active_frame.sequence;completion.Service(active_frame,true,objects);Check(state_plays==count && completion.Status().completion_pending);transitioning=false;
    current_hash=71;++active_frame.sequence;completion.Service(active_frame,true,objects);
    Check(state_plays==count && completion.Status().successor && completion.Status().successor->observed &&
        !completion.Status().successor->returned && completion.Status().successor->state=="待机");
    current_hash=93;current_time=0.2f;
    const auto source_serial=completion.Queue(framework::FreeStyleState{"attack_loop"});completion.Service(active_frame,true,objects);
    ++active_frame.sequence;completion.Service(active_frame,true,objects);count=state_plays;
    current_hash=94;++active_frame.sequence;completion.Service(active_frame,true,objects);
    Check(completion.Status().completion_pending && !completion.Status().successor && state_plays==count); // intermediate is not yet persisted
    current_hash=95;current_full_hash=94;looping=true;++active_frame.sequence;completion.Service(active_frame,true,objects);looping=false;current_full_hash=0;
    Check(!completion.Status().completion_pending && state_plays==count && completion.Status().successor &&
        completion.Status().successor->predecessor==source_serial && completion.Status().successor->controller_state &&
        completion.Status().successor->state==successor_name && completion.Status().last->serial==source_serial);
    current_hash=93;current_time=0.2f;
    completion.Queue(framework::FreeStyleState{"attack_loop"});completion.Service(active_frame,true,objects);
    ++active_frame.sequence;completion.Service(active_frame,true,objects);count=state_plays;
    current_hash=94;looping=true;cancel_on_resolve=true;++active_frame.sequence;completion.Service(active_frame,true,objects);
    Check(!completion.Status().successor && state_plays==count);cancel_on_resolve=false;looping=false;
    completion.Queue(framework::FreeStyleState{successor_name,0,0,{},true});completion.Service(active_frame,true,objects);
    Check(!completion.Status().completion_pending && state_plays==count+1); // restoring observed destination does not force the initial idle
    exists=false;completion.Queue(framework::FreeStyleState{successor_name,0,0,{},true});completion.Service(active_frame,true,objects);exists=true;
    Check(completion.Status().last->outcome==framework::ActionOutcome::StateAbsent && state_plays==count+1);
    current_hash=93;
    completion.Queue(framework::FreeStyleState{"attack_loop"});completion.Service(active_frame,true,objects);
    count=state_plays;completion.CancelPending();++active_frame.sequence;current_time=2;completion.Service(active_frame,true,objects);Check(state_plays==count);
    expected_layer=3;
    completion.Queue(framework::AnimatorState{"attack_loop",3,0});completion.Service(active_frame,true,objects);
    Check(completion.Status().last->outcome==framework::ActionOutcome::Returned && !completion.Status().completion_pending);
    ++active_frame.sequence;completion.Service(active_frame,true,objects);Check(state_plays==count);expected_layer=0;
    Check(completion.Close(active_frame) && roots.empty());api::TrackedEntities().Forget(token);
    token=api::TrackedEntities().Track(own);
    framework::ActionPlayback authored(token,owner_world,animations(token));
    auto config=std::make_shared<const framework::PresentationProfile>(framework::ParsePresentationProfile(R"({
      "format":"hksr.presentation-profile","id":"test","source":"source","initial_phase":"a",
      "phases":{"a":{"idle":"待机"},"b":{"idle":"OtherPhaseIdle"}},
      "actions":{"attack_loop":{"cues":[{"at":0.6,"phase":"b"}]},"待机":{"on_enter":"a"},"OtherPhaseIdle":{"on_enter":"b"}}
    })"));
    active=&authored;authored.SetCompletionState("待机");authored.ConfigurePresentation(config);authored.SetPresentationDomain(true);
    current_hash=93;current_time=0.1f;authored.Queue(framework::FreeStyleState{"attack_loop"});authored.Service(active_frame,true,objects);
    ++active_frame.sequence;authored.Service(active_frame,true,objects);Check(authored.Status().presentation_phase=="a");
    current_time=0.8f;transitioning=true;++active_frame.sequence;authored.Service(active_frame,true,objects);transitioning=false;
    Check(authored.Status().presentation_phase=="b" && authored.CurrentIdle()==successor_name);
    count=state_plays;current_time=1.1f;++active_frame.sequence;authored.Service(active_frame,true,objects);
    Check(state_plays==count+1 && authored.Status().successor && authored.Status().successor->state==successor_name);
    exists=false;authored.Queue(framework::FreeStyleState{"待机"});authored.Service(active_frame,true,objects);exists=true;
    Check(authored.Status().last->outcome==framework::ActionOutcome::StateAbsent && authored.Status().presentation_phase=="b");
    authored.SetPresentationDomain(false);Check(authored.Status().presentation_phase=="a");
    Check(authored.Close(active_frame) && roots.empty());api::TrackedEntities().Forget(token);
    token=api::TrackedEntities().Track(own);unsigned exit_preparations=0;bool cancel_during_exit=false;
    api::EffectRequestServices effects{refs,api::EffectAccess({}),{},{},{},{},{},{},{}};
    effects.exit_events=[&](api::EntityToken actual,const std::string& state,const std::vector<api::EffectRemovalSource>& sources) {
        Check(actual.entity==own && state=="attack_loop" && sources.size()==1);++exit_preparations;
        if(cancel_during_exit)active->CancelPending();return std::unique_ptr<api::EventRegistrationLease>{};
    };
    framework::ActionPlayback retirement(token,owner_world,animations(token),std::make_shared<api::PresentationEffects>(effects));
    active=&retirement;framework::FreeStyleState configured{"attack_loop"};configured.retirements.push_back({"attack_loop",{}});
    count=state_plays;retirement.Queue(configured);retirement.Service(active_frame,true,objects);
    Check(exit_preparations==1 && state_plays==count+1); // absent supplement does not suppress independent body playback
    exists=false;retirement.Queue(configured);retirement.Service(active_frame,true,objects);exists=true;
    Check(exit_preparations==1); // missing state must not install an unrelated exit group
    cancel_during_exit=true;retirement.Queue(configured);retirement.Service(active_frame,true,objects);
    Check(exit_preparations==2 && state_plays==count+1 && retirement.Status().last->outcome==framework::ActionOutcome::Cancelled);
    Check(retirement.Close(active_frame) && roots.empty());api::TrackedEntities().Forget(token);
    token=api::TrackedEntities().Track(own);cancel_during_exit=false;
    auto program_data=std::make_shared<framework::VisualProgram>(framework::ParseVisualProgram(R"({
      "format":"hksr.visual-program","id":"test","source":"source","scope":"scene","effects":{},
      "phases":{"enhanced":{"root":{"kind":"global_modifier","name":"Modifier"},"path":[{"owner":"RPG.GameCore.Change","field":"PhaseAnimConfig"}]}},
      "contexts":{"phase":{"phase":"enhanced"}},
      "actions":{"attack":{"state":"attack_loop","restart":true,"complete_at":0.7,"completion_state":"待机",
        "enter":[{"op":"context_enter","name":"phase","source":"phase"},
                 {"op":"timeline_play","source":"scene","key":"scene","path":"scene.playable"}],
        "after_completion":[{"op":"timeline_stop","source":"tail","key":"scene"}],
        "lanes":[[{"at":0.5,"instructions":[{"op":"assign","source":"phase","variable":"value","value":1}]}]]},
        "alternative":{"state":"attack_loop","restart":true,"enter":[
          {"op":"timeline_play","source":"alternative","key":"other","path":"other.playable","when":{"choice":"variant","is":"selected"}}]}}
    })"));
    bool scene_ready=false,scene_alive=false;unsigned stopped_after_plays=0;std::string timeline_path;
    framework::VisualProgramServices services;services.effects=std::make_shared<api::PresentationEffects>(effects);
    services.create=[](auto&,const auto&,const auto&)->framework::VisualEffectId{throw std::logic_error("no effect creation expected");};
    services.timeline=[&](auto,const auto& op){scene_alive=op.play;timeline_path=op.path;if(!op.play)stopped_after_plays=state_plays;
        return framework::VisualStepResult{scene_ready?framework::VisualStepState::Done:framework::VisualStepState::Pending,{}};};
    services.cancel_timelines=[&](auto){if(scene_alive)stopped_after_plays=state_plays;scene_alive=false;};
    services.timelines_drained=[&]{return !scene_alive;};
    auto phase_data=std::make_shared<api::PhaseVisualConfigSources>();phase_data->visuals.context_available=true;
    phase_data->standby={true,"attack_loop",{}};
    auto& defaults=phase_data->visuals.defaults;auto& override_rows=phase_data->visuals.character;
    defaults.loaded=override_rows.loaded=true;defaults.behaviors.enumerated=override_rows.behaviors.enumerated=true;
    defaults.source="global";override_rows.source="phase";
    api::VisualBehaviorInput base_row;base_row.flag={true,28,{}};base_row.emotion={true,"",{}};
    base_row.occur.resolved_state={true,"attack_loop",{}};base_row.loop.resolved_state={true,"",{}};
    base_row.occur.transition={true,0,{}};defaults.behaviors.rows.push_back(base_row);
    auto phase_row=base_row;phase_row.occur.resolved_state={true,"待机",{}};phase_row.emotion={true,"not-the-expression-source",{}};
    override_rows.behaviors.rows.push_back(phase_row);
    services.read_phase=[&](const auto&){return phase_data;};
    auto player=std::make_shared<framework::VisualProgramPlayer>(program_data,std::move(services));
    framework::ActionPlayback integrated(token,owner_world,animations(token),std::make_shared<api::PresentationEffects>(effects));
    auto conflicting=std::make_shared<framework::PresentationProfile>(*config);
    conflicting->actions["attack_loop"].enter_phase="b";
    integrated.ConfigurePresentation(conflicting);
    integrated.ConfigureVisualProgram(player);integrated.SetPresentationDomain(true);integrated.SetCompletionState("待机");active=&integrated;
    const framework::ProgramAction selected_program{"test","attack"};
    count=state_plays;const auto exits_before=exit_preparations;
    integrated.Queue(selected_program);integrated.Service(active_frame,true,objects);
    Check(integrated.Status().pending && state_plays==count && exit_preparations==exits_before);
    scene_ready=true;integrated.Service(active_frame,true,objects);
    Check(state_plays==count+1 && !integrated.Status().pending && !integrated.Status().completion_pending);
    Check(exit_preparations==exits_before); // program owns its cross-action exits
    Check(std::get<framework::ProgramAction>(integrated.Status().last->request).action=="attack");
    Check(integrated.Status().presentation_phase=="a"); // explicit program phases exclude competing profile rules
    current_hash=93;current_time=0.8f;current_full_hash=0;
    integrated.Service(active_frame,true,objects);Check(state_plays==count+1);
    ++active_frame.sequence;integrated.Service(active_frame,true,objects);
    Check(state_plays==count+2 && scene_alive && player->Status().completing);
    Check(integrated.Status().successor && integrated.Status().successor->returned);
    ++active_frame.sequence;integrated.Service(active_frame,true,objects);
    Check(scene_alive && !player->Status().completed); // successful call is not observed state entry
    current_hash=71;current_time=0;++active_frame.sequence;integrated.Service(active_frame,true,objects);
    Check(stopped_after_plays==state_plays && !scene_alive && player->Status().completed);
    const auto phase=player->ContextToken("phase");Check(phase);
    Check(integrated.CurrentIdle()=="attack_loop");
    auto visual=framework::ResolveVisualState(phase_data->visuals,28);Check(visual && visual->source=="phase" && visual->expression.value.empty());
    visual->value=base_row; // A menu selection prepared before phase entry.
    integrated.Queue(*visual);integrated.Service(active_frame,true,objects);
    Check(integrated.Status().last->outcome==framework::ActionOutcome::Returned && integrated.Status().last->hash.value==71);
    integrated.Queue(framework::FreeStyleState{"待机"});integrated.Service(active_frame,true,objects);
    Check(player->ContextToken("phase")==phase && !scene_alive); // unlisted body state is not phase exit
    // The controller may enter the declared successor itself. Observe it;
    // do not replay StandBy or cancel its tail merely because the source ended.
    auto& attack=program_data->actions.at("attack");attack.complete_at=2;
    integrated.Queue(selected_program);current_hash=93;current_time=0.6f;integrated.Service(active_frame,true,objects);
    ++active_frame.sequence;integrated.Service(active_frame,true,objects);Check(player->Status().finished);
    const auto before_official=state_plays;
    current_hash=71;current_time=0;++active_frame.sequence;integrated.Service(active_frame,true,objects);
    Check(state_plays==before_official && player->Status().completed && !scene_alive && integrated.Status().successor->observed);
    attack.complete_at=0.7f;
    // A failed explicit successor must close, rather than leave an untracked
    // old scene alive indefinitely.
    integrated.Queue(selected_program);current_hash=93;current_time=0.1f;integrated.Service(active_frame,true,objects);
    exists=false;current_time=0.8f;++active_frame.sequence;integrated.Service(active_frame,true,objects);exists=true;
    Check(integrated.Status().last->outcome==framework::ActionOutcome::StateAbsent);
    ++active_frame.sequence;integrated.Service(active_frame,true,objects);Check(!scene_alive);
    count=state_plays;
    integrated.Queue(selected_program);current_time=0.1f;integrated.Service(active_frame,true,objects);
    Check(state_plays==count+1 && scene_alive); // same model can enter again
    integrated.Queue(framework::ProgramAction{"test","alternative",{{"variant","selected"}}});integrated.Service(active_frame,true,objects);
    Check(player->Status().action=="alternative" && timeline_path=="other.playable" && scene_alive);
    Check(std::get<framework::ProgramAction>(integrated.Status().last->request).choices.at("variant")=="selected");
    const auto alternative_intent=player->Status().intent;count=state_plays;
    integrated.Queue(framework::ProgramAction{"test","absent"});integrated.Service(active_frame,true,objects);
    Check(integrated.Status().last->outcome==framework::ActionOutcome::Unavailable && state_plays==count && scene_alive);
    integrated.Queue(framework::ProgramAction{"foreign","attack"});integrated.Service(active_frame,true,objects);
    Check(integrated.Status().last->outcome==framework::ActionOutcome::Unavailable && player->Status().intent==alternative_intent && scene_alive);
    // Direct playback of a matching body state does not select either program.
    integrated.Queue(framework::FreeStyleState{"attack_loop"});integrated.Service(active_frame,true,objects);
    Check(!scene_alive && !player->Status().playing && state_plays==count+1);
    integrated.Queue(selected_program);integrated.Service(active_frame,true,objects);Check(scene_alive);
    integrated.SetPresentationDomain(false);integrated.Service(active_frame,true,objects);Check(!scene_alive && player->ActiveContexts().empty());
    Check(integrated.CurrentIdle()=="待机");
    Check(integrated.Close(active_frame) && player->Drained() && roots.empty());api::TrackedEntities().Forget(token);
    // Without program-owned phases, use the existing profile's phase policy
    // for the resolved body, but never infer its automatic replay/return.
    token=api::TrackedEntities().Track(own);
    auto simple_program=std::make_shared<framework::VisualProgram>(framework::ParseVisualProgram(R"({
      "format":"hksr.visual-program","id":"simple","source":"source","scope":"scene","effects":{},
      "actions":{"play":{"state":"attack_loop","restart":true}}
    })"));
    framework::VisualProgramServices simple_services;
    simple_services.effects=std::make_shared<api::PresentationEffects>(effects);
    simple_services.create=[](auto&,const auto&,const auto&)->framework::VisualEffectId{throw std::logic_error("no effects in phase fixture");};
    auto simple_player=std::make_shared<framework::VisualProgramPlayer>(simple_program,simple_services);
    auto simple_profile=std::make_shared<framework::PresentationProfile>(*config);
    simple_profile->actions["attack_loop"].enter_phase="b";
    simple_profile->actions["attack_loop"].cues={{0.5f,"a"}};
    simple_profile->actions["attack_loop"].complete_phase="b";
    framework::ActionPlayback simple(token,owner_world,animations(token),simple_services.effects);
    simple.ConfigurePresentation(simple_profile);simple.ConfigureVisualProgram(simple_player);
    simple.SetPresentationDomain(true);simple.SetCompletionState("待机");active=&simple;
    count=state_plays;current_hash=93;current_time=0.1f;current_full_hash=0;
    simple.Queue(framework::ProgramAction{"simple","play"});simple.Service(active_frame,true,objects);
    Check(simple.BranchState().phase=="b" && simple.CurrentIdle()==successor_name && state_plays==count+1);
    ++active_frame.sequence;current_time=0.6f;simple.Service(active_frame,true,objects);
    Check(simple.BranchState().phase=="a" && state_plays==count+1);
    ++active_frame.sequence;current_time=1.1f;simple.Service(active_frame,true,objects);
    Check(simple.BranchState().phase=="b" && !simple.Status().completion_pending && state_plays==count+1);
    simple.SetPresentationDomain(false);Check(simple.BranchState().phase.empty());
    Check(simple.Close(active_frame) && simple_player->Drained() && roots.empty());api::TrackedEntities().Forget(token);
    // The task adapter uses the same SDK session and normal owner-frame path.
    // A returned native call must not finish an animation wait by itself.
    token=api::TrackedEntities().Track(own);
    framework::ActionPlayback task_player(token,owner_world,animations(token),simple_services.effects);
    active=&task_player;task_player.SetCompletionState("待机");
    auto task_visual=std::make_shared<framework::VisualProgramPlayer>(std::make_shared<framework::VisualProgram>(),simple_services);
    auto sequence=std::make_shared<framework::TaskSequence>();
    sequence->nodes={
        {"body",framework::TaskSubmit{"body",framework::TaskAnimationCommand{{"body"},"attack_loop"}}},
        {"wait",framework::TaskWaitRequest{"body"}},
        {"entry",framework::TaskSerial{{0,1}}}
    };sequence->entry=2;
    framework::TaskPresentationServices task_services;
    task_services.animation=[&](const auto& participant)->std::optional<framework::TaskAnimationEndpoint>{
        Check(participant.name=="body");return framework::BindTaskAnimation(task_player);
    };
    framework::TaskPresentation task({100},token.generation,sequence,task_visual,{},task_services);
    count=state_plays;current_time=0;current_hash=93;
    task.Service(0,++active_frame.sequence);Check(state_plays==count && !task.Drained());
    task_player.Service(active_frame,false,objects);Check(state_plays==count);
    task_player.Service(active_frame,true,objects);Check(state_plays==count+1 && !task_player.Status().completion_pending);
    task.Service(0,active_frame.sequence);Check(!task.Drained());
    current_time=0.4f;++active_frame.sequence;task_player.Service(active_frame,true,objects);task.Service(0.1,active_frame.sequence);
    Check(!task.Drained());
    current_time=1.2f;++active_frame.sequence;task_player.Service(active_frame,true,objects);task.Service(0.1,active_frame.sequence);
    task.Service(0,++active_frame.sequence);Check(task.Drained() && state_plays==count+1); // no implicit idle replay
    // A task may deliberately leave a clip at an observed cue, before its
    // natural end. Exercise the real request adapter and SDK session together.
    auto cue_plan=std::make_shared<framework::TaskSequence>();
    cue_plan->nodes={
        {"body",framework::TaskSubmit{"body",framework::TaskAnimationCommand{{"body"},"attack_loop"}}},
        {"cue",framework::TaskWaitRequest{"body",framework::TaskWaitSignal::Progress,0.931}},
        {"return",framework::TaskSubmit{"return",framework::TaskAnimationCommand{{"body"},"待机"}}},
        {"return-accepted",framework::TaskWaitRequest{"return",framework::TaskWaitSignal::RequestComplete}},
        {"observe-handoff",framework::TaskWaitFrame{}},
        {"entry",framework::TaskSerial{{0,1,2,3,4}}}};cue_plan->entry=5;
    framework::TaskPresentation cue_task({101},token.generation,cue_plan,task_visual,{},task_services);
    count=state_plays;current_hash=93;current_time=0;
    cue_task.Service(0,++active_frame.sequence);task_player.Service(active_frame,true,objects);cue_task.Service(0,active_frame.sequence);
    current_time=0.95f;++active_frame.sequence;task_player.Service(active_frame,true,objects);cue_task.Service(0.1,active_frame.sequence);
    const auto cue_requests=cue_task.Requests();
    const auto old_body=std::find_if(cue_requests.begin(),cue_requests.end(),[](const auto& row){return row.name=="body";});
    Check(old_body!=cue_requests.end() && old_body->state.superseded && !old_body->state.performance_complete &&
        old_body->state.progress && *old_body->state.progress>=0.931 && *old_body->state.progress<1);
    ++active_frame.sequence;task_player.Service(active_frame,true,objects);cue_task.Service(0.1,active_frame.sequence);
    cue_task.Service(0,++active_frame.sequence);
    Check(cue_task.Drained() && cue_task.Status().reason==framework::TaskExitReason::Completed &&
        cue_task.Status().issues.empty() && state_plays==count+2);
    framework::TaskPresentation interrupted({102},token.generation,sequence,task_visual,{},task_services);
    current_hash=93;current_time=0.2f;
    interrupted.Service(0,++active_frame.sequence);task_player.Service(active_frame,true,objects);interrupted.Service(0,active_frame.sequence);
    auto outside=task_player.QueueAnimation({"待机"});
    interrupted.Service(0,++active_frame.sequence);
    Check(interrupted.Status().reason==framework::TaskExitReason::Failed && !interrupted.Status().issues.empty());
    task_player.Service(active_frame,true,objects);Check(outside->accepted && !outside->released);
    task_player.ReleaseAnimation(outside->serial);
    auto first=task_player.QueueAnimation({"attack_loop"});
    ++active_frame.sequence;task_player.Service(active_frame,true,objects);Check(first->accepted && !first->entered);
    task_player.ReleaseAnimation(first->serial);auto second=task_player.QueueAnimation({"待机"});
    task_player.ReleaseAnimation(first->serial); // stale cleanup does not cancel replacement
    ++active_frame.sequence;task_player.Service(active_frame,true,objects);Check(second->accepted && !second->released);
    changing=true;++active_frame.sequence;task_player.Service(active_frame,true,objects);changing=false;
    Check(second->cancelled && second->released && !second->error.empty());
    const auto old_model=second->model_generation;
    auto third=task_player.QueueAnimation({"待机"});++active_frame.sequence;task_player.Service(active_frame,true,objects);
    Check(third->accepted && third->model_generation>old_model);
    task_player.ReleaseAnimation(third->serial);
    current_hash=93;current_time=0.1f;face_time=0.1f;
    auto body_ticket=task_player.QueueAnimation({"attack_loop"});++active_frame.sequence;task_player.Service(active_frame,true,objects);
    expected_layer=3;auto face_ticket=task_player.QueueAnimation({"attack_loop",3});
    ++active_frame.sequence;task_player.Service(active_frame,true,objects);expected_layer=0;
    Check(face_ticket->accepted && !body_ticket->released);
    current_time=1.1f;++active_frame.sequence;task_player.Service(active_frame,true,objects);
    Check(body_ticket->complete && face_ticket->entered && !face_ticket->complete);
    face_time=1.1f;++active_frame.sequence;task_player.Service(active_frame,true,objects);Check(face_ticket->complete);
    task_player.ReleaseAnimation(body_ticket->serial);task_player.ReleaseAnimation(face_ticket->serial);
    // Authored offsets reach each actual writer, but do not manufacture entry
    // or progress before the Animator reports it. Completion never queues idle.
    expected_start=0.45f;expected_blend=0.125f;current_time=0.46f;
    auto offset_body=task_player.QueueAnimation({"attack_loop",0,expected_blend,expected_start});
    count=state_plays;++active_frame.sequence;task_player.Service(active_frame,true,objects);
    Check(offset_body->accepted && !offset_body->entered && !offset_body->progress && state_plays==count+1);
    ++active_frame.sequence;task_player.Service(active_frame,true,objects);
    Check(offset_body->entered && offset_body->progress && std::abs(*offset_body->progress-0.46)<0.00001);
    bool invalid_start=false;
    try { task_player.QueueAnimation({"attack_loop",0,0,std::numeric_limits<float>::quiet_NaN()}); }
    catch(const std::invalid_argument&) { invalid_start=true; }
    Check(invalid_start && !offset_body->released); // rejected input cannot cancel current playback
    task_player.ReleaseAnimation(offset_body->serial);
    expected_layer=3;
    auto offset_fade=task_player.QueueAnimation({"attack_loop",3,expected_blend,expected_start});
    ++active_frame.sequence;task_player.Service(active_frame,true,objects);Check(offset_fade->accepted);
    task_player.ReleaseAnimation(offset_fade->serial);
    expected_blend=0;
    auto offset_play=task_player.QueueAnimation({"attack_loop",3,0,expected_start});
    ++active_frame.sequence;task_player.Service(active_frame,true,objects);Check(offset_play->accepted);
    task_player.ReleaseAnimation(offset_play->serial);
    expected_layer=0;expected_start=0;
    // Request End, clip completion and state departure are separate clocks.
    // Keep observing after a non-loop clip reaches 1: a source End may be later.
    current_hash=93;current_time=0.2f;
    auto bounded=task_player.QueueAnimation({"attack_loop",0,0,0,0.6f});
    ++active_frame.sequence;task_player.Service(active_frame,true,objects);
    Check(bounded->accepted && !bounded->end_reached && !bounded->state_left);
    current_time=0.61f;++active_frame.sequence;task_player.Service(active_frame,true,objects);
    Check(bounded->end_reached && !bounded->complete && !bounded->state_left);
    current_time=1.1f;++active_frame.sequence;task_player.Service(active_frame,true,objects);
    Check(bounded->complete && !bounded->state_left);
    current_hash=7;++active_frame.sequence;task_player.Service(active_frame,true,objects);
    Check(bounded->state_left && !bounded->cancelled);
    task_player.ReleaseAnimation(bounded->serial);
    current_hash=93;current_time=0.2f;
    auto extended=task_player.QueueAnimation({"attack_loop",0,0,0,1.5f});
    ++active_frame.sequence;task_player.Service(active_frame,true,objects);
    current_time=1.1f;++active_frame.sequence;task_player.Service(active_frame,true,objects);
    Check(extended->complete && !extended->end_reached);
    current_time=1.6f;++active_frame.sequence;task_player.Service(active_frame,true,objects);
    Check(extended->end_reached && extended->progress && *extended->progress>1.5 && !extended->state_left);
    auto replacement=task_player.QueueAnimation({"待机"});
    Check(extended->cancelled && !extended->state_left && !extended->error.empty());
    task_player.ReleaseAnimation(replacement->serial);
    current_hash=93;current_time=0.1f;
    auto early_exit=task_player.QueueAnimation({"attack_loop",0,0,0,1.5f});
    ++active_frame.sequence;task_player.Service(active_frame,true,objects);
    ++active_frame.sequence;task_player.Service(active_frame,true,objects);
    current_hash=7;++active_frame.sequence;task_player.Service(active_frame,true,objects);
    Check(early_exit->state_left && early_exit->end_reached && early_exit->progress.value()<1.5);
    task_player.ReleaseAnimation(early_exit->serial);
    current_hash=93;current_time=0;
    auto immediate_end=task_player.QueueAnimation({"attack_loop",0,0,0,-1.0f});
    ++active_frame.sequence;task_player.Service(active_frame,true,objects);
    Check(immediate_end->accepted && !immediate_end->end_reached);
    ++active_frame.sequence;task_player.Service(active_frame,true,objects);
    Check(immediate_end->entered && immediate_end->end_reached && !immediate_end->complete);
    task_player.ReleaseAnimation(immediate_end->serial);
    // Cancellation during a managed read cannot reach the playback write.
    const auto before_cancel=state_plays;
    auto cancelled=task_player.QueueAnimation({"attack_loop"});cancel_on_has=true;
    ++active_frame.sequence;task_player.Service(active_frame,true,objects);cancel_on_has=false;
    Check(cancelled->released && !cancelled->accepted && state_plays==before_cancel);
    auto departed=task_player.QueueAnimation({"attack_loop"});task_player.Service(foreign,true,objects);
    Check(departed->released && !departed->accepted && state_plays==before_cancel);
    Check(task_player.Close(active_frame) && roots.empty());task_visual->End();task_visual->Service();
    Check(task_visual->Drained());api::TrackedEntities().Forget(token);
    std::cout<<"Action playback passed: owned lookup, queued replacement, readiness, three official paths, current state membership, no replay after faults, disposal isolation; local only.\n";
}
