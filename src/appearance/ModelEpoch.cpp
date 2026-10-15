#include <hksr/framework/appearance/ModelEpoch.hpp>
#include <stdexcept>

namespace hksr::framework {
ModelEpoch::ModelEpoch(std::shared_ptr<const api::ModelServices> services)
    :services_(std::move(services)),roots_(services_->CreateRoots()),renderers_(services_->Renderers()),
     targets_(*services_),assets_(services_){}
void ModelEpoch::Before(const api::ModelReleaseEvent& event,void* context) {
    auto& self=*static_cast<ModelEpoch*>(context);
    if(event.binding.model!=self.status_.binding.model || event.binding.generation!=self.status_.binding.generation) return;
    self.status_.release_seen=true;
    self.status_.published=false;
    self.status_.restoration=self.assets_.BeforeModelRelease();
}
void ModelEpoch::Failure(const api::ModelReleaseEvent&,const char* error,void* context) noexcept {
    auto& self=*static_cast<ModelEpoch*>(context);
    self.status_.release_seen=true; self.status_.published=false;
    self.assets_.SealWithoutRestore();
    try { self.status_.errors.push_back(error); } catch(...) {}
}
void ModelEpoch::PrepareAndApply(api::CharacterModelComponent* model,void* instance_art,void* source_art,
    ModelAssetServices services,const api::SceneObjects& scene,const ModelAssetInput& input) {
    if(status_.started || status_.closed) throw std::logic_error("model incarnation is single-use");
    status_.started=true;
    try {
        if(!model) throw std::invalid_argument("model component missing");
        model_root_=roots_->Keep(model);
        if(!roots_->Status(model_root_).ready) throw std::runtime_error("model component retention failed");
        status_.binding=api::RegisterModelRelease(model,{Before,Failure,this});
        status_.discovery=targets_.Discover(services.objects,scene,services.arrays,renderers_,input,instance_art,source_art);
        if(!status_.discovery.result.complete || status_.release_seen) return;
        status_.preparation=assets_.Prepare(services,input,status_.discovery.art_transform,status_.discovery.targets);
        if(!status_.preparation.result.complete || status_.release_seen) return;
        status_.publication=assets_.Apply();
        status_.published=status_.publication.complete && !status_.release_seen;
    } catch(const std::exception& e) { status_.errors.push_back(e.what()); }
}
BindingResult ModelEpoch::Close() {
    if(status_.closed) return status_.cleanup;
    status_.published=false;
    BindingResult out;
    if(!status_.detached) {
        // Once official release started, only query each retained renderer's
        // actual native lifetime. Never retry restoration into pooled objects.
        out=status_.release_seen?assets_.ObserveReleasedRenderers():assets_.Detach();
        if(!out.complete) { status_.cleanup=out; return out; }
        status_.detached=true;
        if(status_.binding.model) api::ForgetModelRelease(status_.binding);
        // Preserve the token in the receipt; Forget is generation-qualified.
    }
    out=assets_.Close();
    const auto targets=targets_.Close();
    out.errors.insert(out.errors.end(),targets.errors.begin(),targets.errors.end());
    if(model_root_) {
        if(roots_->Release(model_root_).complete) { roots_->Forget(model_root_); model_root_=0; }
        else out.errors.push_back("model component GC root release unresolved");
    }
    out.complete=out.errors.empty(); status_.closed=out.complete; status_.cleanup=out; return out;
}
void ModelEpoch::OwnerGone() {
    if(status_.release_seen || status_.detached) return;
    status_.published=false; status_.release_seen=true;
    assets_.SealWithoutRestore();
    status_.errors.push_back("model owner ended without a pre-release delivery; old renderer writes sealed");
}
}
