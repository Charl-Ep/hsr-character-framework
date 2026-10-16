#include <hksr/framework/presentation/VisualEffectContext.hpp>
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
    delegates.clear();
}
