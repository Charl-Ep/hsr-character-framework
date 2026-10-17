#include "../../src/presentation/VisualProgramCallbacks.hpp"
#include <hksr/metadata/MetadataResolver.hpp>
#include <cstddef>
#include <stdexcept>

namespace {
using namespace hksr::api;
using namespace hksr::framework;
int metadata,actor,world,manager,next_root=0;
volatile int calls[16]{};
std::map<int,void*> roots;
struct Request {GameEntity* caster=reinterpret_cast<GameEntity*>(&actor);unsigned id=17;};
void Check(bool value){if(!value)throw std::runtime_error("visual program observer assembly test failed");}
template<unsigned N> __declspec(noinline) void __fastcall Entry(void*,void*,void*) {
    ++calls[N];if(calls[N]>1000)calls[N]=0;
}
template<class T>Method M(T function){return {reinterpret_cast<void*>(function),&metadata};}
int __fastcall Keep(void* object,int,int,void*){roots[++next_root]=object;return next_root;}
void* __fastcall Target(int id,void*){return roots.at(id);}
void __fastcall Free(int id,void*){roots.erase(id);}
struct Methods:MethodResolver {
    Method Resolve(const char*,const char* type,const char* name,const char*,std::initializer_list<const char*>,MethodKind) override {
        const std::string owner=type,method=name;
        if(owner=="TaskContext" && method=="get_CasterEntity")return M(Entry<15>);
        if(owner=="MonoEffectManager" && method=="FireEffectAsync")return M(Entry<0>);
        if(owner=="MonoEffectManager" && method=="FireEffect")return M(Entry<1>);
        if(owner=="NFGENDDOCJJ" && method=="OnTaskBegin")return M(Entry<2>);
        if(owner=="KFELGAIGHAP") {
            if(method=="OnTaskBegin")return M(Entry<3>);
            if(method=="Tick")return M(Entry<4>);
            if(method=="OnTaskReset")return M(Entry<5>);
            if(method=="Dispose")return M(Entry<6>);
        }
        if(owner=="MKDHBHBFEDD") {
            if(method=="OnTaskBegin")return M(Entry<7>);
            if(method=="OnTaskReset")return M(Entry<8>);
            if(method=="Dispose")return M(Entry<9>);
        }
        if(owner=="JKNLHJLPIOO" && method=="KJCNEJALIJO")return M(Entry<10>);
        if(owner=="HHECHLOIEJO" && method=="FGAHFIBMPCP")return M(Entry<11>);
        throw std::runtime_error("unexpected observer method: "+owner+"."+method);
    }
};
struct Types:ObjectMetadata {
    DeclaredType ReadObjectType(void*)override{throw std::logic_error("unexpected object enumeration");}
    std::vector<DeclaredField> ReadObjectFields(void*)override{return {};}
    RuntimeTypeName ReadObjectTypeName(void*)override{return {};}
};
std::vector<DeclaredField> Fields(const char* ns,const char* name) {
    const std::string type=name;
    if(type=="TriggerEffectParams")return {
        {ns,name,"CasterEntity","RPG.GameCore.GameEntity",offsetof(Request,caster),6,true},
        {ns,name,"<UniqueEffectID>k__BackingField","System.UInt32",offsetof(Request,id),6,true}};
    if(type=="NFGENDDOCJJ")return {
        {ns,name,"OFKGLJOAMLD","RPG.GameCore.TriggerEffect",24,6,true},
        {ns,name,"DDDNLILNMKA","RPG.GameCore.TaskContext",32,6,true}};
    if(type=="KFELGAIGHAP" || type=="MKDHBHBFEDD")return {
        {ns,name,type=="KFELGAIGHAP"?"<TaskContext>k__BackingField":"DDDNLILNMKA","RPG.GameCore.TaskContext",24,6,true},
        {"","GELIALJDFBL","DELMCJFBNBL","RPG.GameCore.TaskState",16,6,true}};
    throw std::logic_error("unexpected observer fields");
}
}
void TestVisualProgramObservers() {
    using namespace hksr::framework;
    Methods methods;Types types;ReferenceMethods refs{M(Keep),M(Target),M(Free),{}, {}};
    const auto identify=[](GameEntity* entity){return EntityOrigin{{entity,1},reinterpret_cast<GameWorld*>(&world)};};
    VisualEventRoutes routes([](const auto&){return AnimationEventDefinitions{};},
        [](const auto&,const auto&){return AnimationSequenceTracking::Unavailable;});
    auto callbacks=detail::ConnectVisualProgramCallbacks(routes,[](void*){return std::string{};});
    Check(callbacks.sequence && callbacks.before_construction && callbacks.tasks && callbacks.after_construction &&
        callbacks.before_effect && callbacks.after_effect);
    AnimationSequenceExecutions sequences(methods,types,refs,Fields,identify,callbacks.sequence);
    AnimationEventConstruction constructions(methods,refs,identify,callbacks.before_construction,callbacks.tasks,callbacks.after_construction);
    unsigned before=0,after=0;
    EffectSubmissions effects(methods,refs,Fields,identify,
        [&](const auto& value){++before;callbacks.before_effect(value);},
        [&](const auto& value,bool returned){++after;Check(returned);callbacks.after_effect(value,returned);});
    sequences.ActivateOnCurrentThread();constructions.ActivateOnCurrentThread();effects.ActivateOnCurrentThread();
    Request request;Entry<0>(&manager,&request,&metadata);
    Check(before==1 && after==1 && calls[0]==1 && roots.empty());
    effects.Stop();constructions.Stop();Check(sequences.Close());routes.Clear();
}
