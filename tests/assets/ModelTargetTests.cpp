#include "../support/ModelServicesFixture.hpp"
#include <hksr/framework/assets/ModelTargets.hpp>
#include <Windows.h>
#include <map>
#include <memory>
#include <stdexcept>
#include <iostream>
namespace {
using namespace hksr;
struct Node { std::string name; std::vector<void*> children; void* mesh=nullptr; void* materials=nullptr; };
Node art,source,body,hair,source_body,source_hair,mesh,materials,type,transform;
std::vector<std::unique_ptr<std::string>> strings;
std::map<int,void*> handles;
int next=0,info,enumerations=0,mesh_reads=0;
bool compare_fault=false;
void Check(bool ok) { if(!ok) throw std::runtime_error("model target discovery mismatch"); }
template<class T> api::Method M(T f) { return {reinterpret_cast<void*>(f),&info}; }
void* String(const char* text) { auto value=std::make_unique<std::string>(text); auto* p=value.get(); strings.push_back(std::move(value)); return p; }
struct Objects:api::ManagedObjects {
    api::ObjectResult NewStringUtf8(const char* text) override { return {{true,0},String(text)}; }
    api::ObjectResult AllocateObject(const char*,const char*) override { throw std::runtime_error("discovery must not allocate native objects"); }
    api::ObjectResult ManagedType(const char* space,const char* name) override {
        Check(std::string(space)=="UnityEngine" && std::string(name)=="SkinnedMeshRenderer"); return {{true,0},&type};
    }
};
int __fastcall Retain(void* p,int,int,void*) { handles[++next]=p; return next; }
void* __fastcall Target(int id,void*) { return handles.at(id); }
void __fastcall Free(int id,void*) { handles.erase(id); }
void* __fastcall Transform(Node* p,void* metadata) { Check(p==&art && metadata==&info); return &transform; }
void* __fastcall Components(Node* p,void* requested,bool inactive,void* metadata) {
    Check((p==&art || p==&source) && requested==&type && inactive && metadata==&info); ++enumerations; return p;
}
void* __fastcall Name(Node* p,void* metadata) { Check(metadata==&info); return String(p->name.c_str()); }
bool __fastcall Equal(std::string* a,std::string* b,void* metadata) {
    Check(metadata==&info);
    if(compare_fault) RaiseException(0xE0610001,0,0,nullptr);
    return *a==*b;
}
int __fastcall Length(Node* p,void*) { return static_cast<int>(p->children.size()); }
void* __fastcall Get(Node* p,int i,void*) { return p->children.at(i); }
void* __fastcall Mesh(Node* p,void*) { ++mesh_reads; return p->mesh; }
void* __fastcall Materials(Node* p,void*) { return p->materials; }
struct Resolver:api::MethodResolver {
    std::vector<std::string> calls;
    bool missing_string=false;
    api::Method Resolve(const char* space,const char* owner,const char* name,const char* result,
        std::initializer_list<const char*> args,api::MethodKind kind) override {
        calls.push_back(std::string(owner)+"."+name);
        const std::string method=name;
        if(method=="get_transform") { Check(std::string(owner)=="GameObject" && args.size()==0); return M(Transform); }
        if(method=="GetComponentsInChildren") { Check(std::string(result)=="UnityEngine.Component[]" && args.size()==2); return M(Components); }
        if(method=="get_name") return M(Name);
        if(method=="Equals") {
            Check(std::string(space)=="System" && std::string(owner)=="String" && args.size()==2 && kind==api::MethodKind::Static);
            if(missing_string) throw std::runtime_error("current domain did not provide requested overload");
            return M(Equal);
        }
        throw std::runtime_error("unexpected method");
    }
};
}
void TestModelTargets() {
    // These are API-call substitutes, NOT an observed System.String declaration.
    Resolver resolver; const auto methods=api::ResolveSceneObjectMethods(resolver); Check(methods.binding_errors.empty() && resolver.calls.size()==4);
    resolver.missing_string=true; const auto missing=api::ResolveSceneObjectMethods(resolver);
    Check(missing.binding_errors.size()==1 && !missing.string_equals.code && missing.components.code);
    Objects objects; api::SceneObjects scene(methods); api::ManagedArrays arrays({{},M(Length),M(Get),{}});
    api::RendererAccess renderers({M(Mesh),{},M(Materials),{},{},{}});
    api::ReferenceMethods refs{M(Retain),M(Target),M(Free),{}, {}};
    body.name="身体"; hair.name=std::string(300,'h'); source_body.name=body.name; source_hair.name=hair.name;
    source_body.mesh=source_hair.mesh=&mesh; source_body.materials=source_hair.materials=&materials;
    body.materials=hair.materials=&materials;
    art.children={&hair,&body,nullptr}; source.children={&source_body,&source_hair};
    framework::ModelAssetInput input; framework::MeshDescription a,b; a.renderer=body.name; b.renderer=hair.name; input.manifest.meshes={a,b};
    input.shapes=framework::ShapeData::MatchingTemplate;
    {
        framework::ModelTargets target(*TestModelServices(refs));
        auto found=target.Discover(objects,scene,arrays,renderers,input,&art,&source);
        Check(found.result.complete && found.targets.size()==2 && found.art_transform==&transform && enumerations==2);
        Check(found.targets[0].renderer==&body && found.targets[0].template_mesh==&mesh && found.targets[1].renderer==&hair);
        Check(!handles.empty() && target.Close().complete && handles.empty());
    }
    {
        framework::ModelTargets target(*TestModelServices(refs)); auto fresh=input; fresh.shapes=framework::ShapeData::None;
        const auto before=mesh_reads,scans=enumerations;
        Check(target.Discover(objects,scene,arrays,renderers,fresh,&art,&art).result.complete);
        Check(mesh_reads==before && enumerations==scans+1 && target.Close().complete && handles.empty());
    }
    {
        framework::ModelTargets target(*TestModelServices(refs)); Node duplicate=body; art.children.push_back(&duplicate); source.children.pop_back();
        const auto failed=target.Discover(objects,scene,arrays,renderers,input,&art,&source);
        Check(!failed.result.complete && failed.targets.empty() && failed.result.errors.size()==2);
        Check(target.Close().complete && handles.empty()); art.children.pop_back(); source.children.push_back(&source_hair);
    }
    {
        framework::ModelTargets target(*TestModelServices(refs)); compare_fault=true;
        const auto failed=target.Discover(objects,scene,arrays,renderers,input,&art,&source); compare_fault=false;
        Check(!failed.result.complete && failed.targets.empty() && failed.result.errors.size()>=8 && target.Close().complete && handles.empty());
    }
    {
        framework::ModelTargets target(*TestModelServices(refs)); Check(target.Close().complete);
        Check(!target.Discover(objects,scene,arrays,renderers,input,&art,&source).result.complete && handles.empty());
    }
    std::cout<<"Model-local target discovery passed: full-name matching, inactive nodes, independent sources and no fresh-mesh getter dependency.\n";
}
