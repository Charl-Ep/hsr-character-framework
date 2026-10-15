#pragma once
#include "ModelAssets.hpp"
#include <hksr/unity/SceneObjects.hpp>

namespace hksr::framework {
struct ModelTargetReceipt {
    BindingResult result;
    void* art_transform=nullptr;
    std::vector<ModelRendererTarget> targets;
};
// Owns GC roots for a synchronous model-local discovery result. It does not
// own native Unity resources or a prefab lease. Source art must be either this
// instance's unmodified art or an independently retained asset, never a player.
// Keep both native hierarchies alive until Prepare has consumed these targets.
class ModelTargets {
public:
    explicit ModelTargets(const api::ModelServices& services):roots_(services.CreateRoots()){}
    ModelTargetReceipt Discover(api::ManagedObjects& objects,const api::SceneObjects& scene,
        const api::ManagedArrays& arrays,const api::RendererAccess& renderers,
        const ModelAssetInput& input,void* instance_art,void* source_art);
    BindingResult Close();
private:
    void* Keep(api::ObjectResult value);
    std::unique_ptr<api::ManagedRoots> roots_;
    std::vector<api::ManagedRootId> ids_;
    bool started_=false,closed_=false;
};
}
