#include <hksr/framework/appearance/ArtSlot.hpp>
#include <stdexcept>
namespace hksr::framework {
ArtSlot::ArtSlot(PrivateVisual& visual,std::shared_ptr<const api::ModelServices> services,api::ModelReleaseSink payload)
    :visual_(visual),services_(std::move(services)),admission_(services_->Admission()),
     model_access_(services_->Models()),roots_(services_->CreateRoots()),payload_(payload) {
    if((payload.before!=nullptr)!=(payload.failure!=nullptr))
        throw std::invalid_argument("payload release callback needs its failure reporter");
}
void* ArtSlot::Keep(api::ObjectResult value,const char* label) {
    if(!value.call.returned || !value.object) throw std::runtime_error(label);
    root_ids_.push_back(0); const auto id=roots_->Keep(value.object); root_ids_.back()=id;
    if(!roots_->Status(id).ready) throw std::runtime_error(std::string(label)+": retention failed");
    return value.object;
}
void ArtSlot::Submit(api::ManagedObjects& objects,api::CharacterModelComponent* model) {
    if(status_.started || status_.closed) throw std::logic_error("Art slot transaction is single-use");
    status_.started=true;
    try {
        if(!visual_.Status().ready || !model) throw std::runtime_error("Art slot lacks a prepared visual or owning model");
        Keep({{true,0},model},"owning model");
        status_.logical_path=admission_.LogicalPath(model);
        auto* path=Keep(status_.logical_path,"model logical Art path");
        auto* key=Keep(objects.NewStringUtf8("ArtRoot"),"ArtRoot key");
        status_.binding=api::RegisterModelRelease(model,{Before,Failure,this});
        visual_.BeginSupply(); status_.supply_attempted=true;
        status_.supply=admission_.Supply(model,key,path,false,nullptr,visual_.Status().art);
        // An exception may be AFTER official adoption. Observe independently;
        // never equate call.returned to ownership, never delete the NPC here.
        if(!status_.owner_gone) {
            status_.current_art=model_access_.Art(model);
            status_.adopted=status_.current_art.read.returned && status_.current_art.value==visual_.Status().art && !status_.release_seen;
        }
        if(!status_.supply.returned) status_.errors.push_back("official Art supply did not return normally");
        if(!status_.adopted) status_.errors.push_back("official Art adoption not observed");
    } catch(const std::exception& error) { status_.errors.push_back(error.what()); }
}
void ArtSlot::Before(const api::ModelReleaseEvent& event,void* context) {
    static_cast<ArtSlot*>(context)->Notify(event);
}
void ArtSlot::Notify(const api::ModelReleaseEvent& event) {
    if(event.binding.model!=status_.binding.model || event.binding.generation!=status_.binding.generation || status_.owner_gone) return;
    status_.current_art=model_access_.Art(event.binding.model);
    if(!status_.current_art.read.returned) throw std::runtime_error("pre-release Art identity unavailable");
    if(status_.current_art.value!=visual_.Status().art) {
        // Supply unloads the PREVIOUS official Art. It must not seal the prepared
        // payload. No coordinate/name/frame heuristics: compare exact Art object.
        if(status_.adopted && !status_.release_seen) {
            status_.adopted=false;
            const char* error="adopted Art disappeared before its release notification";
            status_.errors.push_back(error);
            if(payload_.failure) payload_.failure(event,error,payload_.context);
        } else if(!status_.release_seen) ++status_.previous_art_releases;
        if(event.boundary==api::ModelReleaseBoundary::RootRelease) status_.owner_gone=true;
        return;
    }
    if(status_.release_seen) return;
    status_.release_seen=true; status_.adopted=false;
    if(payload_.before) payload_.before(event,payload_.context);
}
void ArtSlot::Failure(const api::ModelReleaseEvent& event,const char* error,void* context) noexcept {
    auto& self=*static_cast<ArtSlot*>(context);
    try { self.status_.errors.push_back(error); } catch(...) {}
    // The original release still runs. The consumer must seal stale writes,
    // not retry on a potentially pooled renderer next frame.
    if(self.payload_.failure) self.payload_.failure(event,error,self.payload_.context);
}
void ArtSlot::RequestUnload() {
    if(!status_.supply_attempted || status_.owner_gone || status_.superseded || status_.release_seen || status_.unload_attempted || status_.closed) return;
    status_.current_art=model_access_.Art(status_.binding.model);
    if(!status_.current_art.read.returned || status_.current_art.value!=visual_.Status().art) return;
    status_.unload_attempted=true;
    status_.unload=admission_.Unload(status_.binding.model,true);
    if(!status_.unload.returned) status_.errors.push_back("official private Art unload did not return normally");
}
void ArtSlot::OwnerGone() {
    const bool previously_ended=status_.owner_gone;
    status_.owner_gone=true; status_.adopted=false;
    visual_.OwnerEnded();
    if(!previously_ended && status_.supply_attempted && !status_.release_seen && payload_.failure)
        payload_.failure({status_.binding,api::ModelReleaseBoundary::RootRelease,false},
            "model owner ended without a confirmed private Art pre-release",payload_.context);
    DetachReleasedBinding();
}
void ArtSlot::Superseded() {
    if(status_.superseded) return;
    status_.superseded=true; status_.adopted=false;
    if(status_.supply_attempted && !status_.release_seen && payload_.failure)
        payload_.failure({status_.binding,api::ModelReleaseBoundary::ArtUnload,false},
            "private Art/model identity changed without pre-release; old writes sealed",payload_.context);
    DetachReleasedBinding();
}
void ArtSlot::DetachReleasedBinding() {
    if(status_.binding_detached) return;
    if(status_.binding.model && !status_.release_seen && !status_.owner_gone && !status_.superseded) return;
    if(status_.binding.model) api::ForgetModelRelease(status_.binding);
    status_.binding_detached=true;
}
BindingResult ArtSlot::Close() {
    if(status_.closed) return status_.cleanup;
    DetachReleasedBinding();
    BindingResult out;
    if(status_.supply_attempted && !visual_.Status().art_lifetime.native_destroyed) {
        out.errors.push_back("officially submitted Art lifetime still pending");
    } else {
        if(status_.binding.model) api::ForgetModelRelease(status_.binding);
        for(auto id:root_ids_) if(id && !roots_->Release(id).complete) out.errors.push_back("Art slot root release unresolved");
    }
    out.complete=out.errors.empty(); status_.closed=out.complete; status_.cleanup=out; return out;
}
}
