#include <hksr/framework/assets/MeshObjects.hpp>
#include <stdexcept>

namespace hksr::framework {
namespace {
bool Validate(MeshPreparation& r,const MeshDescription& d,const MeshData& data) {
    try { ValidateMeshData(d,data); return true; }
    catch(const std::invalid_argument& e) { r.error=e.what(); return false; }
}
void Write(MeshPreparation& r,const api::MeshAccess& mesh,const api::MeshSkinning& skinning,
    void* object,const MeshDescription& d,const MeshData& data) {
    using A=api::MeshAttribute; using E=api::MeshElement; using I=api::MeshIndex;
    const auto count=static_cast<std::int32_t>(d.vertex_count);
    const auto channel=[&](A a,E format,int dimension,const void* bytes) {
        const auto result=mesh.Channel(object,a,format,dimension,bytes,count);
        r.channels.push_back({a,result}); return result.returned;
    };
    // Position establishes vertex count: all later channels and skinning
    // depend on it. Other channels are independent and report separately.
    if(!channel(A::Position,E::Float32,3,data.positions.data())) { r.error="mesh position upload failed"; return; }
    bool channels=true;
    channels=channel(A::Normal,E::Float32,3,data.normals.data()) && channels;
    channels=channel(A::Tangent,E::Float32,4,data.tangents.data()) && channels;
    channels=channel(A::Color,E::UNorm8,4,data.colors.data()) && channels;
    channels=channel(A::UV0,E::Float32,2,data.uv0.data()) && channels;
    if(d.uv1) channels=channel(A::UV1,E::Float32,2,data.uv1.data()) && channels;
    r.index_format=mesh.IndexFormat(object,I::UInt16);
    r.submeshes=mesh.Submeshes(object,static_cast<int>(d.submeshes.size()));
    bool indices=r.index_format.returned && r.submeshes.returned;
    if(indices) for(std::size_t i=0;i<d.submeshes.size();++i) {
        const auto range=d.submeshes[i];
        const auto result=mesh.Triangles(object,static_cast<int>(i),I::UInt16,data.indices.data(),
            static_cast<int>(range.first_index),static_cast<int>(range.index_count));
        r.triangles.push_back(result); indices=result.returned && indices;
    }
    // Explicit value conversion: identical byte layouts do not permit C++
    // aliasing of unrelated vector element types.
    std::vector<api::FixedBoneWeights> weights; weights.reserve(data.bone_weights.size());
    for(const auto& v:data.bone_weights) weights.push_back({v.weights,v.indices});
    r.skinning=skinning.Write(object,weights.data(),count,data.bindposes.data(),static_cast<int>(data.bindposes.size()));
    if(!channels || !indices || !r.skinning.Returned()) { r.error="mesh channel/index/skinning write failed; private object retained"; return; }
    r.bounds=mesh.RecalculateBounds(object);
    if(!r.bounds.returned) { r.error="mesh bounds update failed"; return; }
    // Keep the CPU copy for later official readers; do not change readability
    // as a side effect of applying a character package.
    r.upload=mesh.Upload(object,false); r.ready=r.upload.returned;
    if(!r.ready) r.error="mesh upload failed";
}
}
MeshPreparation PrepareFreshMesh(api::OwnedUnityObjects& owner,const api::MeshAccess& mesh,
    const api::MeshSkinning& skinning,void* object,const MeshDescription& d,const MeshData& data) {
    MeshPreparation r; r.owned=owner.Adopt(object);
    if(!owner.Status(r.owned).root.ready) { r.error="mesh retention failed"; return r; }
    if(!Validate(r,d,data)) return r;
    r.initialize=mesh.Initialize(object);
    if(!r.initialize.returned) { r.error="mesh initialization failed; partial object retained"; return r; }
    Write(r,mesh,skinning,object,d,data); return r;
}
MeshPreparation PrepareTemplateMesh(api::OwnedUnityObjects& owner,const api::MeshAccess& mesh,
    const api::MeshSkinning& skinning,const api::MeshTemplateAccess& templates,void* source,
    const MeshDescription& d,const MeshData& data) {
    MeshPreparation r;
    if(!source) { r.error="missing independent template mesh"; return r; }
    if(!Validate(r,d,data)) return r;
    r.source_vertices=templates.VertexCount(source);
    if(!r.source_vertices.read.returned || r.source_vertices.value!=static_cast<int>(d.vertex_count)) {
        r.error="matching_template vertex count missing/mismatched; vertex correspondence cannot be inferred"; return r;
    }
    r.source_shapes=templates.ShapeCount(source);
    r.clone=templates.Clone(source);
    if(!r.clone.call.returned || !r.clone.object) { r.error="mesh clone did not return an object"; return r; }
    if(r.clone.object==source) { r.error="mesh clone returned its shared source; not adopted"; return r; }
    r.owned=owner.Adopt(r.clone.object);
    if(!owner.Status(r.owned).root.ready) { r.error="cloned mesh retention failed"; return r; }
    // Do not initialize or clear a clone: this would discard inherited shapes.
    Write(r,mesh,skinning,r.clone.object,d,data);
    r.result_shapes=templates.ShapeCount(r.clone.object);
    if(r.source_shapes.read.returned && r.result_shapes.read.returned && r.source_shapes.value!=r.result_shapes.value) {
        r.ready=false; r.error="mesh shape count changed during writes";
    }
    return r;
}
}
