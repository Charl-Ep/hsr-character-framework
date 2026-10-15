#pragma once
#include "AppearanceTypes.hpp"
#include "../assets/RendererBindings.hpp"
#include <hksr/model/ArtAdmission.hpp>
#include <hksr/unity/AssetLeases.hpp>
#include <hksr/unity/OwnedUnityObjects.hpp>
#include <hksr/managed/ManagedObjects.hpp>
#include <hksr/managed/ManagedArrays.hpp>
#include <hksr/unity/SceneObjects.hpp>

namespace hksr::framework {

struct PrivateVisualStatus {
    api::AssetLeaseId lease=0;
    api::UnityObjectId staging_id=0,art_id=0;
    void* source=nullptr;
    void* staging=nullptr;
    void* staging_transform=nullptr;
    void* art=nullptr;
    void* art_transform=nullptr;
    api::ObjectResult clone;
    api::CallResult initialize,inactivate,activate;
    api::Observed<int> staging_children;
    api::AssetLeaseStatus source_lease;
    api::UnityObjectStatus staging_lifetime,art_lifetime;
    BindingResult preparation,cleanup;
    bool started=false,ready=false,supply_attempted=false,owner_ended=false,closing=false,closed=false;
};
// Source lease and independently cloned visual. No player/scene donor, entity
// creation, JSON diagnostics, or model submission here. Prepare runs on the
// engine thread; native objects remain under an inactive private parent.
// Keep until Close completes; destructor makes no engine calls.
class PrivateVisual {
public:
    explicit PrivateVisual(const api::ModelServices& services);
    PrivateVisual(const PrivateVisual&)=delete;
    PrivateVisual& operator=(const PrivateVisual&)=delete;
    BindingResult Prepare(api::ManagedObjects& objects,const api::SceneObjects& scene,
        const api::ManagedArrays& arrays,const api::VisualObjects& visuals,
        const std::string& prefab_path,VisualRoot root);
    // Call immediately BEFORE official Supply, even when that call later faults.
    // From here we may not destroy Art or its nonempty parent independently.
    void BeginSupply();
    // Only after the receiving entity/model lifetime has actually ended, not
    // on a failed Supply return or temporary slot mismatch. This permits
    // reclaiming our original private object if official adoption never finished.
    void OwnerEnded() noexcept { status_.owner_ended=true; }
    void RetireEmptyStaging(const api::VisualObjects& visuals);
    // Unsubmitted: private parent destroys its children. Submitted: wait for
    // official Art destruction. Keep external resources alive while referenced.
    // A payload with unresolved renderer references can defer source release
    // independently of native visual retirement.
    BindingResult Close(const api::VisualObjects& visuals,bool release_source=true);
    const PrivateVisualStatus& Status() const { return status_; }
private:
    void* Keep(api::ObjectResult object,const char* label);
    std::unique_ptr<api::ManagedRoots> roots_;
    std::unique_ptr<api::AssetLeases> leases_;
    std::unique_ptr<api::OwnedUnityObjects> native_;
    std::vector<api::ManagedRootId> root_ids_;
    PrivateVisualStatus status_;
};
}
