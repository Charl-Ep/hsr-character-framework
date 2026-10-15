#include <hksr/framework/appearance/PrivateVisual.hpp>
#include <stdexcept>

namespace hksr::framework {
PrivateVisual::PrivateVisual(const api::ModelServices& services)
    :roots_(services.CreateRoots()),leases_(services.CreateAssetLeases()),native_(services.CreateNativeObjects()){}
void* PrivateVisual::Keep(api::ObjectResult value,const char* label) {
    if(!value.call.returned || !value.object) throw std::runtime_error(label);
    root_ids_.push_back(0); const auto id=roots_->Keep(value.object); root_ids_.back()=id;
    if(!roots_->Status(id).ready) throw std::runtime_error(std::string(label)+": retain failed");
    return value.object;
}
BindingResult PrivateVisual::Prepare(api::ManagedObjects& objects,const api::SceneObjects& scene,
    const api::ManagedArrays& arrays,const api::VisualObjects& visuals,const std::string& path,VisualRoot root) {
    if(status_.started || status_.closing) throw std::logic_error("private visual is single-use");
    status_.started=true;
    try {
        if(path.empty()) throw std::invalid_argument("visual source path empty");
        auto* type=Keep(objects.ManagedType("UnityEngine","GameObject"),"GameObject type");
        auto* name=Keep(objects.NewStringUtf8(path.c_str()),"prefab path");
        status_.lease=leases_->Acquire(name,type,false);
        status_.source_lease=leases_->Status(status_.lease);
        auto* prefab=leases_->Object(status_.lease);
        if(!prefab) throw std::runtime_error("independent prefab lease failed");
        status_.source=prefab;
        if(root==VisualRoot::UniqueAnimator) {
            auto* animator_type=Keep(objects.ManagedType("UnityEngine","Animator"),"Animator type");
            auto* components=Keep(scene.Components(prefab,animator_type,true),"source Animator enumeration");
            const auto count=arrays.Length(components);
            if(!count.read.returned || count.value!=1) throw std::runtime_error("Animator visual root is not unique");
            auto* animator=Keep(arrays.Get(components,0),"source Animator");
            status_.source=Keep(visuals.GameObject(animator),"source Animator GameObject");
        }
        const auto allocated=objects.AllocateObject("UnityEngine","GameObject");
        if(!allocated.call.returned || !allocated.object) throw std::runtime_error("staging allocation failed");
        status_.staging=allocated.object;
        status_.staging_id=native_->Adopt(status_.staging); // before constructor
        if(!native_->Status(status_.staging_id).root.ready) throw std::runtime_error("staging retention failed");
        status_.initialize=visuals.Initialize(status_.staging);
        if(!status_.initialize.returned) throw std::runtime_error("staging constructor failed");
        status_.inactivate=visuals.SetActive(status_.staging,false);
        if(!status_.inactivate.returned) throw std::runtime_error("staging inactivation failed");
        status_.staging_transform=Keep(scene.Transform(status_.staging),"staging transform");
        status_.clone=visuals.CloneUnderParent(status_.source,status_.staging_transform,false);
        if(!status_.clone.call.returned || !status_.clone.object) throw std::runtime_error("private visual clone failed");
        if(status_.clone.object==status_.source || status_.clone.object==prefab || status_.clone.object==status_.staging)
            throw std::runtime_error("clone returned a nonprivate object");
        status_.art=status_.clone.object;
        status_.art_id=native_->Adopt(status_.art);
        if(!native_->Status(status_.art_id).root.ready) throw std::runtime_error("private visual retention failed");
        status_.activate=visuals.SetActive(status_.art,true); // parent remains inactive
        if(!status_.activate.returned) throw std::runtime_error("private visual activeSelf failed");
        status_.art_transform=Keep(scene.Transform(status_.art),"private visual transform");
        status_.ready=true; status_.preparation.complete=true;
    } catch(const std::exception& error) { status_.preparation.errors.push_back(error.what()); }
    return status_.preparation;
}
void PrivateVisual::BeginSupply() {
    if(!status_.ready || status_.closing || status_.supply_attempted)
        throw std::logic_error("private visual cannot begin submission");
    status_.supply_attempted=true;
}
void PrivateVisual::RetireEmptyStaging(const api::VisualObjects& visuals) {
    if(!status_.supply_attempted || !status_.staging_id || status_.staging_lifetime.complete) return;
    status_.staging_lifetime=native_->ObserveEnd(status_.staging_id);
    if(status_.staging_lifetime.native_destroyed) return;
    if(!status_.staging_lifetime.alive.read.returned || !status_.staging_lifetime.alive.value) return;
    status_.staging_children=visuals.ChildCount(status_.staging_transform);
    if(status_.staging_children.read.returned && status_.staging_children.value==0)
        status_.staging_lifetime=native_->Close(status_.staging_id);
}
BindingResult PrivateVisual::Close(const api::VisualObjects& visuals,bool release_source) {
    if(status_.closed) return status_.cleanup;
    status_.closing=true; status_.ready=false;
    BindingResult out;
    if(status_.staging_id) {
        if(!status_.supply_attempted) status_.staging_lifetime=native_->Close(status_.staging_id);
        else RetireEmptyStaging(visuals);
    }
    if(status_.art_id) status_.art_lifetime=status_.owner_ended ? native_->Close(status_.art_id) : native_->ObserveEnd(status_.art_id);
    const bool art_dead=!status_.art_id || status_.art_lifetime.native_destroyed;
    // Official unload may destroy Art while leaving other private children in
    // staging. Once Art is native-dead, the parent is ours to retire as well.
    if(status_.supply_attempted && art_dead && status_.staging_id)
        status_.staging_lifetime=native_->Close(status_.staging_id);
    const bool stage_dead=!status_.staging_id || status_.staging_lifetime.native_destroyed;
    if(!art_dead) out.errors.push_back("private Art native lifetime still pending");
    if(!stage_dead) out.errors.push_back("preparation parent native lifetime still pending");
    if(status_.art_id && !status_.art_lifetime.complete) out.errors.push_back("private Art root cleanup pending");
    if(status_.staging_id && !status_.staging_lifetime.complete) out.errors.push_back("preparation parent root cleanup pending");
    if(art_dead && stage_dead && release_source) {
        // GC handle failure is not a dependency of independent asset release.
        if(status_.lease) {
            status_.source_lease=leases_->Release(status_.lease);
            if(!status_.source_lease.complete) out.errors.push_back("source lease cleanup unresolved");
        }
        for(auto id:root_ids_) if(id && !roots_->Release(id).complete) out.errors.push_back("visual input root cleanup unresolved");
    }
    if(!release_source) out.errors.push_back("visual source retained for pending payload references");
    out.complete=out.errors.empty(); status_.closed=out.complete; status_.cleanup=out; return out;
}
}
