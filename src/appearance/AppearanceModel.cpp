#include <hksr/framework/appearance/AppearanceModel.hpp>
#include <stdexcept>

namespace hksr::framework {
AppearanceModel::AppearanceModel(std::shared_ptr<const api::ModelServices> services)
    :services_(std::move(services)),admission_(services_->Admission()),renderers_(services_->Renderers()),
     visual_(*services_),targets_(*services_),assets_(services_),inputs_(*services_),
     look_(services_->CreateLookSupport()),slot_(visual_,services_,{Before,Failure,this}){}
void AppearanceModel::Prepare(ModelAssetServices services,const api::SceneObjects& scene,
    const api::VisualObjects& visuals,const api::AdventureConfig& config,
    const Appearance& source,VisualRoot root,const ModelAssetInput* payload) {
    if(status_.started || status_.closing) throw std::logic_error("appearance model is single-use");
    status_.started=true; status_.has_payload=payload!=nullptr;
    // Input/config failures are independent of geometry. Receipts remain in
    // Inputs(), not a single readiness gate that hides other capabilities.
    inputs_.Prepare(services.objects,config,source);
    if(!visual_.Prepare(services.objects,scene,services.arrays,visuals,source.template_prefab,root).complete) return;
    if(payload) {
        status_.discovery=targets_.Discover(services.objects,scene,services.arrays,renderers_,
            *payload,visual_.Status().art,visual_.Status().source);
        if(!status_.discovery.result.complete) return;
        status_.resources=assets_.Prepare(services,*payload,status_.discovery.art_transform,status_.discovery.targets);
        if(!status_.resources.result.complete) return;
        status_.publication=assets_.Apply(); // all roles before official admission
        if(!status_.publication.complete) return;
    }
    status_.prepared=true;
}
void AppearanceModel::PrepareLookSupport(api::LookSupportServices services,const std::string& npc_prefab_path) {
    if(!status_.prepared || status_.closing || visual_.Status().supply_attempted)
        throw std::logic_error("LookAt inputs must be prepared on inactive private Art before submission");
    look_->Prepare(services,visual_.Status().art,npc_prefab_path);
    for(const auto& error:look_->Status().errors) status_.errors.push_back("LookAt: "+error);
}
void AppearanceModel::Submit(api::ManagedObjects& objects,api::CharacterModelComponent* model) {
    if(!status_.prepared || status_.closing) throw std::logic_error("appearance not prepared for submission");
    if(look_->Status().started && !look_->SettleFailure()) return;
    slot_.Submit(objects,model);
    if(slot_.Status().adopted && slot_.Status().supply.returned)
        status_.weapons=inputs_.ApplyWeaponDefaults(admission_,model);
    // Adoption after a fault is an ownership observation, NOT proof that all
    // official attach-map initialization completed. Preserve it for retirement.
}
void AppearanceModel::Before(const api::ModelReleaseEvent&,void* context) {
    auto& self=*static_cast<AppearanceModel*>(context);
    self.status_.prepared=false;
    self.status_.payload_sealed=true;
    if(self.status_.has_payload) self.status_.restoration=self.assets_.BeforeModelRelease();
    else self.status_.restoration.complete=true;
    self.status_.payload_detached=self.status_.restoration.complete;
}
void AppearanceModel::Seal() noexcept {
    status_.prepared=false; status_.payload_sealed=true;
    if(status_.has_payload) assets_.SealWithoutRestore();
}
void AppearanceModel::Failure(const api::ModelReleaseEvent&,const char* error,void* context) noexcept {
    auto& self=*static_cast<AppearanceModel*>(context);
    self.Seal();
    try { self.status_.errors.push_back(error); } catch(...) {}
}
void AppearanceModel::OwnerGone() {
    status_.prepared=false; status_.closing=true;
    slot_.OwnerGone();
}
void AppearanceModel::Superseded() {
    status_.prepared=false; status_.closing=true;
    slot_.Superseded();
}
BindingResult AppearanceModel::Close(const api::VisualObjects& visuals) {
    if(status_.closed) return status_.cleanup;
    status_.closing=true; status_.prepared=false;
    slot_.RequestUnload(); // owning Art callback restores BEFORE official unload
    if(!status_.payload_detached) {
        if(!status_.has_payload) status_.payload_detached=true;
        else if(!visual_.Status().supply_attempted && !status_.payload_sealed) {
            status_.restoration=assets_.Detach();
            status_.payload_detached=status_.restoration.complete;
            if(!status_.payload_detached) Seal(); // private hierarchy can retire; never retry stale writes
        } else if(!status_.payload_sealed) Seal(); // missing/uncertain release, no late restoration
    }
    BindingResult out;
    // Retire visual hierarchy even if a payload restore failed; payload native
    // resources are NOT destroyed until every consuming renderer is detached/dead.
    const auto visual=visual_.Close(visuals,status_.payload_detached);
    if(!status_.payload_detached && status_.payload_sealed) {
        const auto ended=assets_.ObserveReleasedRenderers();
        status_.payload_detached=ended.complete;
        out.errors.insert(out.errors.end(),ended.errors.begin(),ended.errors.end());
    }
    if(status_.payload_detached) {
        const auto assets=assets_.Close();
        out.errors.insert(out.errors.end(),assets.errors.begin(),assets.errors.end());
    }
    const auto targets=targets_.Close(),inputs=inputs_.Close(),slot=slot_.Close();
    const auto look=look_->Close();
    out.errors.insert(out.errors.end(),look.errors.begin(),look.errors.end());
    out.errors.insert(out.errors.end(),targets.errors.begin(),targets.errors.end());
    out.errors.insert(out.errors.end(),inputs.errors.begin(),inputs.errors.end());
    out.errors.insert(out.errors.end(),slot.errors.begin(),slot.errors.end());
    out.errors.insert(out.errors.end(),visual.errors.begin(),visual.errors.end());
    out.complete=out.errors.empty(); status_.closed=out.complete; status_.cleanup=out;
    return out;
}
}
