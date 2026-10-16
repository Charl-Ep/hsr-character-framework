#include <hksr/framework/presentation/VisualEffectContext.hpp>
#include <hksr/framework/presentation/VisualEffectBindings.hpp>
#include <hksr/framework/presentation/VisualEventRoutes.hpp>
#include <hksr/framework/presentation/PresentationProfileFiles.hpp>
#include <array>
#include <cstring>
#include <functional>
#include <map>
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
void Check(bool ok){if(!ok)throw std::runtime_error("visual effect context test failed");}
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
void __fastcall Submit(void* service,Params* p,void*){Check(service==&manager);p->state=1;if(on_submit)on_submit();}
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
    return std::make_shared<PresentationEffects>(std::move(requests),std::move(controls));
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
    program->Service();Check(program->Drained() && a.removes==1 && b.removes==1 && roots.empty());
    // A new playback cannot take effects still attributed to the old one.
    auto replacement=std::make_shared<VisualEffectContext>(services);execution.serial=5;bindings.Bind(execution,replacement);
    Check(!bindings.Track("old-playback",submission) && bindings.Size()==1);
    bool duplicate=false;try{bindings.Bind(execution,program);}catch(const std::logic_error&){duplicate=true;}Check(duplicate);
    done.execution=execution;done.continues=false;done.original_returned=false;bindings.Observe(done);Check(bindings.Size()==0);
    bindings.Bind(execution,replacement);replacement.reset();bindings.Collect();Check(bindings.Size()==0);
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
    routes.Clear();delegates.clear();
}
