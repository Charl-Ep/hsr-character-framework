#pragma once
#include "CharacterPackage.hpp"
#include "ModelPayload.hpp"
#include "MeshObjects.hpp"
#include "MaterialObjects.hpp"
#include "TextureObjects.hpp"
#include "ReferenceArrays.hpp"
#include "RendererBindings.hpp"
#include <hksr/managed/ManagedObjects.hpp>

namespace hksr::framework {
// Resolved once by the host. No addresses, class offsets or character-specific
// rules are embedded in this consumer. Services outlive synchronous Prepare.
struct ModelAssetServices {
    api::ManagedObjects& objects;
    const api::MeshAccess& mesh;
    const api::MeshSkinning& skinning;
    const api::MeshTemplateAccess& mesh_templates;
    const api::TextureAccess& texture;
    const api::MaterialAccess& material;
    const api::ShaderAccess& shader;
    const api::ManagedArrays& arrays;
    const api::HierarchyAccess& hierarchy;
};
struct ModelRendererTarget {
    std::string role;
    void* renderer=nullptr; // this model epoch, never the player's renderer
    void* template_mesh=nullptr;
    void* template_materials=nullptr; // independent source Material[]
};
struct ModelAssetReceipt {
    std::vector<MeshPreparation> meshes;
    std::vector<TexturePreparation> textures;
    std::vector<MaterialPreparation> materials;
    std::vector<BonePalettePreparation> palettes;
    std::vector<ReferenceArrayPreparation> material_arrays;
    BindingResult result;
};
// One model epoch's private resources. Prepare constructs the full payload but
// does not change renderers. Apply publishes once; BeforeModelRelease restores
// all references before pooling; Close then drains native destruction and GC.
// Never discard this owner on a partial failure. No engine calls in destructor.
class ModelAssets {
public:
    explicit ModelAssets(std::shared_ptr<const api::ModelServices> services);
    ModelAssets(const ModelAssets&)=delete;
    ModelAssets& operator=(const ModelAssets&)=delete;
    ModelAssetReceipt Prepare(ModelAssetServices services,const ModelAssetInput& input,
        void* rooted_instance_art_transform,const std::vector<ModelRendererTarget>& rooted_targets);
    BindingResult Apply();
    BindingResult BeforeModelRelease();
    BindingResult Detach(); // restore renderer references; keep GC/owned assets
    void SealWithoutRestore() noexcept;
    BindingResult ObserveReleasedRenderers();
    BindingResult Close();
private:
    void* Keep(api::ObjectResult object);
    void* String(api::ManagedObjects& source,const std::string& text);
    std::unique_ptr<api::ManagedRoots> roots_;
    std::unique_ptr<api::OwnedUnityObjects> native_;
    RendererBindings bindings_;
    std::vector<api::ManagedRootId> root_ids_;
    bool started_=false,prepared_=false,closing_=false;
};
}
