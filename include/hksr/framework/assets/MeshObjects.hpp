#pragma once
#include "MeshAssets.hpp"
#include <hksr/unity/OwnedUnityObjects.hpp>
#include <hksr/unity/UnityAssetAccess.hpp>
#include <hksr/unity/MeshSkinning.hpp>

namespace hksr::framework {
struct MeshChannelWrite { api::MeshAttribute attribute; api::CallResult result; };
struct MeshPreparation {
    api::UnityObjectId owned=0;
    api::ObjectResult clone;
    api::CallResult initialize, index_format, submeshes, bounds, upload;
    std::vector<MeshChannelWrite> channels;
    std::vector<api::CallResult> triangles;
    api::MeshSkinningResult skinning;
    api::Observed<std::int32_t> source_vertices, source_shapes, result_shapes;
    bool ready=false;
    std::string error;
};
// Consumes a fresh AllocateObject(Mesh). No blendshape inheritance. Adoption
// happens before initialization, including invalid input/partial failure.
MeshPreparation PrepareFreshMesh(api::OwnedUnityObjects& owner,const api::MeshAccess& mesh,
    const api::MeshSkinning& skinning,void* allocated_mesh,const MeshDescription& description,const MeshData& data);
// matching_template is an AUTHOR contract: vertex i corresponds to template
// vertex i. Count agreement is only a necessary check, NOT topology proof.
// The independently loaded template stays unchanged/unowned; only its new
// clone is adopted. Never pass a player's mesh. No Renderer is modified here.
MeshPreparation PrepareTemplateMesh(api::OwnedUnityObjects& owner,const api::MeshAccess& mesh,
    const api::MeshSkinning& skinning,const api::MeshTemplateAccess& templates,void* rooted_template,
    const MeshDescription& description,const MeshData& data);
}
