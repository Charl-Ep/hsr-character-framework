#pragma once
#include "PrivateVisual.hpp"
#include <hksr/model/ModelRelease.hpp>

namespace hksr::framework {
struct ArtSlotStatus {
    api::ModelBindingToken binding;
    api::ObjectResult logical_path;
    api::Observed<void*> current_art;
    api::CallResult supply,unload;
    BindingResult cleanup;
    std::vector<std::string> errors;
    std::size_t previous_art_releases=0;
    bool started=false,supply_attempted=false,adopted=false,release_seen=false;
    bool unload_attempted=false,owner_gone=false,closed=false;
    bool superseded=false,binding_detached=false;
};
// Binds ONE prepared private visual to the owning NPC's logical Art slot.
// Does not create an entity or decide appearance/config/events. The consumer
// must finish payload/support preparation before Submit. Never share a model
// registration with ModelEpoch: choose private Art OR existing-model overlay.
// The release sink restores payload references; it and visual outlive this
// object and in-flight callbacks. Explicit engine-thread operations only.
class ArtSlot {
public:
    ArtSlot(PrivateVisual& visual,std::shared_ptr<const api::ModelServices> services,
        api::ModelReleaseSink payload_release);
    ArtSlot(const ArtSlot&)=delete;
    ArtSlot& operator=(const ArtSlot&)=delete;
    void Submit(api::ManagedObjects& objects,api::CharacterModelComponent* model);
    // Active retirement: calls official unload only if the slot still points
    // at THIS Art. Call before retiring the NPC or switching the full model.
    void RequestUnload();
    // Entity/model lifetime ended: never query or mutate this model afterward.
    void OwnerGone();
    // Observed replacement is not proof that the old private Art died. Stop
    // accessing the old model, but do NOT grant orphan-destruction permission.
    void Superseded();
    // Called at the engine service boundary after release callbacks returned.
    // Old native resources/GC may remain pending without occupying the slot.
    void DetachReleasedBinding();
    // Closes binding/GC bookkeeping, NOT the visual lease or external payload.
    // Unsubmitted roots can close immediately; submitted Art must be native-dead
    // (recorded by PrivateVisual.Close) before root bookkeeping is released.
    BindingResult Close();
    const ArtSlotStatus& Status() const { return status_; }
private:
    static void Before(const api::ModelReleaseEvent&,void*);
    static void Failure(const api::ModelReleaseEvent&,const char*,void*) noexcept;
    void Notify(const api::ModelReleaseEvent&);
    void* Keep(api::ObjectResult,const char*);
    PrivateVisual& visual_;
    std::shared_ptr<const api::ModelServices> services_;
    const api::ArtAdmission& admission_;
    const api::ModelAccess& model_access_;
    std::unique_ptr<api::ManagedRoots> roots_;
    api::ModelReleaseSink payload_;
    std::vector<api::ManagedRootId> root_ids_;
    ArtSlotStatus status_;
};
}
