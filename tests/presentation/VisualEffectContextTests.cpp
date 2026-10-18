#include <hksr/framework/presentation/VisualEffectContext.hpp>
#include <hksr/framework/presentation/VisualEffectBindings.hpp>
#include <hksr/framework/presentation/VisualEventRoutes.hpp>
#include <hksr/framework/presentation/VisualProgram.hpp>
#include <hksr/framework/presentation/VisualEntityMotion.hpp>
#include <hksr/framework/presentation/PresentationProfileFiles.hpp>
#include <array>
#include <cstring>
#include <functional>
#include <map>
#include <fstream>
#include <stdexcept>

namespace {
using namespace hksr::api;
using namespace hksr::framework;
int info,entity,other_entity,path,manager,effect,root_id=0;
struct Params {unsigned id=1;bool released=false;int state=0;void* callback=nullptr;void* target=nullptr;int removes=0;};
Params a,b,c;Params* allocating=&a;
std::map<int,void*> roots;
std::vector<std::unique_ptr<std::array<unsigned char,128>>> delegates;
EffectCompletionLayout layout{24,40,64};
std::vector<void*> controlled;
std::function<void()> on_submit;
std::function<void()> on_remove;
bool release_on_remove=false;
std::vector<EffectRemovalOptions> explicit_removals;
int default_triggers=0;
std::vector<std::pair<void*,EffectAnimatorParameter>> parameter_calls;
std::function<void()> on_parameter;
void CheckAt(bool ok,int line){if(!ok)throw std::runtime_error("visual effect context test failed at "+std::to_string(line));}
#define Check(value) CheckAt((value),__LINE__)
template<class T>Method M(T entry){return {reinterpret_cast<void*>(entry),&info};}
int __fastcall Keep(void* object,int,int,void*){roots[++root_id]=object;return root_id;}
void* __fastcall Target(int id,void*){return roots.at(id);}
void __fastcall Free(int id,void*){roots.erase(id);}
void* __fastcall Map(void*,void* name,void*){return name;}
bool __fastcall Empty(void*,void*){return false;}
EffectReference __fastcall Acquire(void*,void*){return {allocating->id,0,allocating};}
unsigned __fastcall Id(Params* p,void*){return p->id;}
bool __fastcall Released(Params* p,void*){return p->released;}
int __fastcall State(Params* p,void*){return p->state;}
void* __fastcall Instance(Params* p,void*){return p->state==3?&effect:nullptr;}
void __fastcall Submit(void* service,Params* p,void*){Check(service==&manager);p->state=1;auto callback=on_submit;if(callback)callback();}
bool __fastcall Remove(void*,EffectReference ref,bool,void*) {
    auto* p=static_cast<Params*>(ref.parameters);Check(p->id==ref.id);++p->removes;
    if(on_remove)on_remove();
    if(release_on_remove)p->released=true;
    return true;
}
void __fastcall Release(Params* p,void*){p->released=true;}
void Complete(Params& p) {
    void* entry=nullptr;void* cookie=nullptr;
    std::memcpy(&entry,static_cast<char*>(p.callback)+layout.invoke_impl,sizeof(entry));
    std::memcpy(&cookie,static_cast<char*>(p.callback)+layout.method_code,sizeof(cookie));
    reinterpret_cast<void(__fastcall*)(void*,void*,void*,void*)>(entry)(cookie,&effect,&p,nullptr);
    p.state=3;
}
std::shared_ptr<const PresentationEffects> Services() {
    EffectRequestServices requests{{M(Keep),M(Target),M(Free),{},{}}
        ,EffectAccess({M(Map),M(Acquire),M(Submit),M(Remove),M(Release),M(Id),M(Released),M(State),M(Instance),{},M(Empty)}),
        {[]{auto data=std::make_unique<std::array<unsigned char,128>>();data->fill(0);auto* p=data->data();delegates.push_back(std::move(data));return ObjectResult{{true,0},p};},layout},
        []{return ObjectResult{{true,0},&manager};},
        [](void* raw,void* target,void* callback){auto* p=static_cast<Params*>(raw);p->target=target;p->callback=callback;return EffectInputResult{{true,0},{true,0}};}};
    EffectControlServices controls;
    requests.configure_spatial=[](void* raw,void* caster,void* callback,const SpatialEffectOptions& options) {
        Check(caster==&entity && options.position.x==6 && options.position.z==58);
        auto* p=static_cast<Params*>(raw);p->target=nullptr;p->callback=callback;
        return EffectInputResult{{true,0},{true,0}};
    };
    controls.progress=[](EffectReference ref,float,float){controlled.push_back(ref.parameters);const auto* p=static_cast<Params*>(ref.parameters);
        return EffectControlResult{p->state==3?EffectControlState::Applied:EffectControlState::NotLoaded,{true,0},true,{}};};
    controls.animator=[progress=controls.progress](EffectReference ref,int,float duration,bool){return progress(ref,0,duration);};
    controls.named_animator=[animator=controls.animator](EffectReference ref,const std::string& name,float duration,bool restart) {
        Check(name=="Move" || name=="Idle");return animator(ref,name=="Move"?11:12,duration,restart);
    };
    EffectActionServices actions;
    actions.trigger=[](EffectReference ref,const std::string& name,bool reset){
        Check(name=="Fire" && !reset);const auto* p=static_cast<Params*>(ref.parameters);
        if(p->state!=3)return EffectControlResult{EffectControlState::NotLoaded,{},true,{}};
        ++default_triggers;return EffectControlResult{EffectControlState::Applied,{true,0},true,{}};
    };
    actions.remove=[](void* service,EffectReference ref,const EffectRemovalOptions& options){
        explicit_removals.push_back(options);EffectRemovalResult result;
        result.removal.value=Remove(service,ref,options.fade,nullptr);result.removal.read={true,0};result.input_release={true,0};return result;
    };
    actions.parameter=[](EffectReference ref,const EffectAnimatorParameter& value){
        const auto* p=static_cast<Params*>(ref.parameters);
        if(p->state!=3)return EffectControlResult{EffectControlState::NotLoaded,{},true,"loading"};
        parameter_calls.emplace_back(ref.parameters,value);
        if(on_parameter)on_parameter();
        return EffectControlResult{value.name=="missing"?EffectControlState::NoMatch:EffectControlState::Applied,{true,0},true,"parameter result"};
    };
    return std::make_shared<PresentationEffects>(std::move(requests),std::move(controls),
        [](void* raw,void* caster,void* target,void* callback,const EntityEffectOptions&) {
            Check(caster==&entity && target==&other_entity);auto* p=static_cast<Params*>(raw);
            p->target=target;p->callback=callback;return EffectInputResult{{true,0},{true,0}};
        },std::move(actions));
}
}
void TestVisualParameters() {
    const std::string json=R"({
      "format":"hksr.visual-program","id":"parameters","source":"fixture","scope":"scene","effects":{},
      "actions":{"enter":{"state":"Enter","restart":true,"enter":[
        {"op":"effect_parameter","effect":"flame","name":"speed","type":"float","value":0.5,"source":"float"},
        {"op":"effect_parameter","effect":"flame","name":"phase","type":"int","value":-4,"selection":"first_matching_parameter","source":"int"},
        {"op":"effect_parameter","effect":"flame","name":"active","type":"bool","value":true,"source":"bool"},
        {"op":"effect_parameter","effect":"flame","name":"fire","type":"trigger","source":"trigger"},
        {"op":"effect_parameter","effect":"flame","name":"missing","type":"float","value":1,"selection":"first_matching_parameter","source":"missing"}
      ]}}
    })";
    auto program=std::make_shared<VisualProgram>(ParseVisualProgram(json));
    auto services=Services();VisualProgramServices callbacks;callbacks.effects=services;
    callbacks.create=[](auto&,const auto&,const auto&)->VisualEffectId{throw std::runtime_error("no creation expected");};
    callbacks.attach=[&](auto context){context->Track("flame",&manager,{a.id,0,&a});context->Track("flame",&manager,{b.id,0,&b});};
    a={};a.state=3;b={};b.id=2;b.state=1;parameter_calls.clear();
    VisualProgramPlayer player(program,callbacks);player.Select("enter");player.Service();
    Check(parameter_calls.size()==1 && parameter_calls[0].first==&a && !player.Status().prepared);
    player.Service();Check(parameter_calls.size()==1); // no repeated accepted setter
    b.state=3;player.Service();Check(player.Status().prepared && player.Status().issues.empty());
    Check(parameter_calls.size()==10 && player.Status().notes.size()==2);
    Check(std::get<float>(parameter_calls[1].second.value)==0.5f && parameter_calls[1].first==&b);
    Check(std::get<int>(parameter_calls[2].second.value)==-4 && parameter_calls[2].second.selection==EffectAnimatorSelection::FirstMatchingParameter);
    Check(std::get<bool>(parameter_calls[4].second.value) && std::holds_alternative<EffectParameterTrigger>(parameter_calls[6].second.value));
    player.Service();Check(parameter_calls.size()==10); // no-match is a completed no-op
    player.End();release_on_remove=true;player.Service();Check(player.Drained() && roots.empty());
    // Interrupted callbacks cannot operate on the remaining batch or next instruction.
    a={};a.state=3;b={};b.id=2;b.state=3;parameter_calls.clear();
    VisualProgramPlayer interrupted(program,callbacks);interrupted.Select("enter");
    on_parameter=[&]{interrupted.End();interrupted.Service();};interrupted.Service();on_parameter={};
    Check(parameter_calls.size()==1);interrupted.Service();Check(interrupted.Drained() && roots.empty());
    auto rejected=[&](const std::string& from,const std::string& to){
        auto invalid=json;const auto at=invalid.find(from);Check(at!=std::string::npos);invalid.replace(at,from.size(),to);
        bool caught=false;try{ParseVisualProgram(invalid);}catch(const std::exception&){caught=true;}Check(caught);
    };
    rejected("\"value\":-4","\"value\":0.5");
    rejected("\"value\":-4","\"value\":4294967296");
    rejected("\"value\":true","\"value\":1");
    rejected("\"type\":\"trigger\"","\"type\":\"trigger\",\"value\":false");
    rejected("first_matching_parameter","all");
    a={};b={};allocating=&a;release_on_remove=false;parameter_calls.clear();
}
void TestVisualEffectContext() {
    TestVisualParameters();
    {
        const std::string json=R"({"format":"hksr.visual-program","id":"partners","source":"config","scope":"scene",
          "effects":{"persistent":{"path":"effect.prefab","target":"partner:helper","resident":true}},"restore":[],
          "partners":{"helper":{"model":"partner.prefab","config":"partner.json","member":"helper"}},
          "contexts":{"phase":{"enter":[{"op":"partner_create","partner":"helper","source":"spawn"}],
            "restore":[{"op":"partner_create","partner":"helper","source":"restore"}],"exit":[]}},
          "actions":{
            "enter":{"state":"Enter","restart":true,"enter":[{"op":"context_enter","name":"phase","source":"phase"},
              {"op":"partner_attach","context":"phase","partner":"helper","target":"Caster","self_point":"EffectScale","target_point":"Hand","source":"attach"},
              {"op":"partner_visibility","context":"phase","partner":"helper","key":"initial","visible":false,"mute_collider":true,"source":"hide"},
              {"op":"partner_animation_follow","context":"phase","partner":"helper","target":null,"source":"anim"},
              {"op":"partner_visibility_follow","context":"phase","partner":"helper","target":"Caster","visibility":"invisible","source":"visible"},
              {"op":"partner_idle","context":"phase","partner":"helper","state":"Loop","normalized_transition":0.25,"source":"idle"}]},
            "leave":{"state":"Leave","enter":[{"op":"context_exit","name":"phase","source":"exit"}]},
            "action":{"state":"Action","restart":true,"enter":[{"op":"partner_create","partner":"helper","source":"spawn"}],"completion_state":"Idle"}
          }})";
        auto program=std::make_shared<VisualProgram>(ParseVisualProgram(json));
        Check(program->partners.size()==1 && program->actions.at("enter").enter.size()==6);
        Check(program->effects.at("persistent").options.resident);
        const auto& idle=std::get<hksr::api::PartnerIdleAnimation>(std::get<VisualPartnerOperation>(program->actions.at("enter").enter.back().operation).command);
        Check(idle.state=="Loop" && idle.normalized_start==0 && idle.normalized_transition==0.25f && idle.normalized_end==1);
        bool ready=false,drained=true;unsigned submissions=0,ends=0;
        std::shared_ptr<VisualEffectContext> owner;
        VisualProgramServices services;services.effects=Services();services.choices={{"mode","original"}};
        services.create=[](auto&,const auto&,const auto&)->VisualEffectId{throw std::runtime_error("no effects expected");};
        services.partner=[&](auto context,const auto& operation) {
            owner=context;drained=false;
            if(std::holds_alternative<VisualPartnerCreate>(operation.command) && !ready)return VisualStepResult{VisualStepState::Pending,{}};
            ++submissions;return VisualStepResult{VisualStepState::Done,{}};
        };
        services.end_partners=[&](auto context){if(context==owner){++ends;owner.reset();}};
        services.partners_drained=[&]{return drained;};
        VisualProgramPlayer player(program,services);player.Select("enter");player.Service();
        Check(!player.Status().prepared && player.Status().issues.empty() && submissions==0 && ends==0);
        ready=true;player.Service();Check(player.Status().prepared && submissions==6 && ends==0);
        const auto branches=player.BranchState();
        Check(branches.values.size()==2 && branches.values[0].scope.empty() && branches.values[1].scope=="phase");
        Check(std::get<std::string>(branches.values[1].value)=="original");
        const auto saved=player.Capture();Check(saved.contexts.size()==1);
        player.Select("leave",{{"mode","next"}});
        Check(std::get<std::string>(player.BranchState().values[0].value)=="original"); // not yet consumed
        player.Service();Check(ends==1 && player.ActiveContexts().empty());
        Check(player.BranchState().values.size()==1 && std::get<std::string>(player.BranchState().values[0].value)=="next");
        player.End();player.Service();Check(!player.Drained());drained=true;player.Service();Check(player.Drained());
        VisualProgramPlayer restored(program,services);restored.Restore(saved);restored.Service();
        Check(!restored.Restoring() && submissions==7);restored.End();restored.Service();Check(ends==2);
        drained=true;restored.Service();Check(restored.Drained());
        VisualProgramPlayer action(program,services);const auto intent=action.Select("action");action.Service();
        Check(action.Played(intent,1,1));action.Observe(1,1,"Action",1);action.Service();
        Check(action.SuccessorPlayed(intent,2,1));action.Observe(2,1,"Idle",0);action.Service();
        Check(action.Status().completed && ends==3);action.End();action.Service();Check(!action.Drained());
        drained=true;action.Service();Check(action.Drained());
        auto invalid=json;const std::string from="\"visibility\":\"invisible\"";const auto at=invalid.find(from);invalid.replace(at,from.size(),"\"visibility\":\"unknown\"");
        bool rejected=false;try{ParseVisualProgram(invalid);}catch(const std::exception&){rejected=true;}Check(rejected);
    }
    {
        const auto anchors=ParseVisualSpatialAnchors(R"({"center":{"position":[6,0,58],"rotation":[0,1,0,0]}})");
        auto program=std::make_shared<VisualProgram>(ParseVisualProgram(R"({
          "format":"hksr.visual-program","id":"spatial","source":"sample","scope":"scene",
          "effects":{"ground":{"path":"ground.prefab","anchor":"center","unique_name":"ground"}},
          "actions":{"start":{"state":"Start","restart":true,"enter":[
            {"op":"create","effects":["ground"],"source":"entry"},
            {"op":"effect_state","effect":"ground","state":"Move","source":"entry"}]}},
          "signals":{"enter_view":[{"op":"effect_state","effect":"ground","state":"Idle","source":"view"}]}
        })"));
        Check(program->effects.at("ground").target.empty());
        auto services=Services();VisualProgramServices callbacks;callbacks.effects=services;
        callbacks.create=[&](auto& context,const auto& key,const auto& input){
            const auto& anchor=anchors.at(input.anchor);SpatialEffectOptions options;
            options.position=anchor.position;options.rotation=anchor.rotation;options.unique_name=input.options.unique_name;
            return context.StartSpatial(key,&entity,&path,options);
        };
        callbacks.effect_state=[](auto& context,auto id,const VisualEffectState& op){
            const auto result=context.SetAnimatorState(id,op.state,op.transition,op.restart);
            return VisualStepResult{result.state==EffectControlState::Applied?VisualStepState::Done:VisualStepState::Pending,{}};
        };
        VisualProgramPlayer player(program,callbacks);const auto intent=player.Select("start");player.Service();
        Check(!player.Status().prepared && !a.target && a.state==1);
        Complete(a);player.Service();Check(player.Status().prepared && player.Status().issues.empty());
        Check(player.Signal(intent,"enter_view")!=0);player.Service();Check(player.Status().completed_signal==1);
        player.End();release_on_remove=true;player.Service();Check(player.Drained() && roots.empty() && a.removes==1);
        a={};allocating=&a;release_on_remove=false;
        // Exit during synchronous submission still retains the spatial receipt.
        VisualEffectContext context(services);SpatialEffectOptions pose;pose.position={6,0,58};
        on_submit=[&]{context.End();context.Service();};
        const auto id=context.StartSpatial("ground",&entity,&path,pose);on_submit={};
        Check(context.Status(id)->cancelled);Complete(a);release_on_remove=true;context.Service();
        Check(context.Drained() && roots.empty());
        a={};allocating=&a;release_on_remove=false;
    }
    auto services=Services();VisualEffectContext scene(services),aura(services),another_actor(services);
    const auto scene_id=scene.Start("PK",&entity,&path);
    allocating=&b;const auto aura_id=aura.Start("PK",&entity,&path);
    allocating=&c;const auto other_id=another_actor.Start("PK",&other_entity,&path);
    Check(scene.Status(scene_id)->submitted && aura.Status(aura_id)->submitted && another_actor.Status(other_id)->submitted);
    Check(scene.SetProgress("PK",0.8f).at(0).result.state==EffectControlState::NotLoaded && controlled.back()==&a);
    Complete(a);Complete(b);Complete(c);scene.Service();aura.Service();another_actor.Service();
    scene.Remove("PK");scene.Service();
    Check(a.removes==1 && b.removes==0 && c.removes==0 && !scene.Closing());
    Check(scene.SetProgress("PK",1,0.1f).at(0).result.state==EffectControlState::Applied && controlled.back()==&a);
    Check(aura.SetAnimatorState("PK",123).at(0).result.state==EffectControlState::Applied && controlled.back()==&b);
    Check(scene.SetProgress("missing",1).empty());
    scene.End();scene.Service();Check(!scene.Drained() && a.removes==1);
    Check(scene.SetProgress("PK",1).empty() && !aura.Status(aura_id)->cancelled);
    // End only this context. A late successful completion is removed using its
    // original receipt; the other context and actor retain their outputs.
    Complete(a);release_on_remove=true;scene.Service();
    Check(scene.Drained() && a.removes==2 && b.removes==0 && c.removes==0);
    bool rejected=false;try{scene.Start("late",&entity,&path);}catch(const std::logic_error&){rejected=true;}Check(rejected);
    aura.End();aura.Service();another_actor.End();another_actor.Service();Check(aura.Drained() && another_actor.Drained() && roots.empty());
    // Synchronous submission may trigger a context exit. Reentrant Service
    // must not destroy a request while its Start is still on the stack.
    a={};allocating=&a;VisualEffectContext reentrant(services);
    on_submit=[&]{reentrant.End();reentrant.Service();};
    const auto id=reentrant.Start("scene",&entity,&path);on_submit={};
    Check(reentrant.Status(id)->cancelled && !reentrant.Drained());
    reentrant.Service();Check(reentrant.Drained() && roots.empty());
    a={};b={};allocating=&a;VisualEffectContext repeated(services);
    repeated.Start("rabbits",&entity,&path);allocating=&b;repeated.Start("rabbits",&entity,&path);
    Complete(a);Complete(b);const auto both=repeated.SetAnimatorState("rabbits",123);
    Check(both.size()==2 && both[0].id!=both[1].id);
    Check(controlled[controlled.size()-2]==&a && controlled.back()==&b);
    repeated.End();repeated.Service();Check(repeated.Drained() && roots.empty());
    a={};b={};a.state=1;b.state=3;
    int official_callback=1; a.callback=&official_callback;b.callback=&official_callback;
    const auto previous_delegates=delegates.size();VisualEffectContext observed(services);
    const auto receipt=observed.Track("event scene",&manager,{a.id,0,&a});
    Check(observed.Track("event scene",&manager,{a.id,0,&a})==receipt && observed.PendingRequests()==1);
    bool conflict=false;try{observed.Track("unrelated",&manager,{a.id,0,&a});}catch(const std::logic_error&){conflict=true;}Check(conflict);
    conflict=false;try{observed.Track("event scene",&other_entity,{a.id,0,&a});}catch(const std::logic_error&){conflict=true;}Check(conflict);
    Check(delegates.size()==previous_delegates && a.callback==&official_callback);
    release_on_remove=false;observed.End(false);observed.Service();Check(a.removes==1);
    const auto late=observed.Track("late event",&manager,{b.id,0,&b});
    Check(observed.Status(late)->cancelled && b.removes==0 && observed.SetProgress("late event",1).empty());
    a.state=3;release_on_remove=true;observed.Service();
    Check(observed.Drained() && a.removes==2 && b.removes==1 && roots.empty());
    Check(a.callback==&official_callback && b.callback==&official_callback && delegates.size()==previous_delegates);
    // Actual execution receipts, not the selected body action, choose the
    // persistent visual context. Completion of one sequence keeps effects.
    int sequence=0,world=0;VisualEffectBindings bindings;
    auto program=std::make_shared<VisualEffectContext>(services);
    AnimationSequenceExecution execution;
    execution.construction=1;execution.definition=2;execution.serial=3;execution.sequence=&sequence;
    execution.origin={{reinterpret_cast<GameEntity*>(&entity),4},reinterpret_cast<GameWorld*>(&world)};
    bindings.Bind(execution,program);a={};a.state=1;a.callback=&official_callback;
    EffectSubmission submission;submission.sequence=execution;submission.origin=execution.origin;
    submission.trigger.serial=8;submission.manager=&manager;submission.request={a.id,0,&a};
    Check(bindings.Track("original-event",submission).has_value() && a.removes==0);
    AnimationSequenceObservation done;done.execution=execution;done.original_returned=true;done.state_read=true;
    bindings.Observe(done);
    Check(bindings.Size()==0 && !program->Closing() && program->PendingRequests()==1 && a.callback==&official_callback);
    // A later body action can share this program; an unrelated generation or
    // direct submission cannot acquire it merely by reusing its sequence ID.
    execution.serial=4;bindings.Bind(execution,program);submission.sequence=execution;
    ++submission.origin.token.generation;Check(!bindings.Track("wrong-owner",submission));--submission.origin.token.generation;
    submission.trigger.serial=0;Check(!bindings.Track("direct",submission));submission.trigger.serial=9;
    ++submission.sequence.definition;Check(!bindings.Track("wrong-definition",submission));--submission.sequence.definition;
    done.execution=execution;done.continues=true;bindings.Observe(done);Check(bindings.Size()==1);
    program->End(false);b={};b.state=3;submission.request={b.id,0,&b};
    Check(bindings.Track("late-event",submission).has_value() && b.removes==0);
    program->Service();Check(!program->Drained() && a.removes==1 && b.removes==1 && roots.empty());
    c={};c.state=3;submission.request={c.id,0,&c};Check(bindings.Track("after-empty",submission).has_value());
    program->Service();Check(c.removes==1 && !program->Drained() && roots.empty());
    // A new playback cannot take effects still attributed to the old one.
    auto replacement=std::make_shared<VisualEffectContext>(services);execution.serial=5;bindings.Bind(execution,replacement);
    Check(program->Drained());
    Check(!bindings.Track("old-playback",submission) && bindings.Size()==1);
    bool duplicate=false;try{bindings.Bind(execution,program);}catch(const std::logic_error&){duplicate=true;}Check(duplicate);
    done.execution=execution;done.continues=false;done.original_returned=false;bindings.Observe(done);Check(bindings.Size()==0);
    bindings.Bind(execution,replacement);replacement.reset();Check(bindings.Size()==1);
    bindings.Observe(done);Check(bindings.Size()==0);
    // The shipped Sparxie selectors join actual final task-list identities to
    // two distinct sequence executions sharing the same Gift-scene context.
    const auto profiles=ReadPresentationProfiles(HKSR_PROFILE_DATA_DIR);
    const auto& recipe=profiles.sources.at("Config/ConfigCharacter/Avatar/Avatar_Sparxie_00_Config.json")->native_events;
    Check(recipe.size()==3 && recipe[0].scope==recipe[1].scope && recipe[1].scope==recipe[2].scope);
    auto gift=std::make_shared<VisualEffectContext>(services);
    int receiver=0,collection1=0,collection2=0,sequence2=0,unrelated=0,reads=0,tracks=0;
    AnimationEventSource source;source.serial=100;source.origin=execution.origin;source.receiver=&receiver;
    AnimationEventDefinitions definitions;
    definitions.rows={{AnimationEventPart::TimePoint,38,{"Skill02_Gift"},{},&collection1},
        {AnimationEventPart::TimePoint,33,{"Skill02"},{},&collection2}};
    bool labels_readable=true;int label_errors=0,definition_traces=0,submission_traces=0;
    VisualEventRoutes routes([&](const auto&){++reads;return definitions;},
        [&](const auto&,const auto&){++tracks;return AnimationSequenceTracking::Tracked;},
        [&](const std::string& text){
            if(text.find("effect source label unresolved:")==0)++label_errors;
            if(text.find("visual event tracked ")==0)++definition_traces;
            if(text.find("visual event submission ")==0)++submission_traces;
        },
        [&](const auto&){return labels_readable?TextResult{{true,0},recipe[0].effect_keys.begin()->first,{}}:
            TextResult{{false,13},{},"path unavailable"};});
    std::vector<VisualEventRoutes::RuleId> rule_ids;
    for(const auto& rule:recipe)rule_ids.push_back(routes.Bind(source.origin,&receiver,rule,gift));
    auto other_source=source;++other_source.origin.token.generation;
    routes.Before(other_source,recipe[0].file);routes.Before(source,"unrelated/file");Check(reads==0);
    routes.Before(source,recipe[0].file);Check(reads==1);
    AnimationTaskConstruction built{10,nullptr,&collection1,&sequence,&unrelated,true};routes.Built(source,built);
    built={11,nullptr,&collection2,&sequence2,&unrelated,true};routes.Built(source,built);
    routes.After(source);Check(tracks==2);
    execution.construction=100;execution.definition=10;execution.serial=101;execution.sequence=&sequence;
    AnimationSequenceObservation begin;begin.execution=execution;begin.operation=AnimationSequenceOperation::Begin;begin.entering=true;
    routes.Observe(begin);a={};a.state=3;a.callback=&official_callback;
    submission.sequence=execution;submission.origin=execution.origin;submission.request={a.id,0,&a};
    Check(routes.Submitted(submission).has_value());routes.SubmissionReturned(submission,true);
    Check(gift->Requests("native.UI_01").size()==1);
    done.execution=execution;routes.Observe(done);Check(!gift->Closing() && gift->PendingRequests()==1);
    begin.execution.definition=11;begin.execution.serial=102;begin.execution.sequence=&sequence2;routes.Observe(begin);
    submission.sequence=begin.execution;b={};b.state=3;b.callback=&official_callback;submission.request={b.id,0,&b};
    labels_readable=false;
    Check(routes.Submitted(submission).has_value() && gift->PendingRequests()==2 && a.removes==0);
    Check(label_errors==1 && definition_traces==2 && submission_traces==2 && gift->Requests("native.resolution").size()==1);labels_readable=true;
    routes.SubmissionReturned(submission,false);
    Check(a.removes==0 && b.removes==0); // the callback records intent only
    gift->Service();Check(a.removes==0 && b.removes==1);
    // An unrelated child collection cannot be mapped by config/output index.
    source.serial=200;routes.Before(source,recipe[0].file);built.serial=12;built.configurations=&unrelated;
    routes.Built(source,built);routes.After(source);Check(tracks==2);
    // A shared collection with an unselected event is genuinely ambiguous.
    definitions.rows.push_back({AnimationEventPart::TimePoint,99,{"Other"},{},&collection1});
    source.serial=201;routes.Before(source,recipe[0].file);built.configurations=&collection1;
    routes.Built(source,built);routes.After(source);Check(tracks==2);
    // Retire only the program. In-flight submissions retain their old scope;
    // a new Begin after it ended cannot prolong or reassign that scope.
    gift->End(false);c={};c.state=3;submission.request={c.id,0,&c};Check(routes.Submitted(submission).has_value());
    for(auto rule_id:rule_ids)routes.Unbind(rule_id);
    begin.execution.serial=103;routes.Observe(begin);submission.sequence=begin.execution;Check(!routes.Submitted(submission));
    gift->Service();Check(gift->Drained() && a.removes==1 && b.removes==1 && c.removes==1 && roots.empty());
    routes.Clear();
    Check(gift->Drained());
    // Model construction precedes program entry. Keep definitions while no
    // context is attached, and select the context at each actual Begin.
    definitions.rows.resize(1);source.serial=300;built.serial=30;built.parent=&sequence;built.configurations=&collection1;
    const auto declared=routes.Declare(source.origin,recipe[0]);
    routes.Before(source,recipe[0].file);routes.Built(source,built);routes.After(source);routes.Collect();
    auto later=std::make_shared<VisualEffectContext>(services);
    routes.Attach(declared,&receiver,later);
    begin.execution.construction=300;begin.execution.definition=30;begin.execution.sequence=&sequence;begin.execution.serial=301;
    routes.Observe(begin);a={};a.state=3;submission.sequence=begin.execution;submission.request={a.id,0,&a};
    // Removal before this execution submits the named effect is retained for
    // that execution only. No native mutation occurs in the observer callback.
    Check(routes.RemoveInFlight(later,"native.UI_01",{false,true,false}));
    Check(routes.Submitted(submission).has_value());
    Check(later->Status(later->Requests("native.UI_01").front())->cancelled && a.removes==0);
    routes.Detach(declared);b={};b.state=3;submission.request={b.id,0,&b};
    Check(routes.Submitted(submission).has_value()); // already in-flight
    done.execution=begin.execution;routes.Observe(done);
    begin.execution.serial=302;routes.Observe(begin);submission.sequence=begin.execution;Check(!routes.Submitted(submission));
    auto next_context=std::make_shared<VisualEffectContext>(services);
    routes.Attach(declared,&unrelated,next_context);begin.execution.serial=303;routes.Observe(begin);submission.sequence=begin.execution;
    Check(!routes.Submitted(submission)); // other model receiver, same entity
    routes.Attach(declared,&receiver,next_context);begin.execution.serial=304;routes.Observe(begin);submission.sequence=begin.execution;
    c={};c.state=3;submission.request={c.id,0,&c};Check(routes.Submitted(submission).has_value());
    Check(!next_context->Status(next_context->Requests("native.UI_01").front())->cancelled);
    Check(later->PendingRequests()==2 && next_context->PendingRequests()==1);
    later->End(false);next_context->End(false);later->Service();next_context->Service();
    Check(!next_context->Drained());
    done.execution=begin.execution;done.operation=AnimationSequenceOperation::DefinitionRetired;
    routes.Observe(done);Check(next_context->Drained());
    routes.Unbind(declared);routes.Clear();
    Check(roots.empty());
    a={};allocating=&a;VisualEffectContext targeted(services);
    on_submit=[&]{targeted.End(false);targeted.Service();};
    EntityEffectOptions options;options.unique_name="explicit-target";
    const auto target_id=targeted.StartEntity("target-scene",&entity,&other_entity,&path,options);on_submit={};
    Check(a.target==&other_entity && targeted.Status(target_id)->cancelled && a.removes==0);
    targeted.Service();Check(targeted.Drained() && a.removes==1 && roots.empty());
    delegates.clear();
}

