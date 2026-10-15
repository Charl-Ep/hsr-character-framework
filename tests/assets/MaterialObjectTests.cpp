#include <hksr/framework/assets/MaterialObjects.hpp>
#include <Windows.h>
#include <map>
#include <stdexcept>
#include <iostream>

namespace {
using namespace hksr;
struct Material { bool alive=false; int copied=0, writes=0; } source;
int info, shader, sequence=0, texture_dim=2;
std::map<int,void*> roots;
bool fault_unrelated=false;
void Check(bool ok) { if(!ok) throw std::runtime_error("Material preparation mismatch"); }
int __fastcall Keep(void* object,int zero,int kind,void*) { Check(!zero && kind==2); roots[++sequence]=object; return sequence; }
void* __fastcall Target(int id,void*) { return roots.at(id); }
void __fastcall Free(int id,void*) { roots.erase(id); }
bool __fastcall Alive(Material* object,void*) { return object->alive; }
void __fastcall Destroy(Material* object,void*) { object->alive=false; }
void __fastcall Copy(Material* object,void* original,void*) { Check(original==&source && !roots.empty()); object->alive=true; ++object->copied; }
int __fastcall Id(int* name,void*) { return *name; }
void* __fastcall Shader(void*,void*) { return &shader; }
int __fastcall Count(void* object,void*) { Check(object==&shader); return 3; }
int __fastcall NameId(void* object,int index,void*) { Check(object==&shader); return index+10; }
int __fastcall Type(void* object,int index,void*) {
    Check(object==&shader);
    if(fault_unrelated && index==2) RaiseException(0xE0560001,0,0,nullptr);
    return index==1?2:4;
}
int __fastcall Dimension(void* object,int index,void*) { Check(object==&shader && index!=1); return index==0?2:4; }
int __fastcall TextureDimension(int* dimension,void*) { return *dimension; }
void __fastcall Texture(Material* object,int id,void* value,void*) { Check(id==10 && (value==&texture_dim || !value)); ++object->writes; }
void __fastcall Scale(Material* object,int id,api::Float2 value,void*) { Check(id==10 && value.x==2 && value.y==3); ++object->writes; }
void __fastcall Offset(Material* object,int id,api::Float2 value,void*) { Check(id==10 && value.x==0 && value.y==1); ++object->writes; }
template<class T> api::Method M(T function) { return {reinterpret_cast<void*>(function),&info}; }
}
void TestMaterialObjects() {
    api::OwnedUnityObjects owner({M(Alive),M(Destroy)},{M(Keep),M(Target),M(Free),{}, {}});
    api::MaterialAccess material({M(Copy),M(Id),{},M(Texture),M(Scale),M(Offset)});
    api::ShaderAccess shader_api({M(Shader),M(Count),M(NameId),M(Type),M(Dimension)});
    api::TextureMethods texture_methods; texture_methods.dimension=M(TextureDimension);
    api::TextureAccess textures(texture_methods);
    auto close=[&](api::UnityObjectId id) { owner.Close(id); Check(owner.Close(id).complete); owner.Forget(id); Check(roots.empty()); };
    int texture_name=10, scalar_name=11, cube_name=12, missing_name=99;
    framework::TextureBindingInput binding{&texture_name,&texture_dim,{2,3},{0,1}};
    Material good;
    const auto a=framework::PrepareMaterial(owner,material,shader_api,textures,&good,&source,{binding});
    Check(a.ready && a.shader.complete && good.copied==1 && good.writes==3 && source.writes==0); close(a.owned);
    Material no_inputs;
    const auto b=framework::PrepareMaterial(owner,material,api::ShaderAccess({}),api::TextureAccess({}),&no_inputs,&source,{});
    Check(b.ready && !b.shader.shader.call.returned && no_inputs.writes==0); close(b.owned);
    Material partial; fault_unrelated=true;
    const auto c=framework::PrepareMaterial(owner,material,shader_api,textures,&partial,&source,{binding}); fault_unrelated=false;
    Check(c.ready && !c.shader.complete && partial.writes==3); close(c.owned);
    Material bad_type; binding.managed_property_name=&scalar_name;
    const auto d=framework::PrepareMaterial(owner,material,shader_api,textures,&bad_type,&source,{binding});
    Check(!d.ready && bad_type.writes==0 && d.bindings[0].property_id.value==11); close(d.owned);
    Material wrong_dimension; binding.managed_property_name=&cube_name;
    const auto e=framework::PrepareMaterial(owner,material,shader_api,textures,&wrong_dimension,&source,{binding});
    Check(!e.ready && wrong_dimension.writes==0 && e.bindings[0].texture_dimension.value==2); close(e.owned);
    Material mixed; binding.managed_property_name=&missing_name;
    auto valid=binding; valid.managed_property_name=&texture_name;
    const auto f=framework::PrepareMaterial(owner,material,shader_api,textures,&mixed,&source,{binding,valid});
    Check(!f.ready && !f.bindings[0].ready && f.bindings[1].ready && mixed.writes==3); close(f.owned);
    Material clear; valid.texture=nullptr;
    const auto g=framework::PrepareMaterial(owner,material,shader_api,api::TextureAccess({}),&clear,&source,{valid});
    Check(g.ready && clear.writes==3 && !g.bindings[0].texture_dimension.read.returned); close(g.owned);
    bool source_rejected=false;
    try { framework::PrepareMaterial(owner,material,shader_api,textures,&source,&source,{}); }
    catch(const std::invalid_argument&) { source_rejected=true; }
    Check(source_rejected && roots.empty() && source.copied==0);
    std::cout<<"Material preparation passed: actual shader type/dimension, template isolation and independent binding results.\n";
}
