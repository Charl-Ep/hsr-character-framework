#include "../support/ModelServicesFixture.hpp"
#include <hksr/framework/appearance/PrivateVisual.hpp>
#include <hksr/framework/appearance/ArtSlot.hpp>
#include <Windows.h>
#include <map>
#include <stdexcept>
#include <iostream>
#include <cstddef>
namespace {
using namespace hksr;
struct Node { bool alive=false,active=true; unsigned destroy=0; Node* parent=nullptr; } prefab,source,staging,art;
int info,type,animator_type,animator,components,path,sequence=0,loads=0,releases=0,clones=0;
int animator_count=1;
bool ctor_fault=false,clone_fault=false,return_alias=false,activate_fault=false,child_fault=false;
int logical,key,art_reads=0,private_releases=0,payload_releases=0;
bool supply_fault=false,skip_adoption=false;
struct Model { void* path=&logical; Node* current=&prefab; } model;
std::map<int,void*> handles;
void Check(bool ok) { if(!ok) throw std::runtime_error("private visual ownership mismatch"); }
template<class T> api::Method M(T f) { return {reinterpret_cast<void*>(f),&info}; }
int __fastcall Keep(void* p,int zero,int kind,void*) { Check(!zero && kind==2); handles[++sequence]=p; return sequence; }
void* __fastcall Target(int id,void*) { return handles.at(id); }
void __fastcall Free(int id,void*) { handles.erase(id); }
void* __fastcall Load(void* p,void* t,bool option,void*) { Check(p==&path && t==&type && !option); ++loads; return &prefab; }
void __fastcall Release(void* p,void*) { Check(p==&prefab); ++releases; }
bool __fastcall Alive(Node* p,void*) { return p->alive; }
void __fastcall Destroy(Node* p,void*) { Check(p==&staging || p==&art); ++p->destroy; } // never destroy source/template
void __fastcall Initialize(Node* p,void*) {
    Check(p==&staging && !handles.empty()); p->alive=true;
    if(ctor_fault) RaiseException(0xE0630001,0,0,nullptr);
}
void __fastcall Active(Node* p,bool active,void*) {
    Check(p==&staging || (p==&art && !staging.active)); p->active=active;
    if(activate_fault && p==&art) RaiseException(0xE0630002,0,0,nullptr);
}
void* __fastcall Clone(Node* p,Node* parent,bool world,void*) {
    Check((p==&source || p==&prefab) && parent==&staging && !parent->active && !world);
    ++clones;
    if(return_alias) return p;
    art.alive=true; art.parent=parent;
    if(clone_fault) RaiseException(0xE0630003,0,0,nullptr); // created but no returned pointer
    return &art;
}
void* __fastcall GameObject(void* p,void*) { Check(p==&animator); return &source; }
int __fastcall ChildCount(Node* p,void*) {
    Check(p==&staging);
    if(child_fault) RaiseException(0xE0630004,0,0,nullptr);
    return art.alive && art.parent==p ? 1 : 0;
}
void* __fastcall Transform(Node* p,void*) { Check(p==&staging || p==&art); return p; }
void* __fastcall Components(void* p,void* t,bool inactive,void*) {
    Check(p==&prefab && t==&animator_type && inactive); return &components;
}
int __fastcall Length(void* p,void*) { Check(p==&components); return animator_count; }
void* __fastcall Get(void* p,int i,void*) { Check(p==&components && i==0); return &animator; }
struct Objects:api::ManagedObjects {
    api::ObjectResult NewStringUtf8(const char* text) override {
        if(std::string(text)=="ArtRoot") return {{true,0},&key};
        Check(std::string(text)=="asset/player.prefab"); return {{true,0},&path};
    }
    api::ObjectResult AllocateObject(const char* space,const char* name) override {
        Check(std::string(space)=="UnityEngine" && std::string(name)=="GameObject"); return {{true,0},&staging};
    }
    api::ObjectResult ManagedType(const char* space,const char* name) override {
        Check(std::string(space)=="UnityEngine");
        if(std::string(name)=="GameObject") return {{true,0},&type};
        Check(std::string(name)=="Animator"); return {{true,0},&animator_type};
    }
};
void Reset() {
    Check(handles.empty() && loads==releases);
    staging={}; art={}; prefab.alive=source.alive=true;
    ctor_fault=clone_fault=return_alias=activate_fault=child_fault=false; animator_count=1;
}
void DrainParent() {
    Check(staging.destroy==1); staging.alive=false;
    if(art.parent==&staging) art.alive=false;
}
void* __fastcall Art(void* p,void*) { Check(p==&model); ++art_reads; return model.current; }
__declspec(noinline) void __fastcall Unload(void* p,bool release,void*) {
    Check(p==&model && release);
    if(model.current==&art) ++private_releases;
    model.current=nullptr;
}
__declspec(noinline) void __fastcall RootRelease(void* p,void*) { Check(p==&model); model.current=nullptr; }
void __fastcall Supply(void* p,void* k,void* label,bool option,void* callback,void* provided,void*) {
    Check(p==&model && k==&key && label==&logical && !option && !callback && provided==&art);
    Unload(p,true,&info); // actual local pre-release hook, old NPC Art
    if(!skip_adoption) { model.current=&art; art.parent=nullptr; }
    if(supply_fault) RaiseException(0xE0630005,0,0,nullptr);
}
void BeforePayload(const api::ModelReleaseEvent&,void*) { Check(model.current==&art); ++payload_releases; }
void FailedPayload(const api::ModelReleaseEvent&,const char*,void*) noexcept { std::terminate(); }
struct ReleaseResolver:api::MethodResolver {
    api::Method Resolve(const char*,const char*,const char* name,const char*,std::initializer_list<const char*>,api::MethodKind) override {
        if(std::string(name)=="OnArtNodeUnload") return M(Unload);
        Check(std::string(name)=="_ReleaseRootModel"); return M(RootRelease);
    }
};
}
void TestPrivateVisuals() {
    Objects objects;
    const api::ReferenceMethods refs{M(Keep),M(Target),M(Free),M(Load),M(Release)};
    const api::UnityLifetimeMethods lifetime{M(Alive),M(Destroy)};
    const api::VisualObjects visuals({M(Initialize),M(Active),M(Clone),M(GameObject),M(ChildCount)});
    const api::SceneObjects scene({M(Transform),M(Components),{}, {}});
    const api::ManagedArrays arrays({{},M(Length),M(Get),{}});
    {
        Reset(); framework::PrivateVisual visual(*TestModelServices(refs,lifetime));
        Check(visual.Prepare(objects,scene,arrays,visuals,"asset/player.prefab",framework::VisualRoot::UniqueAnimator).complete);
        Check(visual.Status().source==&source && !staging.active && art.active && art.parent==&staging);
        Check(!visual.Close(visuals).complete && releases==loads-1);
        DrainParent(); Check(visual.Close(visuals).complete && handles.empty());
    }
    {
        Reset(); framework::PrivateVisual visual(*TestModelServices(refs,lifetime));
        Check(visual.Prepare(objects,scene,arrays,visuals,"asset/player.prefab",framework::VisualRoot::Prefab).complete);
        Check(visual.Status().source==&prefab); visual.BeginSupply();
        visual.RetireEmptyStaging(visuals); Check(staging.destroy==0); // failed/uncertain handoff: no destructive fallback
        Check(!visual.Close(visuals).complete && staging.destroy==0 && art.alive);
        art.alive=false; Check(!visual.Close(visuals).complete && staging.destroy==1);
        DrainParent(); Check(visual.Close(visuals).complete && handles.empty());
    }
    {
        Reset(); framework::PrivateVisual visual(*TestModelServices(refs,lifetime));
        Check(visual.Prepare(objects,scene,arrays,visuals,"asset/player.prefab",framework::VisualRoot::UniqueAnimator).complete);
        visual.BeginSupply(); art.parent=nullptr; // official adoption reparents Art
        child_fault=true; visual.RetireEmptyStaging(visuals); Check(staging.destroy==0);
        child_fault=false; visual.RetireEmptyStaging(visuals); Check(staging.destroy==1);
        DrainParent(); Check(art.alive && !visual.Close(visuals).complete && releases==loads-1);
        art.alive=false; Check(visual.Close(visuals).complete && handles.empty());
    }
    {
        Reset(); animator_count=2; const auto before=clones;
        framework::PrivateVisual visual(*TestModelServices(refs,lifetime));
        Check(!visual.Prepare(objects,scene,arrays,visuals,"asset/player.prefab",framework::VisualRoot::UniqueAnimator).complete);
        Check(clones==before && !staging.alive && visual.Close(visuals).complete && handles.empty());
    }
    {
        Reset(); ctor_fault=true; framework::PrivateVisual visual(*TestModelServices(refs,lifetime));
        Check(!visual.Prepare(objects,scene,arrays,visuals,"asset/player.prefab",framework::VisualRoot::Prefab).complete);
        Check(!visual.Close(visuals).complete); DrainParent(); Check(visual.Close(visuals).complete && handles.empty());
    }
    {
        Reset(); clone_fault=true; framework::PrivateVisual visual(*TestModelServices(refs,lifetime));
        Check(!visual.Prepare(objects,scene,arrays,visuals,"asset/player.prefab",framework::VisualRoot::Prefab).complete);
        Check(art.alive && !visual.Status().art && !visual.Close(visuals).complete);
        DrainParent(); Check(!art.alive && visual.Close(visuals).complete && handles.empty());
    }
    {
        Reset(); return_alias=true; framework::PrivateVisual visual(*TestModelServices(refs,lifetime));
        Check(!visual.Prepare(objects,scene,arrays,visuals,"asset/player.prefab",framework::VisualRoot::Prefab).complete);
        Check(!visual.Close(visuals).complete); DrainParent(); Check(visual.Close(visuals).complete && prefab.alive && handles.empty());
    }
    {
        Reset(); activate_fault=true; framework::PrivateVisual visual(*TestModelServices(refs,lifetime));
        Check(!visual.Prepare(objects,scene,arrays,visuals,"asset/player.prefab",framework::VisualRoot::Prefab).complete);
        Check(!visual.Close(visuals).complete); DrainParent(); Check(visual.Close(visuals).complete && handles.empty());
    }
    Reset();
    ReleaseResolver resolver; api::InstallModelReleaseHooks(resolver);
    const api::ArtAdmission admission({M(Supply),M(Unload),{},offsetof(Model,path),true});
    const api::ModelAccess access({M(Art)});
    auto* component=reinterpret_cast<api::CharacterModelComponent*>(&model);
    {
        framework::PrivateVisual visual(*TestModelServices(refs,lifetime));
        Check(visual.Prepare(objects,scene,arrays,visuals,"asset/player.prefab",framework::VisualRoot::UniqueAnimator).complete);
        framework::ArtSlot slot(visual,TestModelServices(refs,lifetime,{}, {},access,admission),{BeforePayload,FailedPayload,nullptr});
        model.current=&prefab; slot.Submit(objects,component);
        Check(slot.Status().adopted && slot.Status().previous_art_releases==1 && payload_releases==0);
        Check(slot.Status().supply.returned && !slot.Close().complete);
        slot.RequestUnload(); Check(payload_releases==1 && private_releases==1 && slot.Status().release_seen);
        slot.RequestUnload(); Check(private_releases==1); // never repeat side effect
        Check(!slot.Close().complete && slot.Status().binding_detached && art.alive);
        const auto next_binding=api::RegisterModelRelease(component,{BeforePayload,FailedPayload,nullptr});
        // Repeated old closure cannot unregister a newer model incarnation.
        Check(!slot.Close().complete);
        api::ForgetModelRelease(next_binding);
        Check(!visual.Close(visuals).complete); DrainParent(); art.alive=false;
        Check(visual.Close(visuals).complete && slot.Close().complete && handles.empty());
    }
    {
        Reset(); framework::PrivateVisual visual(*TestModelServices(refs,lifetime));
        Check(visual.Prepare(objects,scene,arrays,visuals,"asset/player.prefab",framework::VisualRoot::UniqueAnimator).complete);
        framework::ArtSlot slot(visual,TestModelServices(refs,lifetime,{}, {},access,admission),{BeforePayload,FailedPayload,nullptr});
        model.current=&prefab; supply_fault=true; slot.Submit(objects,component); supply_fault=false;
        Check(slot.Status().adopted && !slot.Status().supply.returned && !slot.Status().errors.empty());
        slot.RequestUnload(); Check(payload_releases==2 && private_releases==2);
        Check(!visual.Close(visuals).complete); DrainParent(); art.alive=false;
        Check(visual.Close(visuals).complete && slot.Close().complete && handles.empty());
    }
    {
        Reset(); framework::PrivateVisual visual(*TestModelServices(refs,lifetime));
        Check(visual.Prepare(objects,scene,arrays,visuals,"asset/player.prefab",framework::VisualRoot::UniqueAnimator).complete);
        framework::ArtSlot slot(visual,TestModelServices(refs,lifetime,{}, {},access,admission),{BeforePayload,FailedPayload,nullptr});
        model.current=&prefab; slot.Submit(objects,component); Unload(&model,true,&info); slot.OwnerGone();
        const auto reads=art_reads; slot.RequestUnload(); Check(reads==art_reads && payload_releases==3);
        Check(!visual.Close(visuals).complete); DrainParent(); art.alive=false;
        Check(visual.Close(visuals).complete && slot.Close().complete && handles.empty());
    }
    {
        Reset(); framework::PrivateVisual visual(*TestModelServices(refs,lifetime));
        Check(visual.Prepare(objects,scene,arrays,visuals,"asset/player.prefab",framework::VisualRoot::UniqueAnimator).complete);
        framework::ArtSlot slot(visual,TestModelServices(refs,lifetime,{}, {},access,admission),{});
        model.current=&prefab; skip_adoption=true; slot.Submit(objects,component); skip_adoption=false;
        Check(!slot.Status().adopted && slot.Status().supply.returned);
        model.current=&source; const auto retired=private_releases; slot.RequestUnload();
        Check(model.current==&source && private_releases==retired && !slot.Status().unload_attempted);
        // No guessed rollback while the partially submitted Art might be owned.
        Check(!visual.Close(visuals).complete && staging.destroy==0 && !slot.Close().complete);
        slot.OwnerGone(); Check(!visual.Close(visuals).complete && art.destroy==1 && staging.destroy==0);
        art.alive=false; Check(!visual.Close(visuals).complete); DrainParent();
        Check(visual.Close(visuals).complete && slot.Close().complete && handles.empty());
    }
    {
        Reset(); framework::PrivateVisual visual(*TestModelServices(refs,lifetime));
        Check(visual.Prepare(objects,scene,arrays,visuals,"asset/player.prefab",framework::VisualRoot::UniqueAnimator).complete);
        framework::ArtSlot slot(visual,TestModelServices(refs,lifetime,{}, {},access,admission),{});
        model.current=&prefab; slot.Submit(objects,component);
        model.current=&source; slot.Superseded();
        const auto reads=art_reads;
        slot.RequestUnload(); Check(!slot.Close().complete && slot.Status().binding_detached && art_reads==reads);
        Check(!visual.Close(visuals).complete && art.destroy==0); // replacement != death
        slot.OwnerGone(); Check(!visual.Close(visuals).complete && art.destroy==1);
        art.alive=false; DrainParent();
        Check(visual.Close(visuals).complete && slot.Close().complete && handles.empty());
    }
    api::RemoveModelReleaseHooksAtQuiescentPoint(); Reset();
    std::cout<<"Private visual source tests passed: inactive preparation, owned source lease, partial construction and delegated native retirement; no game proof.\n";
    std::cout<<"Private Art slot tests passed: previous/new Art release separation, adoption after faults, owned-only unload and no post-disposal reads.\n";
}