namespace {
void TestVisualSnapshots() {
    auto program=std::make_shared<VisualProgram>(ParseVisualProgram(R"({
      "format":"hksr.visual-program","id":"restore","source":"config","scope":"scene","effects":{},
      "restore":[{"op":"timeline_play","source":"restore","key":"root","path":"persistent.playable"}],
      "phases":{
        "first":{"root":{"kind":"ability","name":"One"},"path":[{"owner":"Phase","field":"Value"}]},
        "second":{"root":{"kind":"ability","name":"Two"},"path":[{"owner":"Phase","field":"Value"}]}},
      "contexts":{
        "z_first":{"phase":"first","restore":[],"enter":[{"op":"assign","variable":"amount","value":1,"source":"initial"}]},
        "a_second":{"phase":"second","restore":[]}},
      "actions":{
        "enter":{"state":"Start","restart":true,"enter":[
          {"op":"assign","variable":"root_value","value":3,"source":"initial"},
          {"op":"context_enter","name":"z_first","source":"first"},
          {"op":"context_enter","name":"a_second","source":"second"},
          {"op":"timeline_play","source":"initial","key":"intro","path":"intro.playable"}]},
        "continue":{"state":"Resolve"}},
      "signals":{"change":{"context":"z_first","instructions":[{"op":"assign","variable":"amount","value":7,"source":"change"}]}}
    })"));
    VisualProgramServices callbacks;callbacks.effects=Services();
    callbacks.create=[](auto&,const auto&,const auto&)->VisualEffectId{throw std::logic_error("unexpected effect");};
    auto first=std::make_shared<PhaseVisualConfigSources>(),second=std::make_shared<PhaseVisualConfigSources>();
    first->visuals.context_available=second->visuals.context_available=true;
    first->standby={true,"First",{}};second->standby={true,"Second",{}};
    callbacks.read_phase=[&](const auto& key){return key=="first"?first:second;};
    VisualProgramParameters defaults;defaults.targets["Target"]=42;defaults.anchors["center"]={{1,2,3},{0,0,0,1}};
    std::map<const VisualEffectContext*,VisualProgramParameters> captured;
    callbacks.select_inputs=[&]{return defaults;};
    callbacks.prepare_inputs=[&](auto context,const auto& inputs,bool){captured[context.get()]=inputs;};
    callbacks.capture_inputs=[&](const auto& context){return captured.at(&context);};
    bool loaded=true;std::vector<std::string> submissions;
    callbacks.timeline=[&](auto,const auto& op){submissions.push_back(op.path);return VisualStepResult{
        loaded?VisualStepState::Done:VisualStepState::Pending,{}};};
    callbacks.cancel_timelines=[](auto){};callbacks.timelines_drained=[]{return true;};
    VisualProgramPlayer original(program,callbacks);original.Select("enter",{{"variant","left"}});original.Service();
    Check(original.Status().prepared && original.Signal(original.ContextToken("z_first"),"change"));original.Service();
    const auto saved=original.Capture();Check(saved.scene && saved.scene->variables.at("root_value")==3);
    Check(saved.contexts.size()==2 && saved.contexts[0].name=="z_first" && saved.contexts[1].name=="a_second");
    Check(saved.contexts[0].variables.at("amount")==7 && saved.contexts[0].inputs.choices.at("variant")=="left");
    Check(saved.contexts[0].inputs.targets.at("Target")==42 && saved.scene->inputs.anchors.at("center").position.y==2);
    original.End();original.Service();Check(original.Drained());
    defaults.targets["Target"]=999;defaults.choices["variant"]="right";loaded=false;submissions.clear();
    VisualProgramPlayer restored(program,callbacks);restored.Restore(saved);restored.Select("continue");restored.Service();
    Check(restored.Restoring() && !restored.Status().prepared && restored.CurrentPhase()==second);
    Check(restored.Capture().contexts[0].inputs.targets.at("Target")==42);
    Check(submissions==std::vector<std::string>{"persistent.playable"}); // no replay of intro/body history
    loaded=true;restored.Service();Check(!restored.Restoring() && restored.Status().prepared && restored.Status().action=="continue");
    const auto after=restored.Capture();Check(after.contexts[0].variables.at("amount")==7 && after.scene->variables.at("root_value")==3);
    Check(after.contexts[0].inputs.targets.at("Target")==42 && after.contexts[0].inputs.choices.at("variant")=="left");
    Check(after.scene->inputs.choices.at("variant")=="left" && after.scene->inputs.targets.at("Target")==42);
    Check(restored.Played(restored.Status().intent,1,1));restored.End();restored.Service();Check(restored.Drained());
    bool rejected=false;auto invalid=saved;invalid.contexts.push_back(invalid.contexts.front());
    VisualProgramPlayer validation(program,callbacks);try{validation.Restore(invalid);}catch(const std::invalid_argument&){rejected=true;}
    Check(rejected && !validation.Restoring());validation.Restore(saved);validation.End();validation.Service();Check(validation.Drained());
    auto missing=std::make_shared<VisualProgram>(*program);missing->contexts.at("a_second").restore.reset();
    VisualProgramPlayer unsupported(missing,callbacks);unsupported.Select("enter");unsupported.Service();rejected=false;
    try{unsupported.Capture();}catch(const std::runtime_error&){rejected=true;}
    Check(rejected);unsupported.End();unsupported.Service();Check(unsupported.Drained());
    // Cancellation during restoration must retain then retire every partial owner.
    auto cancel_callbacks=callbacks;
    VisualProgramPlayer* active=nullptr;
    cancel_callbacks.prepare_inputs=[&](auto,const auto&,bool){active->End();};
    VisualProgramPlayer reentrant(program,cancel_callbacks);active=&reentrant;reentrant.Restore(saved);reentrant.Service();reentrant.Service();
    Check(reentrant.Drained() && reentrant.ActiveContexts().empty());
    // Target readiness is independent of lifetime entry order. A late target
    // in the first context must not make its phase the newest override.
    auto targets=saved;targets.contexts[1].inputs.targets.clear();targets.scene.reset();
    bool target_ready=false;
    auto deferred_callbacks=callbacks;
    deferred_callbacks.restore_inputs_ready=[&](const auto& parameters){return VisualStepResult{
        parameters.targets.empty() || target_ready?VisualStepState::Done:VisualStepState::Pending,"waiting for target"};};
    VisualProgramPlayer deferred(program,deferred_callbacks);deferred.Restore(targets);deferred.Service();
    Check(deferred.Restoring() && deferred.CurrentPhase()==second && !deferred.Status().waiting.empty());
    target_ready=true;deferred.Service();Check(!deferred.Restoring() && deferred.CurrentPhase()==second);
    deferred.End();deferred.Service();Check(deferred.Drained());
}
void TestVisualLifetimes() {
    auto program=std::make_shared<VisualProgram>(ParseVisualProgram(R"({
      "format":"hksr.visual-program","id":"lifetimes","source":"config","scope":"scene",
      "effects":{"ground":{"path":"ground.prefab","target":"Caster"}},
      "phases":{"enhanced":{"root":{"kind":"global_modifier","name":"Modifier"},"path":[{"owner":"RPG.GameCore.Change","field":"PhaseAnimConfig"}]}},
      "contexts":{
        "aura":{"enter":[{"op":"create","effects":["ground"],"source":"aura.create"}]},
        "visual_manager":{"phase":"enhanced"},
        "skill":{"event_scope":"skill-events","enter":[{"op":"create","effects":["ground"],"source":"skill.create"}],
          "exit":[{"op":"effect_state","effect":"ground","state":"Idle","source":"skill.exit"}]}
      },
      "actions":{
        "ultra":{"state":"Skill03","restart":true,"enter":[{"op":"context_enter","name":"aura","source":"ultra"}],
          "lanes":[[{"at":0.806,"instructions":[{"op":"context_enter","name":"visual_manager","source":"visual"}]}]],
          "completion_state":"StandBy_Transfer"},
        "skill":{"state":"Skill11_Start","restart":true,"enter":[{"op":"context_enter","name":"skill","source":"skill"}],
          "completion_state":"StandBy_Transfer"},
        "skill_end":{"state":"Skill11_End","restart":true,"enter":[{"op":"context_exit","name":"skill","source":"release"}]},
        "ultra_end":{"state":"StandBy01toStandBy","restart":true,
          "enter":[{"op":"context_exit","name":"aura","source":"ultra.exit"}],
          "lanes":[[{"at":0.25,"instructions":[{"op":"context_exit","name":"visual_manager","source":"visual.exit"}]}]],
          "completion_state":"StandBy"}
      },
      "signals":{
        "enter_view":{"context":"skill","instructions":[
          {"op":"effect_state","effect":"ground","state":"Idle","source":"view.idle"},
          {"op":"follow","effect":"ground","targets":[],"source":"view.clear"}]},
        "exit_view":{"context":"skill","instructions":[
          {"op":"effect_state","effect":"ground","state":"Move","source":"view.move"},
          {"op":"follow","effect":"ground","targets":["Target"],"source":"view.bind"}]},
        "pulse":{"context":"aura","instructions":[{"op":"effect_state","effect":"ground","state":"Move","source":"aura.pulse"}]}
      }
    })"));
    auto services=Services();std::vector<std::unique_ptr<Params>> requests;
    std::vector<std::string> order;
    std::map<std::string,std::weak_ptr<VisualEffectContext>> contexts;
    std::vector<std::string> detached;
    VisualProgramServices callbacks;callbacks.effects=services;
    unsigned phase_reads=0;
    auto phase_data=std::make_shared<PhaseVisualConfigSources>();phase_data->visuals.context_available=true;
    phase_data->standby={true,"PhaseIdle",{}};
    callbacks.read_phase=[&](const std::string& name){Check(name=="enhanced");++phase_reads;return phase_data;};
    callbacks.context_started=[&](const std::string& name,auto context){contexts[name]=context;};
    callbacks.context_stopped=[&](const std::string& name){detached.push_back(name);};
    callbacks.create=[&](auto& context,const auto& key,const auto&) {
        auto p=std::make_unique<Params>();p->id=unsigned(100+requests.size());allocating=p.get();requests.push_back(std::move(p));
        return context.StartEntity(key,&entity,&other_entity,&path,{});
    };
    callbacks.effect_state=[&](auto& context,auto id,const VisualEffectState& op) {
        const auto result=context.SetAnimatorState(id,op.state,op.transition,op.restart);
        if(result.state==EffectControlState::Applied)order.push_back(op.state);
        return VisualStepResult{result.state==EffectControlState::Applied?VisualStepState::Done:
            result.state==EffectControlState::NotLoaded?VisualStepState::Pending:VisualStepState::Failed,result.detail};
    };
    callbacks.follow=[&](auto&,auto,const auto& op) {order.push_back(op.targets.empty()?"clear":"bind");return VisualStepResult{VisualStepState::Done,{}};};
    release_on_remove=true;VisualProgramPlayer player(program,callbacks);
    auto intent=player.Select("ultra");player.Service();Check(player.Status().prepared && requests.size()==1);
    const auto aura=player.ContextToken("aura");Check(aura && !player.ContextToken("visual_manager"));
    Check(!player.CurrentPhase() && !phase_reads && program->phases.at("enhanced").path[0].field=="PhaseAnimConfig");
    Check(player.Played(intent,800,1));player.Observe(800,1,"Skill03",0.8f);player.Service();Check(!player.ContextToken("visual_manager"));
    player.Observe(800,1,"Skill03",0.81f);player.Service();const auto visual=player.ContextToken("visual_manager");Check(visual);
    Check(player.CurrentPhase()==phase_data && phase_reads==1);
    Check(player.SuccessorPlayed(intent,801,1));player.Observe(801,1,"StandBy_Transfer",0);player.Service();
    Check(player.Status().completed && player.ContextToken("aura")==aura && player.ContextToken("visual_manager")==visual);
    Check(player.CurrentPhase()==phase_data);
    Check(player.Signal(aura,"pulse")); // no active body cursor is required
    // A new body scene neither recreates nor cancels independently owned effects.
    intent=player.Select("skill");player.Service();const auto skill=player.ContextToken("skill");
    Check(player.Status().prepared && requests.size()==2 && skill && requests[0]->removes==0);
    Check(program->contexts.at("skill").event_scope=="skill-events");
    int sequence=0,world=0;VisualEffectBindings native;
    AnimationSequenceExecution execution;
    execution.construction=1;execution.definition=2;execution.serial=3;execution.sequence=&sequence;
    execution.origin={{reinterpret_cast<GameEntity*>(&entity),4},reinterpret_cast<GameWorld*>(&world)};
    native.Bind(execution,contexts.at("skill").lock());
    Complete(*requests[1]);player.Service();
    Check(player.Signal(aura,"pulse") && player.Signal(skill,"enter_view") && player.Signal(skill,"exit_view"));
    player.Service();Check(order==std::vector<std::string>({"Idle","clear","Move","bind"}));
    // Aura is still loading; it does not block signals in the other lifetime.
    Complete(*requests[0]);player.Service();Check(order.back()=="Move");
    player.InterruptAction();player.Service();Check(player.ContextToken("skill")==skill && requests[0]->removes==0 && requests[1]->removes==0);
    Check(player.Signal(skill,"enter_view"));player.Service();Check(order.back()=="clear");
    player.Select("skill");player.Service();Check(requests.size()==2 && player.ContextToken("skill")==skill); // idempotent entry
    player.Select("skill_end");player.Service();player.Service();
    Check(!player.ContextToken("skill") && requests[1]->removes==1 && requests[0]->removes==0);
    Check(detached==std::vector<std::string>{"skill"});
    Params native_late;native_late.id=500;native_late.state=3;
    EffectSubmission late;late.sequence=execution;late.origin=execution.origin;late.trigger.serial=8;
    late.manager=&manager;late.request={native_late.id,0,&native_late};
    Check(native.Track("late-native",late).has_value());player.Service();Check(native_late.removes==1);
    Check(!player.Signal(skill,"exit_view"));
    player.Select("skill");player.Service();const auto next_skill=player.ContextToken("skill");Check(next_skill!=skill && requests.size()==3);
    Check(!player.Signal(skill,"enter_view"));
    // A pending scoped signal survives an unrelated body selection, but not
    // context exit/re-entry; its receipt must never bind the new request.
    Check(player.Signal(next_skill,"enter_view"));player.Service();
    intent=player.Select("ultra_end");player.Service();Check(player.Status().prepared && !player.ContextToken("aura"));
    Check(player.ContextToken("visual_manager")==visual && player.ContextToken("skill")==next_skill);
    Check(player.Played(intent,802,1));player.Observe(802,1,"StandBy01toStandBy",0.2f);player.Service();Check(player.ContextToken("visual_manager"));
    player.Observe(802,1,"StandBy01toStandBy",0.26f);player.Service();Check(!player.ContextToken("visual_manager"));
    Check(!player.CurrentPhase() && phase_reads==1);
    // Whole-owner exit does not wait for an unloaded effect's authored exit.
    player.Select("skill_end");player.Service();Check(!player.Status().prepared);
    const auto controls=order.size();player.End();player.Service();Check(player.ActiveContexts().empty() && requests[2]->removes==1);
    Complete(*requests[2]);player.Service();Check(!player.Drained() && roots.empty() && order.size()==controls);
    native.Clear();player.Service();Check(player.Drained());
    // Lifetime entry is retained before synchronous callbacks. Reentrant End
    // cancels it even though neither the body nor the entry cursor completed.
    requests.clear();VisualProgramPlayer reentrant(program,callbacks);
    on_submit=[&]{reentrant.End();reentrant.Service();};reentrant.Select("ultra");reentrant.Service();on_submit={};
    Check(requests.size()==1);reentrant.Service();Check(reentrant.Drained() && roots.empty());
    auto phase_program=std::make_shared<VisualProgram>(ParseVisualProgram(R"({
      "format":"hksr.visual-program","id":"phase-owners","source":"config","scope":"scene","effects":{},
      "phases":{
        "first":{"root":{"kind":"ability","name":"One"},"path":[{"owner":"RPG.GameCore.Change","field":"PhaseAnimConfig"}]},
        "second":{"root":{"kind":"global_template","name":"Two"},"path":[{"owner":"RPG.GameCore.Change","field":"PhaseAnimConfig"}]}},
      "contexts":{"one":{"phase":"first"},"two":{"phase":"second"}},
      "actions":{
        "one":{"state":"One","restart":true,"enter":[{"op":"context_enter","name":"one","source":"one"}]},
        "two":{"state":"Two","restart":true,"enter":[{"op":"context_enter","name":"two","source":"two"}]},
        "exit":{"state":"Exit","restart":true,"enter":[{"op":"context_exit","name":"two","source":"exit"}]}}
    })"));
    auto first_phase=std::make_shared<PhaseVisualConfigSources>(*phase_data),second_phase=std::make_shared<PhaseVisualConfigSources>(*phase_data);
    Check(program->phases.at("enhanced").root==PhaseConfigRoot::GlobalModifier);
    Check(phase_program->phases.at("first").root==PhaseConfigRoot::Ability && phase_program->phases.at("first").name=="One");
    Check(phase_program->phases.at("second").root==PhaseConfigRoot::GlobalTemplate && phase_program->phases.at("second").name=="Two");
    first_phase->standby.value="First";second_phase->standby.value="Second";
    auto phase_callbacks=callbacks;unsigned distinct_reads=0;
    phase_callbacks.read_phase=[&](const auto& name){++distinct_reads;return name=="first"?first_phase:second_phase;};
    VisualProgramPlayer layered(phase_program,phase_callbacks),independent(phase_program,phase_callbacks);
    layered.Select("one");layered.Service();Check(layered.CurrentPhase()==first_phase);
    Check(layered.BranchState().phase=="first");
    layered.Select("two");layered.Service();Check(layered.CurrentPhase()==second_phase && distinct_reads==2);
    Check(layered.BranchState().phase=="second");
    layered.Select("two");layered.Service();Check(distinct_reads==2);
    independent.Select("two");independent.Service();layered.Select("exit");layered.Service();
    Check(layered.CurrentPhase()==first_phase && independent.CurrentPhase()==second_phase);
    Check(layered.BranchState().phase=="first" && independent.BranchState().phase=="second");
    layered.InterruptAction();layered.Service();Check(layered.CurrentPhase()==first_phase);
    layered.Leave();Check(!layered.CurrentPhase() && layered.BranchState().phase.empty());layered.Service();layered.End();layered.Service();
    independent.End();Check(!independent.CurrentPhase());independent.Service();Check(layered.Drained() && independent.Drained());
    phase_callbacks.read_phase=[](const auto&)->std::shared_ptr<const PhaseVisualConfigSources>{throw std::runtime_error("source unavailable");};
    VisualProgramPlayer missing_phase(phase_program,phase_callbacks);missing_phase.Select("one");missing_phase.Service();
    Check(!missing_phase.Status().prepared && !missing_phase.CurrentPhase() && missing_phase.ActiveContexts().empty());
    Check(missing_phase.Status().issues.size()==1);missing_phase.End();missing_phase.Service();Check(missing_phase.Drained());
    requests.clear();allocating=&a;release_on_remove=false;delegates.clear();
}
}
void TestVisualProgram(const char* file) {
    std::shared_ptr<const VisualProgram> program;
    if(file) {
        std::ifstream input(file,std::ios::binary);Check(static_cast<bool>(input));
        program=std::make_shared<VisualProgram>(ParseVisualProgram({std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()}));
        Check(program->effects.size()==19 && program->actions.size()==3);
        Check(program->effects.at("Monster").target=="AbilityTargetAndAdjoinEntity");
        Check(program->effects.at("Common_Hit01").target=="AbilityTargetEntity");
        Check(program->effects.at("Rabbit").options.unique_name.empty());
    } else program=std::make_shared<VisualProgram>(ParseVisualProgram(R"({
      "format":"hksr.visual-program","id":"test","source":"config","scope":"scene",
      "effects":{"PK":{"path":"pk","target":"Caster"},"recipient":{"path":"other","target":"Target"}},
      "actions":{
        "gift":{"state":"Gift","restart":true,"enter":[
          {"op":"create","source":"template","effects":["PK","recipient"]},
          {"op":"assign","source":"assign","variable":"progress","value":0},
          {"op":"progress","source":"initial","effect":"PK","value":{"variable":"progress"}}
        ],"lanes":[
          [{"at":0.2,"instructions":[{"op":"assign","source":"assign","variable":"progress","value":0.3},
             {"op":"progress","source":"progress","effect":"PK","value":{"variable":"progress"},"seconds":1}]}],
          [{"at":0.6,"instructions":[{"op":"trigger","source":"prize","effects":["recipient"],"trigger":"Fire"}]}]
        ]},
        "resolve":{"state":"Resolve","enter":[{"op":"progress","source":"resolution","effect":"PK","value":0.8,"seconds":0.183}],
          "lanes":[[{"at":0.24,"instructions":[{"op":"progress","source":"end","effect":"PK","value":1,"seconds":0.1}]}]]}
      }} )"));
    auto services=Services();std::vector<std::unique_ptr<Params>> requests;
    std::vector<std::string> created,timeline,triggered;int attaches=0,detaches=0,cancellations=0;
    bool missing_target=false,failed_submit=false;std::uint64_t id=50;
    VisualProgramServices callbacks;callbacks.effects=services;
    callbacks.create=[&](VisualEffectContext& context,const std::string& key,const VisualEffectInput& input) {
        created.push_back(key);if(missing_target && input.target!="Caster")throw std::runtime_error("explicit target unavailable");
        auto p=std::make_unique<Params>();p->id=static_cast<unsigned>(++id);allocating=p.get();requests.push_back(std::move(p));
        if(failed_submit && input.target!="Caster")allocating->released=true;
        return context.StartEntity(key,&entity,&other_entity,&path,input.options);
    };
    callbacks.attach=[&](auto){++attaches;};callbacks.detach=[&]{++detaches;};callbacks.cancel_timelines=[&](auto){++cancellations;};
    callbacks.timeline=[&](auto,const VisualTimeline& input){timeline.push_back(input.key);return VisualStepResult{VisualStepState::Done,{}};};
    callbacks.timelines_drained=[]{return true;};
    callbacks.remove=[&](VisualEffectContext& context,const VisualRemove& input){for(const auto& key:input.effects)context.Remove(key,input.fade);return VisualStepResult{VisualStepState::Done,{}};};
    callbacks.trigger=[&](VisualEffectContext&,VisualEffectId,const VisualTrigger& input){triggered.push_back(input.trigger);return VisualStepResult{VisualStepState::Done,{}};};
    release_on_remove=false;
    VisualProgramPlayer player(program,callbacks);
    auto intent=player.Select("gift",{{"fire","yes"},{"prize","grand"}});player.Service();
    Check(!player.Status().prepared && player.Status().issues.empty() && attaches==1);
    Check(created.size()==(file?16:2));
    const auto context=player.Context();
    for(auto& p:requests)Complete(*p);
    player.Service();Check(player.Status().prepared && player.Played(intent,100,3));
    const auto old_controls=controlled.size();
    player.Observe(99,3,program->actions.at("gift").state,0.9f);player.Service();Check(controlled.size()==old_controls);
    player.Observe(100,4,program->actions.at("gift").state,0.9f);player.Service();Check(controlled.size()==old_controls);
    player.Observe(100,3,program->actions.at("gift").state,0.7f);player.Service();
    Check(controlled.size()>old_controls);
    const auto once=controlled.size();player.Service();Check(controlled.size()==once);
    Check(file?created.size()==17:triggered.size()==1);
    intent=player.Select("resolve");player.Service();
    Check(player.Context()==context && attaches==1 && player.Status().prepared);
    Check(player.Played(intent,101,3));player.Observe(100,3,program->actions.at("gift").state,1);player.Service();
    const auto before_end=controlled.size();
    player.Observe(101,3,program->actions.at("resolve").state,0.25f);player.Service();Check(controlled.size()>before_end);
    if(file) {
        player.Observe(101,3,program->actions.at("resolve").state,1);player.Service();Check(player.Status().finished);
        Check(player.SuccessorPlayed(intent,102,4));player.Observe(102,4,"StandBy",0);player.Service();
        Check(player.Status().completed && created.back()=="Common_Hit01" && timeline.back()=="timeline2");
        Check(requests.back()->removes==0); // actual recipe keeps the tail alive
    }
    for(auto& p:requests)if(p->removes)p->released=true; // engine finishes prior fades
    player.End(false);release_on_remove=true;player.Service();Check(player.Drained() && roots.empty());
    const auto detached_after_close=detaches,cancelled_after_close=cancellations;
    player.End();player.Leave();player.Service();
    Check(player.Drained() && detaches==detached_after_close && cancellations==cancelled_after_close);
    if(file){
        requests.clear();created.clear();triggered.clear();release_on_remove=false;
        VisualProgramPlayer alternative(program,callbacks);
        intent=alternative.Select("gift",{{"fire","no"},{"prize","minor"}});alternative.Service();
        for(auto& p:requests)Complete(*p);alternative.Service();Check(alternative.Played(intent,110,5));
        alternative.Observe(110,5,program->actions.at("gift").state,0.7f);alternative.Service();
        Check(created.size()==17);
        intent=alternative.Select("resolve_v1");alternative.Service();Check(alternative.Status().prepared);
        Check(alternative.Played(intent,111,5));alternative.Observe(111,5,program->actions.at("resolve_v1").state,1);alternative.Service();
        Check(alternative.Status().finished && triggered.size()==5);
        Check(alternative.SuccessorPlayed(intent,112,6));alternative.Observe(112,6,"StandBy",0);alternative.Service();
        Check(alternative.Status().completed && created.size()==17 && timeline.back()=="timeline3");
        for(auto& p:requests)if(p->removes)p->released=true;
        alternative.End();release_on_remove=true;alternative.Service();Check(alternative.Drained() && roots.empty());
        delegates.clear();return;
    }
    // Value inputs are snapshotted once per consumed action, not per cue or
    // wall frame. A newer selection supersedes old input reads; explicit
    // choices override the live defaults without altering another player.
    requests.clear();created.clear();
    auto hot_program=std::make_shared<VisualProgram>(ParseVisualProgram(R"({
      "format":"hksr.visual-program","id":"hot","source":"config","scope":"scene",
      "effects":{"PK":{"path":"pk","target":"Caster"},"recipient":{"path":"other","target":"Target"}},
      "actions":{"hot":{"state":"Hot","restart":true,
        "enter":[{"op":"create","source":"enter","effects":["PK"],"when":{"choice":"variant","is":"hot"}}],
        "lanes":[[{"at":0.5,"instructions":[{"op":"create","source":"later","effects":["recipient"],"when":{"choice":"variant","is":"hot"}}]}]]}}})"));
    auto hot_callbacks=callbacks;std::string variant="hot";int input_reads=0;
    hot_callbacks.select_inputs=[&]{++input_reads;return VisualProgramParameters{{},{{"variant",variant}},{}};};
    VisualProgramPlayer hot(hot_program,hot_callbacks);
    hot.Select("hot",{{"variant","cold"}});intent=hot.Select("hot");hot.Service();
    Check(input_reads==1 && created==std::vector<std::string>{"PK"} && hot.Status().prepared);
    Check(hot.Played(intent,120,7));variant="cold";
    hot.Observe(120,7,"Hot",0.6f);hot.Service();
    Check(input_reads==1 && created==std::vector<std::string>({"PK","recipient"}));
    for(auto& p:requests)Complete(*p);
    variant="hot";hot.Select("hot",{{"variant","cold"}});hot.Service();
    Check(input_reads==2 && created.size()==2 && hot.Status().prepared);
    hot.End();hot.Service();Check(hot.Drained() && roots.empty());
    auto invalid_inputs=callbacks;invalid_inputs.select_inputs=[]()->VisualProgramParameters{throw std::runtime_error("input read failed");};
    VisualProgramPlayer failed_inputs(hot_program,invalid_inputs);failed_inputs.Select("hot");failed_inputs.Service();failed_inputs.Service();
    Check(!failed_inputs.Status().prepared && failed_inputs.Status().issues.size()==1 && created.size()==2);
    failed_inputs.End();failed_inputs.Service();Check(failed_inputs.Drained());
    auto optional_program=std::make_shared<VisualProgram>(ParseVisualProgram(R"({
      "format":"hksr.visual-program","id":"optional","source":"config","scope":"scene","effects":{},
      "actions":{"clear":{"state":"Clear","restart":true,"enter":[
        {"op":"remove","source":"shared cleanup","effects":["branch-effect"],"optional":["branch-effect"]}]}}})"));
    auto removal_callbacks=callbacks;removal_callbacks.remove={};
    VisualProgramPlayer optional_cleanup(optional_program,removal_callbacks);
    optional_cleanup.Select("clear");optional_cleanup.Service();optional_cleanup.Service();
    Check(optional_cleanup.Status().prepared && optional_cleanup.Status().issues.empty() && optional_cleanup.Status().notes.size()==1);
    optional_cleanup.End();optional_cleanup.Service();Check(optional_cleanup.Drained());
    auto required_program=std::make_shared<VisualProgram>(*optional_program);
    std::get<VisualRemove>(required_program->actions.at("clear").enter[0].operation).optional.clear();
    VisualProgramPlayer required_cleanup(required_program,removal_callbacks);
    required_cleanup.Select("clear");required_cleanup.Service();required_cleanup.Service();
    Check(!required_cleanup.Status().prepared && required_cleanup.Status().issues.size()==1 && required_cleanup.Status().notes.empty());
    required_cleanup.End();required_cleanup.Service();Check(required_cleanup.Drained());
    // Attachment failure must remain visible, and must not run half a scene
    // or silently succeed on a subsequent Service call.
    auto failed_attachment=callbacks;
    failed_attachment.attach=[](auto){throw std::runtime_error("current receiver missing");};
    const auto before_attachment=created.size();VisualProgramPlayer unattached(program,failed_attachment);
    unattached.Select("gift");unattached.Service();unattached.Service();
    Check(!unattached.Status().prepared && unattached.Status().issues.size()==1 && created.size()==before_attachment);
    unattached.End();unattached.Service();Check(unattached.Drained() && roots.empty());
    // A pending control in one lane must not block another lane, and it must
    // not repeat previously accepted transitions on each frame.
    requests.clear();created.clear();controlled.clear();triggered.clear();
    VisualProgramPlayer concurrent(program,callbacks);intent=concurrent.Select("gift");concurrent.Service();
    for(auto& p:requests)Complete(*p);concurrent.Service();Check(concurrent.Played(intent,200,4));
    requests[0]->state=1;concurrent.Observe(200,4,"Gift",0.7f);concurrent.Service();
    Check(triggered.size()==1 && !concurrent.Status().finished);
    Complete(*requests[0]);concurrent.Service();Check(concurrent.Status().finished && triggered.size()==1);
    concurrent.End();concurrent.Service();Check(concurrent.Drained() && roots.empty());
    // Collect all independent input failures in one preparation. No caster
    // fallback, no body acceptance with an incomplete preparation.
    requests.clear();created.clear();missing_target=true;
    VisualProgramPlayer incomplete(program,callbacks);incomplete.Select("gift");incomplete.Service();
    Check(created.size()==2 && incomplete.Status().issues.size()==1);
    Complete(*requests[0]);incomplete.Service();Check(!incomplete.Status().prepared);
    incomplete.End();incomplete.Service();Check(incomplete.Drained() && roots.empty());missing_target=false;
    requests.clear();created.clear();failed_submit=true;
    VisualProgramPlayer rejected(program,callbacks);rejected.Select("gift");rejected.Service();
    Check(created.size()==2 && rejected.Status().issues.size()==1);
    Complete(*requests[0]);rejected.Service();Check(!rejected.Status().prepared);
    rejected.End();rejected.Service();Check(rejected.Drained() && roots.empty());failed_submit=false;
    // Action replacement during synchronous effect submission stops the old
    // plan at that boundary; reentrant Service cannot free it on its own stack.
    requests.clear();created.clear();VisualProgramPlayer reentrant(program,callbacks);
    reentrant.Select("gift");on_submit=[&]{on_submit={};reentrant.Select("gift");reentrant.Service();};
    reentrant.Service();Check(created.size()==1);
    reentrant.Service();Check(created.size()==3);
    reentrant.Service();Check(requests[0]->released);
    for(std::size_t i=1;i<requests.size();++i)Complete(*requests[i]);reentrant.Service();Check(reentrant.Status().prepared);
    reentrant.End();reentrant.Service();Check(reentrant.Drained() && roots.empty());
    // The default consumer uses the SDK bundle, not only the test overrides.
    // Removal waits for a manager receipt; it does not wait for a fade to end.
    requests.clear();created.clear();callbacks.trigger={};callbacks.remove={};
    const auto prior_removals=explicit_removals.size();
    auto integrated=std::make_shared<VisualProgram>(*program);
    integrated->actions.at("resolve").enter.push_back({VisualRemove{{"recipient"},true,true},"explicit removal",{}, {}});
    VisualProgramPlayer native_services(integrated,callbacks);intent=native_services.Select("gift");native_services.Service();
    for(auto& p:requests)Complete(*p);native_services.Service();Check(native_services.Played(intent,300,5));
    native_services.Observe(300,5,"Gift",0.7f);native_services.Service();Check(default_triggers==1);
    release_on_remove=false;native_services.Select("resolve");native_services.Service();Check(!native_services.Status().prepared);
    native_services.Service();Check(native_services.Status().prepared && native_services.Status().issues.empty());
    Check(explicit_removals.size()==prior_removals+1 && explicit_removals.back().fade && explicit_removals.back().immediate_fade && !explicit_removals.back().unbind);
    Check(!requests[1]->released);requests[1]->released=true;
    native_services.End();release_on_remove=true;native_services.Service();Check(native_services.Drained() && roots.empty());

    // A successor call does not imply entry. Its tail waits for this exact
    // receipt/model, runs once, then lives naturally until an explicit exit.
    requests.clear();created.clear();release_on_remove=false;
    auto tail_program=std::make_shared<VisualProgram>(ParseVisualProgram(R"({
      "format":"hksr.visual-program","id":"tail","source":"config","scope":"scene",
      "effects":{"hit":{"path":"hit","target":"Target"}},
      "actions":{"resolve":{"state":"Resolve","restart":true,"completion_state":"StandBy","completion_at":0.2,
        "after_completion":[{"op":"timeline_stop","source":"stop","key":"scene"},
          {"op":"create","source":"hit","effects":["hit"]}]}}
    })"));
    unsigned stop_calls=0;
    callbacks.timeline=[&](auto,const auto& op){Check(!op.play);++stop_calls;return VisualStepResult{VisualStepState::Done,{}};};
    VisualProgramPlayer natural(tail_program,callbacks);
    intent=natural.Select("resolve");natural.Service();Check(natural.Played(intent,400,6));
    natural.Observe(400,6,"Resolve",1);natural.Service();Check(natural.Status().finished);
    Check(natural.SuccessorPlayed(intent,401,7));natural.Service();Check(created.empty() && stop_calls==0);
    natural.Observe(400,7,"StandBy",1);natural.Observe(401,6,"StandBy",1);
    natural.Observe(401,7,"Resolve",1);natural.Service();Check(created.empty());
    natural.Observe(401,7,"StandBy",0.1f);natural.Service();Check(created.empty());
    const auto finished_context=natural.Context();
    natural.Observe(401,7,"StandBy",0.3f);natural.Service(false);
    Check(created.empty() && stop_calls==0); // pending body intent pauses commands, not collection
    natural.Service();
    Check(natural.Status().completed && !natural.Context() && created.size()==1 && stop_calls==1);
    Check(!natural.Played(intent,400,6) && !natural.SuccessorPlayed(intent,401,7));
    natural.Service();Check(requests[0]->removes==0 && !finished_context->Drained());
    Complete(*requests[0]);natural.Service();Check(requests[0]->removes==0);
    requests[0]->released=true;natural.Service();Check(finished_context->Drained() && roots.empty());
    natural.End();natural.Service();Check(natural.Drained());

    // Removal during async loading keeps the exact tail receipt and removes
    // a late completion; repeated End does not repeat cancellation calls.
    requests.clear();created.clear();VisualProgramPlayer interrupted(tail_program,callbacks);
    intent=interrupted.Select("resolve");interrupted.Service();Check(interrupted.Played(intent,500,8));
    interrupted.Observe(500,8,"Resolve",1);interrupted.Service();Check(interrupted.SuccessorPlayed(intent,501,9));
    interrupted.Observe(501,9,"StandBy",0.3f);interrupted.Service();Check(created.size()==1);
    interrupted.Leave();interrupted.Service();Check(requests[0]->removes==1 && !requests[0]->released);
    Complete(*requests[0]);release_on_remove=true;interrupted.Service();Check(requests[0]->released);
    interrupted.End();interrupted.Service();Check(interrupted.Drained() && roots.empty());

    // Finishing does not drop already executing native producers. New explicit
    // creations stop, but a late native receipt can still finish naturally.
    requests.clear();auto finishing=std::make_shared<VisualEffectContext>(services);
    auto late=std::make_unique<Params>();late->id=999;late->state=3;auto* late_raw=late.get();requests.push_back(std::move(late));
    auto owner=TrackedEntities().Track(reinterpret_cast<GameEntity*>(&entity));
    AnimationSequenceExecution execution{1,1,1,0,{owner,reinterpret_cast<GameWorld*>(&manager)},&effect,nullptr,AnimationSequenceKind::Persistent};
    VisualEffectBindings producer;producer.Bind(execution,finishing);finishing->Finish();
    Check(!finishing->Drained());bool refused=false;
    try{finishing->Start("new",&entity,&path);}catch(const std::logic_error&){refused=true;}Check(refused);
    finishing->Track("late",&manager,{late_raw->id,0,late_raw});finishing->Service();Check(late_raw->removes==0);
    producer.Clear();Check(!finishing->Drained());late_raw->released=true;finishing->Service();
    Check(finishing->Drained() && roots.empty());TrackedEntities().Forget(owner);delegates.clear();

    // Exercise the real context/player boundaries, not just a standalone lease.
    EntityLifecycle lifecycle;
    const EntityOrigin caster{lifecycle.Track(reinterpret_cast<GameEntity*>(&entity)),reinterpret_cast<GameWorld*>(&manager)};
    const EntityOrigin recipient{lifecycle.Track(reinterpret_cast<GameEntity*>(&other_entity)),caster.world};
    std::vector<void*> bound;unsigned binds=0,clears=0;bool motion_loaded=false;
    VisualEntityMotionServices motion_services;
    motion_services.read=[&](auto){return EffectEntityFollowObservation{{motion_loaded?EffectControlState::Applied:EffectControlState::NotLoaded},bound};};
    motion_services.target=[&](void* e){return EffectMotionTargetObservation{{EffectControlState::Applied},std::find(bound.begin(),bound.end(),e)!=bound.end()};};
    motion_services.eligible=[](auto){return true;};
    motion_services.set=[&](auto,const auto& expected,const auto& desired,auto current){
        Check(current());
        Check(bound==expected);bound=desired;if(desired.empty())++clears;else ++binds;
        return EffectEntityFollowObservation{{EffectControlState::Applied},bound};
    };
    auto motion=std::make_shared<VisualEntityMotion>(lifecycle,std::move(motion_services));
    auto motion_program=std::make_shared<VisualProgram>(ParseVisualProgram(R"({
      "format":"hksr.visual-program","id":"follow","source":"config","scope":"scene",
      "effects":{"ground":{"path":"ground","target":"Target"}},
      "actions":{"enter":{"state":"Enter","restart":true,"enter":[
        {"op":"create","effects":["ground"],"source":"create"},
        {"op":"follow","effect":"ground","targets":["Target"],"source":"bind"}],
        "lanes":[[{"at":0.5,"instructions":[{"op":"follow","effect":"ground","targets":[],"source":"clear"}]}]]}}
    })"));
    Params motion_params;allocating=&motion_params;on_submit={};release_on_remove=true;
    VisualProgramServices motion_callbacks;motion_callbacks.effects=services;
    motion_callbacks.create=[](auto& context,const auto& key,const auto&){return context.Start(key,&entity,&path);};
    motion_callbacks.follow=[&](auto& context,auto id,const VisualFollow& follow) {
        context.UseEntityMotion(motion,caster);
        const auto result=context.BindTargets(id,follow.targets.empty()?std::vector<EntityOrigin>{}:std::vector<EntityOrigin>{recipient});
        return VisualStepResult{result.state==EffectControlState::Applied?VisualStepState::Done:
            result.state==EffectControlState::NotLoaded?VisualStepState::Pending:VisualStepState::Failed,result.detail};
    };
    VisualProgramPlayer moving(motion_program,motion_callbacks);
    const auto moving_intent=moving.Select("enter");moving.Service();
    Check(!moving.Status().prepared && binds==0);Complete(motion_params);motion_loaded=true;moving.Service();
    Check(moving.Status().prepared && binds==1 && moving.Played(moving_intent,600,1));
    moving.Observe(600,1,"Enter",0.6f);moving.Service();Check(clears==1 && motion->Empty());
    moving.End();moving.Service();Check(moving.Drained());
    motion_params={};motion_loaded=true;
    VisualProgramPlayer cancelled(motion_program,motion_callbacks);cancelled.Select("enter");cancelled.Service();
    Check(binds==2 && !bound.empty());Complete(motion_params);
    on_remove=[&]{Check(bound.empty() && clears==2);};
    cancelled.End();Check(clears==1);cancelled.Service();
    Check(cancelled.Drained() && motion->Empty() && roots.empty());on_remove={};

    // Actual entry/view event ordering. Events have no body-progress trigger,
    // preserve FIFO through loading, and do not replay completed instructions.
    auto view_program=std::make_shared<VisualProgram>(ParseVisualProgram(R"({
      "format":"hksr.visual-program","id":"views","source":"config","scope":"scene",
      "effects":{"ground":{"path":"ground","target":"Target"}},
      "actions":{"start":{"state":"Start","restart":true,"enter":[
        {"op":"create","effects":["ground"],"source":"start[16]"},
        {"op":"follow","effect":"ground","targets":["Target"],"source":"start[17]"},
        {"op":"effect_state","effect":"ground","state":"Move","source":"start[18]"}],
        "lanes":[[{"at":0.2,"instructions":[{"op":"assign","variable":"body","value":1,"source":"body"}]}]]},
        "next":{"state":"Next"}},
      "signals":{
        "enter_view":[{"op":"effect_state","effect":"ground","state":"Idle","source":"view enter[0]"},
                      {"op":"follow","effect":"ground","targets":[],"source":"view enter[1]"}],
        "exit_view":[{"op":"effect_state","effect":"ground","state":"Move","source":"view exit[0]"},
                     {"op":"follow","effect":"ground","targets":["Target"],"source":"view exit[1]"}]}
    })"));
    const auto& effect_state=std::get<VisualEffectState>(view_program->actions.at("start").enter.at(2).operation);
    Check(effect_state.state=="Move" && !effect_state.restart && effect_state.transition==0);
    motion_params={};std::vector<std::string> order;bool state_loaded=true;std::function<void()> during_state;
    auto view_callbacks=motion_callbacks;
    view_callbacks.follow=[&](auto& context,auto id,const auto& op) {
        const auto result=motion_callbacks.follow(context,id,op);
        if(result.state==VisualStepState::Done)order.push_back(op.targets.empty()?"clear":"bind");
        return result;
    };
    view_callbacks.effect_state=[&](auto& context,auto id,const VisualEffectState& op) {
        if(!state_loaded)return VisualStepResult{VisualStepState::Pending,{}};
        const auto result=context.SetAnimatorState(id,op.state,op.transition,op.restart);
        if(result.state!=EffectControlState::Applied)return VisualStepResult{VisualStepState::Pending,{}};
        order.push_back(op.state);if(during_state)during_state();
        return VisualStepResult{VisualStepState::Done,{}};
    };
    VisualProgramPlayer views(view_program,view_callbacks);auto view_intent=views.Select("start");views.Service();
    Check(order==std::vector<std::string>{"bind"} && !views.Status().prepared);
    Check(!views.Signal(view_intent,"enter_view"));Complete(motion_params);views.Service();
    Check(order==std::vector<std::string>({"bind","Move"}) && views.Played(view_intent,700,1));
    const auto enter_signal=views.Signal(view_intent,"enter_view");
    const auto exit_signal=views.Signal(view_intent,"exit_view");Check(enter_signal && exit_signal>enter_signal);
    state_loaded=false;views.Observe(700,1,"Start",0.3f);views.Service();
    Check(views.Status().finished && !views.Status().completed_signal && order.size()==2 && !bound.empty());
    state_loaded=true;views.Service(false);Check(order.size()==2);
    views.Service();Check(order==std::vector<std::string>({"bind","Move","Idle","clear","Move","bind"}));
    Check(views.Status().completed_signal==exit_signal && views.Status().issues.empty());
    views.Service();Check(order.size()==6);
    Check(views.Signal(view_intent,"enter_view"));view_intent=views.Select("next");views.Service();
    Check(order.size()==6 && !views.Signal(view_intent-1,"enter_view"));
    Check(views.Played(view_intent,701,2));
    Check(views.Signal(view_intent,"enter_view"));
    during_state=[&]{during_state={};views.Select("next");views.Service();};
    views.Service();Check(order.back()=="Idle" && !bound.empty()); // no stale clear after replacement
    views.Service();Check(views.Status().prepared && order.size()==7);
    bool unknown=false;try{views.Signal(views.Status().intent,"unknown");}catch(const std::invalid_argument&){unknown=true;}Check(unknown);
    Check(views.Signal(views.Status().intent,"enter_view"));views.Leave();views.Service();
    Check(bound.empty() && order.size()==7);views.End();views.Service();Check(views.Drained() && roots.empty());
    lifecycle.Forget(caster.token);lifecycle.Forget(recipient.token);delegates.clear();
    TestVisualLifetimes();
    TestVisualSnapshots();
}
