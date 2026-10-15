#include <hksr/framework/assets/MeshObjects.hpp>
#include <hksr/framework/assets/CharacterPackage.hpp>
#include <hksr/framework/assets/ModelPayload.hpp>
#include <Windows.h>
#include <map>
#include <cstring>
#include <fstream>
#include <iterator>
#include <iostream>
#include <stdexcept>

namespace {
using namespace hksr;
struct Mesh {
    bool alive=false;
    int vertices=0,shapes=0,channels=0,triangles=0,initialized=0,uploaded=0;
};
Mesh source,copy;
int info,next=0,fail_attribute=-1;
bool fail_ctor=false,weights_reject=false,clone_alias=false,drop_shapes=false;
std::map<int,void*> roots;
const framework::MeshData* expected=nullptr;
const framework::MeshDescription* description=nullptr;
void Check(bool ok) { if(!ok) throw std::runtime_error("mesh preparation mismatch"); }
void Info(void* p) { Check(p==&info); }
template<class T> api::Method M(T f) { return {reinterpret_cast<void*>(f),&info}; }
int __fastcall Keep(void* p,int zero,int kind,void*) { Check(!zero && kind==2); roots[++next]=p; return next; }
void* __fastcall Target(int id,void*) { return roots.at(id); }
void __fastcall Free(int id,void*) { roots.erase(id); }
bool __fastcall Alive(Mesh* p,void*) { return p->alive; }
void __fastcall Destroy(Mesh* p,void*) { Check(p!=&source); p->alive=false; }
void __fastcall Create(Mesh* p,void* meta) {
    Info(meta); Check(p!=&source && !roots.empty()); p->alive=true; ++p->initialized;
    if(fail_ctor) RaiseException(0xE0580001,0,0,nullptr);
}
void __fastcall Format(Mesh* p,api::MeshIndex f,void* meta) { Info(meta); Check(p!=&source && f==api::MeshIndex::UInt16); }
void __fastcall Channel(Mesh* p,api::MeshAttribute attribute,api::MeshElement format,int dim,const void* data,int length,int first,int count,void* meta) {
    Info(meta); Check(p!=&source && first==0 && count==length && count==static_cast<int>(expected->positions.size()));
    const void* values=nullptr; std::size_t stride=0;
    using A=api::MeshAttribute;
    switch(attribute) {
    case A::Position: values=expected->positions.data(); stride=12; p->vertices=count; break;
    case A::Normal: values=expected->normals.data(); stride=12; break;
    case A::Tangent: values=expected->tangents.data(); stride=16; break;
    case A::Color: values=expected->colors.data(); stride=4; break;
    case A::UV0: values=expected->uv0.data(); stride=8; break;
    case A::UV1: values=expected->uv1.data(); stride=8; break;
    }
    Check(values && !std::memcmp(values,data,stride*count));
    Check(attribute==A::Color ? format==api::MeshElement::UNorm8 && dim==4 : format==api::MeshElement::Float32 && dim==stride/4);
    ++p->channels;
    if(static_cast<int>(attribute)==fail_attribute) RaiseException(0xE0580002,0,0,nullptr);
}
void __fastcall Submeshes(Mesh*,int count,void* meta) { Info(meta); Check(count==static_cast<int>(description->submeshes.size())); }
void __fastcall Triangles(Mesh* p,int submesh,int topology,api::MeshIndex format,const void* data,int first,int count,bool recalc,int base,void* meta) {
    Info(meta); const auto range=description->submeshes.at(submesh);
    Check(topology==0 && format==api::MeshIndex::UInt16 && !recalc && base==0 && first==static_cast<int>(range.first_index) && count==static_cast<int>(range.index_count));
    Check(!std::memcmp(data,expected->indices.data(),expected->indices.size()*2)); ++p->triangles;
}
void __fastcall Bounds(Mesh*,void* meta) { Info(meta); }
void __fastcall Upload(Mesh* p,bool discard,void* meta) { Info(meta); Check(!discard); ++p->uploaded; if(drop_shapes) p->shapes=0; }
void* __fastcall Cached(Mesh* p,void* meta) { Info(meta); return p; }
bool __fastcall Skin(Mesh* p,const api::FixedBoneWeights* data,int count) {
    Check(count==p->vertices);
    for(int i=0;i<count;++i) Check(data[i].weights==expected->bone_weights[i].weights && data[i].indices==expected->bone_weights[i].indices);
    return !weights_reject;
}
void __fastcall Matrices(Mesh*,const api::BindposeMatrix* data,int count) {
    Check(count==static_cast<int>(expected->bindposes.size()));
    for(int i=0;i<count;++i) Check(data[i]==expected->bindposes[i]);
}
void* __fastcall Clone(Mesh* p,void* meta) { Info(meta); Check(p==&source); copy=*p; return clone_alias?&source:&copy; }
int __fastcall Vertices(Mesh* p,void* meta) { Info(meta); return p->vertices; }
int __fastcall Shapes(Mesh* p,void* meta) { Info(meta); return p->shapes; }
std::string Read(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary); Check(bool(in)); return {(std::istreambuf_iterator<char>(in)),{}};
}
}
void TestMeshObjects(int argc,char** argv) {
    api::OwnedUnityObjects owner({M(Alive),M(Destroy)},{M(Keep),M(Target),M(Free),{}, {}});
    api::MeshAccess mesh({M(Create),M(Format),M(Channel),M(Triangles),M(Submeshes),M(Bounds),M(Upload)});
    api::MeshSkinning skin({M(Cached),reinterpret_cast<void*>(Skin),reinterpret_cast<void*>(Matrices)});
    api::MeshTemplateAccess templates({M(Clone),M(Vertices),M(Shapes)});
    const auto close=[&](api::UnityObjectId id) { owner.Close(id); Check(owner.Close(id).complete); owner.Forget(id); Check(roots.empty()); };
    framework::MeshDescription d; d.vertex_count=3; d.index_count=3; d.submeshes={{0,3}}; d.bone_paths={"Root"};
    framework::MeshData data; data.positions={{{0,0,0}},{{1,0,0}},{{0,1,0}}}; data.normals.resize(3); data.tangents.resize(3);
    data.colors.resize(3); data.uv0.resize(3); data.bone_weights.resize(3); data.indices={0,1,2}; data.bindposes.resize(1);
    for(auto& v:data.bone_weights) v.weights={1,0,0,0};
    expected=&data; description=&d;
    Mesh fresh;
    auto r=framework::PrepareFreshMesh(owner,mesh,skin,&fresh,d,data);
    Check(r.ready && fresh.channels==5 && fresh.triangles==1 && fresh.initialized==1 && fresh.uploaded==1); close(r.owned);
    Mesh partial; fail_ctor=true; r=framework::PrepareFreshMesh(owner,mesh,skin,&partial,d,data); fail_ctor=false;
    Check(!r.ready && r.initialize.exception==0xE0580001 && partial.alive && !partial.channels); close(r.owned);
    Mesh channel; fail_attribute=1; r=framework::PrepareFreshMesh(owner,mesh,skin,&channel,d,data); fail_attribute=-1;
    Check(!r.ready && channel.channels==5 && channel.triangles==1 && r.skinning.Returned() && !channel.uploaded); close(r.owned);
    Mesh rejected; weights_reject=true; r=framework::PrepareFreshMesh(owner,mesh,skin,&rejected,d,data); weights_reject=false;
    Check(!r.ready && r.skinning.weights.read.returned && !r.skinning.weights.value && !r.skinning.bindposes.returned); close(r.owned);
    Mesh invalid; data.indices[2]=3; r=framework::PrepareFreshMesh(owner,mesh,skin,&invalid,d,data); data.indices[2]=2;
    Check(!r.ready && !invalid.initialized); close(r.owned);
    source={true,3,2};
    r=framework::PrepareTemplateMesh(owner,mesh,skin,templates,&source,d,data);
    Check(r.ready && r.clone.object==&copy && copy.shapes==2 && !copy.initialized && source.channels==0 && source.alive); close(r.owned);
    source.vertices=4; r=framework::PrepareTemplateMesh(owner,mesh,skin,templates,&source,d,data);
    Check(!r.ready && !r.owned && !r.clone.call.returned && roots.empty()); source.vertices=3;
    clone_alias=true; r=framework::PrepareTemplateMesh(owner,mesh,skin,templates,&source,d,data); clone_alias=false;
    Check(!r.ready && !r.owned && roots.empty() && source.alive);
    drop_shapes=true; r=framework::PrepareTemplateMesh(owner,mesh,skin,templates,&source,d,data); drop_shapes=false;
    Check(!r.ready && r.source_shapes.value==2 && r.result_shapes.value==0); close(r.owned);
    // Readback is diagnostic; an absent shape getter is not an upload gate.
    api::MeshTemplateAccess no_shapes({M(Clone),M(Vertices),{}});
    r=framework::PrepareTemplateMesh(owner,mesh,skin,no_shapes,&source,d,data);
    Check(r.ready && !r.source_shapes.read.returned && !r.result_shapes.read.returned); close(r.owned);
    unsigned loaded=0;
    for(int i=1;i<argc;++i) {
        const auto file=std::filesystem::u8path(argv[i]); const auto package=framework::ParseCharacterPackage(Read(file));
        if(package.source!=framework::SourceKind::External) continue;
        const auto bundle=framework::LoadModelPayload(file.parent_path(),package);
        Check(bundle.texture_payloads.size()==bundle.materials.textures.size());
        for(std::size_t role=0;role<bundle.manifest.meshes.size();++role) {
            const auto& entry=bundle.manifest.meshes[role]; const auto& payload=bundle.meshes[role]; expected=&payload; description=&entry;
            Mesh target;
            if(package.appearance->shapes==framework::ShapeData::None)
                r=framework::PrepareFreshMesh(owner,mesh,skin,&target,entry,payload);
            else {
                // Synthetic template for consumer test, NOT compatibility
                // evidence for the actual game's template or blendshape bytes.
                source={true,static_cast<int>(entry.vertex_count),2};
                r=framework::PrepareTemplateMesh(owner,mesh,skin,templates,&source,entry,payload);
            }
            Check(r.ready && r.channels.size()==(entry.uv1?6u:5u) && r.triangles.size()==entry.submeshes.size()); close(r.owned); ++loaded;
        }
    }
    std::cout<<"Mesh preparation passed: fresh/private clone, all channels, skinning and failures; "<<loaded<<" real payloads, synthetic engine only.\n";
}
