#pragma once
#include <hksr/entity/NpcCreationSession.hpp>
#include "AppearanceModel.hpp"
#include <memory>

namespace hksr::framework {
struct AppearanceServices {
    ModelAssetServices assets;
    const api::SceneObjects& scene;
    const api::VisualObjects& visuals;
    const api::AdventureConfig& config;
    const api::ComponentConfig& components;
};
struct NpcAppearanceStatus {
    api::ObjectResult component;
    api::ModelSnapshot model;
    std::uint64_t incarnations=0;
    std::size_t retiring=0;
    bool enabled=true,closing=false,closed=false;
    std::vector<std::string> errors;
    std::string look_source;
};
// Private Player Art on one owned NPC. Mutually exclusive with NpcModels on
// that model. Source descriptions are immutable; engine state stays typed.
// Failed construction is not retried each frame. A new observed Art/model or
// new NPC epoch is a new attempt. No destructor calls into the engine.
class NpcAppearance {
public:
    NpcAppearance(api::NpcCreationSession& npc,api::GameWorld* world,Appearance source,
        std::shared_ptr<const ModelAssetInput> payload,VisualRoot root,std::string look_source,
        std::shared_ptr<const api::ModelServices> services);
    void Service(const api::WorldFrame& frame,AppearanceServices services);
    void SetEnabled(bool value) noexcept { status_.enabled=value; }
    void RequestClose() noexcept { status_.closing=true; }
    // Payload references and model notification slot have detached. Native
    // objects / source leases / GC may remain pending. Does not mean Art dead.
    bool InputsDetached() const;
    const NpcAppearanceStatus& Status() const { return status_; }
    const AppearanceModel* Active() const { return active_.get(); }
    const AppearanceModel* Retiring(std::size_t i) const { return i<retired_.size()?retired_[i].get():nullptr; }
private:
    void Retire(bool owner_gone,bool superseded);
    void Drain(bool own_frame,bool owner_gone,const api::VisualObjects& visuals);
    api::NpcCreationSession& npc_;
    api::GameWorld* world_;
    Appearance source_;
    std::shared_ptr<const ModelAssetInput> payload_;
    VisualRoot root_;
    std::string look_source_;
    std::shared_ptr<const api::ModelServices> services_;
    const api::EntityAccess& entities_;
    const api::ModelAccess& models_;
    const api::ArtAdmission& admission_;
    std::unique_ptr<api::ManagedRoots> roots_;
    api::ManagedRootId type_=0;
    std::vector<api::ManagedRootId> path_roots_;
    bool type_attempted_=false;
    api::CharacterModelComponent* attempted_model_=nullptr;
    void* attempted_art_=nullptr;
    std::unique_ptr<AppearanceModel> active_;
    std::vector<std::unique_ptr<AppearanceModel>> retired_;
    NpcAppearanceStatus status_;
};
}
