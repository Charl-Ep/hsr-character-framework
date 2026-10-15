#pragma once
#include <hksr/entity/NpcCreationSession.hpp>
#include "ModelEpoch.hpp"
#include <memory>

namespace hksr::framework {
struct NpcModelStatus {
    api::ObjectResult component;
    api::ModelSnapshot model;
    std::uint64_t incarnations=0;
    std::size_t retiring=0;
    bool enabled=true,closing=false,closed=false;
    std::vector<std::string> errors;
};
// External mesh overlay consumer for an already-owned api::NpcCreationSession. Does not
// change its prefab, controller, events, position, rootBone or official state.
// The actor service must service this consumer BEFORE submitting NPC removal;
// natural release is handled synchronously by ModelEpoch's SDK registration.
// All providers and the NPC owner outlive this service. Input is immutable and
// loaded before engine work. Service only on the actual engine frame thread.
class NpcModels {
public:
    NpcModels(api::NpcCreationSession& npc,api::GameWorld* owner_world,std::shared_ptr<const ModelAssetInput> input,
        std::shared_ptr<const api::ModelServices> services);
    void Service(const api::WorldFrame& frame,ModelAssetServices assets,const api::SceneObjects& scene);
    void SetEnabled(bool enabled) noexcept { status_.enabled=enabled; }
    bool InputsDetached() const;
    void RequestClose() noexcept { status_.closing=true; }
    const NpcModelStatus& Status() const { return status_; }
    const ModelEpochStatus* Active() const { return active_?&active_->Status():nullptr; }
    const ModelEpochStatus* Retiring(std::size_t index) const { return index<retired_.size()?&retired_[index]->Status():nullptr; }
private:
    void Retire(bool owner_gone);
    void Drain(bool allow_live_restore);
    api::NpcCreationSession& npc_;
    api::GameWorld* world_;
    std::shared_ptr<const ModelAssetInput> input_;
    std::shared_ptr<const api::ModelServices> services_;
    const api::EntityAccess& entities_;
    const api::ModelAccess& models_;
    std::unique_ptr<api::ManagedRoots> roots_;
    api::ManagedRootId model_type_=0;
    bool type_attempted_=false;
    api::CharacterModelComponent* current_model_=nullptr;
    void* current_art_=nullptr;
    std::unique_ptr<ModelEpoch> active_;
    std::vector<std::unique_ptr<ModelEpoch>> retired_;
    NpcModelStatus status_;
};
}
