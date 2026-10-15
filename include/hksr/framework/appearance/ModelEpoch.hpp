#pragma once
#include "../assets/ModelTargets.hpp"
#include <hksr/model/ModelRelease.hpp>

namespace hksr::framework {
struct ModelEpochStatus {
    api::ModelBindingToken binding;
    ModelTargetReceipt discovery;
    ModelAssetReceipt preparation;
    BindingResult publication,restoration,cleanup;
    std::vector<std::string> errors;
    bool started=false,published=false,release_seen=false,detached=false,closed=false;
};
// One official model incarnation's external override. Registers restoration
// before publication, never takes ownership of the official root/Art, and
// retires its registration once detached (private destruction may finish later).
// Engine-thread/quiescent operations only; owner must outlive Close completion
// and any in-flight model release delivery. Destructor performs no engine work.
class ModelEpoch {
public:
    explicit ModelEpoch(std::shared_ptr<const api::ModelServices> services);
    ModelEpoch(const ModelEpoch&)=delete;
    ModelEpoch& operator=(const ModelEpoch&)=delete;
    void PrepareAndApply(api::CharacterModelComponent* model,void* instance_art,void* source_art,
        ModelAssetServices services,const api::SceneObjects& scene,const ModelAssetInput& input);
    BindingResult Close();
    // Owner disappeared without a pre-release delivery. Record the missing
    // boundary, seal renderer writes, retain unresolved resources explicitly.
    void OwnerGone();
    const ModelEpochStatus& Status() const { return status_; }
private:
    static void Before(const api::ModelReleaseEvent& event,void* self);
    static void Failure(const api::ModelReleaseEvent& event,const char* error,void* self) noexcept;
    std::shared_ptr<const api::ModelServices> services_;
    std::unique_ptr<api::ManagedRoots> roots_;
    api::ManagedRootId model_root_=0;
    const api::RendererAccess& renderers_;
    ModelTargets targets_;
    ModelAssets assets_;
    ModelEpochStatus status_;
};
}
