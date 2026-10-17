#include <hksr/framework/presentation/VisualEffectContext.hpp>
#include <hksr/framework/presentation/VisualEffectBindings.hpp>
#include <hksr/framework/presentation/VisualEventRoutes.hpp>
#include <hksr/framework/presentation/VisualProgram.hpp>
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
struct Params {unsigned id=0;bool released=false;int state=0;void* callback=nullptr;void* target=nullptr;int removes=0;};
Params a,b,c;Params* allocating=&a;
std::map<int,void*> roots;
std::vector<std::unique_ptr<std::array<unsigned char,128>>> delegates;
EffectCompletionLayout layout{24,40,64};
std::vector<void*> controlled;
std::function<void()> on_submit;
bool release_on_remove=false;
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
    controls.progress=[](EffectReference ref,float,float){controlled.push_back(ref.parameters);const auto* p=static_cast<Params*>(ref.parameters);
        return EffectControlResult{p->state==3?EffectControlState::Applied:EffectControlState::NotLoaded,{true,0},true,{}};};
    controls.animator=[progress=controls.progress](EffectReference ref,int,float duration,bool){return progress(ref,0,duration);};
    return std::make_shared<PresentationEffects>(std::move(requests),std::move(controls),
        [](void* raw,void* caster,void* target,void* callback,const EntityEffectOptions&) {
            Check(caster==&entity && target==&other_entity);auto* p=static_cast<Params*>(raw);
            p->target=target;p->callback=callback;return EffectInputResult{{true,0},{true,0}};
        });
}
}
void TestVisualEffectContext() {
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
    const auto receipt=observed.Track("event scene",&manager,{0,0,&a});
    Check(observed.Track("event scene",&manager,{0,0,&a})==receipt && observed.PendingRequests()==1);
    bool conflict=false;try{observed.Track("unrelated",&manager,{0,0,&a});}catch(const std::logic_error&){conflict=true;}Check(conflict);
    conflict=false;try{observed.Track("event scene",&other_entity,{0,0,&a});}catch(const std::logic_error&){conflict=true;}Check(conflict);
    Check(delegates.size()==previous_delegates && a.callback==&official_callback);
    release_on_remove=false;observed.End(false);observed.Service();Check(a.removes==1);
    const auto late=observed.Track("late event",&manager,{0,0,&b});
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
    submission.trigger.serial=8;submission.manager=&manager;submission.request={0,0,&a};
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
    program->End(false);b={};b.state=3;submission.request={0,0,&b};
    Check(bindings.Track("late-event",submission).has_value() && b.removes==0);
    program->Service();Check(!program->Drained() && a.removes==1 && b.removes==1 && roots.empty());
    c={};c.state=3;submission.request={0,0,&c};Check(bindings.Track("after-empty",submission).has_value());
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
    VisualEventRoutes routes([&](const auto&){++reads;return definitions;},
        [&](const auto&,const auto&){++tracks;return AnimationSequenceTracking::Tracked;});
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
    submission.sequence=execution;submission.origin=execution.origin;submission.request={0,0,&a};
    Check(routes.Submitted(submission).has_value());
    done.execution=execution;routes.Observe(done);Check(!gift->Closing() && gift->PendingRequests()==1);
    begin.execution.definition=11;begin.execution.serial=102;begin.execution.sequence=&sequence2;routes.Observe(begin);
    submission.sequence=begin.execution;b={};b.state=3;b.callback=&official_callback;submission.request={0,0,&b};
    Check(routes.Submitted(submission).has_value() && gift->PendingRequests()==2 && a.removes==0);
    // An unrelated child collection cannot be mapped by config/output index.
    source.serial=200;routes.Before(source,recipe[0].file);built.serial=12;built.configurations=&unrelated;
    routes.Built(source,built);routes.After(source);Check(tracks==2);
    // A shared collection with an unselected event is genuinely ambiguous.
    definitions.rows.push_back({AnimationEventPart::TimePoint,99,{"Other"},{},&collection1});
    source.serial=201;routes.Before(source,recipe[0].file);built.configurations=&collection1;
    routes.Built(source,built);routes.After(source);Check(tracks==2);
    // Retire only the program. In-flight submissions retain their old scope;
    // a new Begin after it ended cannot prolong or reassign that scope.
    gift->End(false);c={};c.state=3;submission.request={0,0,&c};Check(routes.Submitted(submission).has_value());
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
    routes.Observe(begin);a={};a.state=3;submission.sequence=begin.execution;submission.request={0,0,&a};
    Check(routes.Submitted(submission).has_value());
    routes.Detach(declared);b={};b.state=3;submission.request={0,0,&b};
    Check(routes.Submitted(submission).has_value()); // already in-flight
    done.execution=begin.execution;routes.Observe(done);
    begin.execution.serial=302;routes.Observe(begin);submission.sequence=begin.execution;Check(!routes.Submitted(submission));
    auto next_context=std::make_shared<VisualEffectContext>(services);
    routes.Attach(declared,&unrelated,next_context);begin.execution.serial=303;routes.Observe(begin);submission.sequence=begin.execution;
    Check(!routes.Submitted(submission)); // other model receiver, same entity
    routes.Attach(declared,&receiver,next_context);begin.execution.serial=304;routes.Observe(begin);submission.sequence=begin.execution;
    c={};c.state=3;submission.request={0,0,&c};Check(routes.Submitted(submission).has_value());
    Check(later->PendingRequests()==2 && next_context->PendingRequests()==1);
    later->End(false);next_context->End(false);later->Service();next_context->Service();routes.Unbind(declared);routes.Clear();
    Check(roots.empty());
    a={};allocating=&a;VisualEffectContext targeted(services);
    on_submit=[&]{targeted.End(false);targeted.Service();};
    EntityEffectOptions options;options.unique_name="explicit-target";
    const auto target_id=targeted.StartEntity("target-scene",&entity,&other_entity,&path,options);on_submit={};
    Check(a.target==&other_entity && targeted.Status(target_id)->cancelled && a.removes==0);
    targeted.Service();Check(targeted.Drained() && a.removes==1 && roots.empty());
    delegates.clear();
}

void TestVisualProgram(const char* file) {
    std::shared_ptr<const VisualProgram> program;
    if(file) {
        std::ifstream input(file,std::ios::binary);Check(static_cast<bool>(input));
        program=std::make_shared<VisualProgram>(ParseVisualProgram({std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()}));
        Check(program->effects.size()==18 && program->actions.size()==3);
        Check(program->effects.at("Monster").target=="AbilityTargetAndAdjoinEntity");
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
    callbacks.attach=[&](auto){++attaches;};callbacks.detach=[&]{++detaches;};callbacks.cancel_timelines=[&]{++cancellations;};
    callbacks.timeline=[&](const VisualTimeline& input){timeline.push_back(input.key);return VisualStepResult{VisualStepState::Done,{}};};
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
    for(auto& p:requests)if(p->removes)p->released=true; // engine finishes prior fades
    player.End(false);release_on_remove=true;player.Service();Check(player.Drained() && roots.empty());
    if(file){delegates.clear();return;}
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
    reentrant.End();reentrant.Service();Check(reentrant.Drained() && roots.empty());delegates.clear();
}
