#pragma once
#include "ArtSlot.hpp"
#include "AppearanceInputs.hpp"
#include "../assets/ModelTargets.hpp"
#include <hksr/control/LookSupport.hpp>

namespace hksr::framework {
struct AppearanceModelStatus {
    ModelTargetReceipt discovery;
    ModelAssetReceipt resources;
    BindingResult publication,restoration,cleanup;
    WeaponDefaultsResult weapons;
    std::vector<std::string> errors;
    bool started=false,prepared=false,has_payload=false;
    bool payload_sealed=false,payload_detached=false,closing=false,closed=false;
};
// One private visual incarnation, not an entity or an animation controller.
// Prepare -> optional PrepareLookSupport -> Submit. Submit polls partial-Add
// rollback before admission; it does not make a failed optional feature a mesh
// failure. Event supplementation is owned at NPC/consumer, not Art, lifetime.
// The source hierarchy and private payload share one explicit retirement owner.
// Use instead of ModelEpoch on this model; all methods are engine-thread-only.
class AppearanceModel {
public:
    explicit AppearanceModel(std::shared_ptr<const api::ModelServices> services);
    AppearanceModel(const AppearanceModel&)=delete;
    AppearanceModel& operator=(const AppearanceModel&)=delete;
    void Prepare(ModelAssetServices services,const api::SceneObjects& scene,
        const api::VisualObjects& visuals,const api::AdventureConfig& config,
        const Appearance& source,VisualRoot root,const ModelAssetInput* payload);
    void PrepareLookSupport(api::LookSupportServices services,const std::string& npc_prefab_path);
    void Submit(api::ManagedObjects& objects,api::CharacterModelComponent* model);
    void OwnerGone(); // actual model/entity lifetime ended, not a failed request
    void Superseded(); // replacement only: no permission to destroy adopted Art
    BindingResult Close(const api::VisualObjects& visuals);
    const AppearanceModelStatus& Status() const { return status_; }
    const PrivateVisualStatus& Visual() const { return visual_.Status(); }
    const ArtSlotStatus& Slot() const { return slot_.Status(); }
    const AppearanceInputs& Inputs() const { return inputs_; }
    const api::LookSupportStatus& Look() const { return look_->Status(); }
private:
    static void Before(const api::ModelReleaseEvent&,void*);
    static void Failure(const api::ModelReleaseEvent&,const char*,void*) noexcept;
    void Seal() noexcept;
    std::shared_ptr<const api::ModelServices> services_;
    const api::ArtAdmission& admission_;
    const api::RendererAccess& renderers_;
    PrivateVisual visual_;
    ModelTargets targets_;
    ModelAssets assets_;
    AppearanceInputs inputs_;
    std::unique_ptr<api::LookSupport> look_;
    ArtSlot slot_; // callback dependencies above outlive slot_
    AppearanceModelStatus status_;
};
}
