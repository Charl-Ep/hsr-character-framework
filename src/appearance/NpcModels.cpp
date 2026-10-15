#include <hksr/framework/appearance/NpcModels.hpp>
#include <stdexcept>

namespace hksr::framework {
NpcModels::NpcModels(api::EntityCreationSession& npc,api::GameWorld* world,std::shared_ptr<const ModelAssetInput> input,
    std::shared_ptr<const api::ModelServices> services)
    :npc_(npc),world_(world),input_(std::move(input)),services_(std::move(services)),
    entities_(services_->Entities()),models_(services_->Models()),roots_(services_->CreateRoots()) {
    if(!world_ || !input_) throw std::invalid_argument("NPC model consumer requires its world and immutable payload");
}
void NpcModels::Retire(bool owner_gone) {
    if(!active_) return;
    retired_.reserve(retired_.size()+1);
    if(owner_gone) active_->OwnerGone();
    retired_.push_back(std::move(active_)); current_model_=nullptr; current_art_=nullptr;
}
bool NpcModels::InputsDetached() const {
    if(active_) return false;
    for(const auto& old:retired_) if(!old->Status().detached) return false;
    return true;
}
void NpcModels::Drain(bool allow_live_restore) {
    for(auto it=retired_.begin();it!=retired_.end();) {
        if(!allow_live_restore && !(*it)->Status().detached && !(*it)->Status().release_seen) { ++it; continue; }
        if((*it)->Close().complete) it=retired_.erase(it); else ++it;
    }
    status_.retiring=retired_.size();
    if(status_.closing && !active_ && retired_.empty()) {
        bool done=true;
        if(model_type_) {
            done=roots_->Release(model_type_).complete;
            if(done) { roots_->Forget(model_type_); model_type_=0; }
        }
        status_.closed=done;
    }
}
void NpcModels::Service(const api::WorldFrame& frame,ModelAssetServices assets,const api::SceneObjects& scene) {
    if(status_.closed) return;
    const auto& npc=npc_.Status();
    const api::NpcEntityReceipt* owner=nullptr;
    for(const auto& entity:npc.entities) if(entity.token.entity==npc.factory.entity) { owner=&entity; break; }
    bool gone=npc.closed;
    if(owner) {
        const auto current=api::TrackedEntities().Current(owner->token.entity);
        gone=gone || owner->disposed || api::TrackedEntities().IsDisposed(owner->token) || current.generation!=owner->token.generation;
    }
    if(gone) { status_.closing=true; Retire(true); }
    if(!status_.enabled) Retire(false);
    if(active_ && active_->Status().release_seen) Retire(false);
    const bool own_frame=frame.world==world_ && frame.disposing.read.returned && !frame.disposing.value;
    Drain(own_frame && !gone);
    if(status_.closed || gone) return;
    // Wrong-world frames can drain already-detached owned resources, but must
    // never read or write the old world's entity/renderer to "repair" it.
    if(!own_frame) return;
    if(npc.removal_requested) status_.closing=true;
    if(status_.closing) { Retire(false); Drain(true); return; }
    if(!status_.enabled) return;
    if(!owner || !npc.create_returned) return;
    const auto entity=entities_.Read(owner->token.entity);
    if(!entity.owner_world.read.returned || entity.owner_world.value!=world_ || !entity.loaded.read.returned || !entity.loaded.value) return;
    try {
        if(!type_attempted_) {
            type_attempted_=true;
            const auto type=services_->ComponentType(assets.objects);
            if(!type.call.returned || !type.object) throw std::runtime_error("model component System.Type unavailable");
            model_type_=roots_->Keep(type.object);
            if(!roots_->Status(model_type_).ready) throw std::runtime_error("model component System.Type could not be retained");
        }
        if(!model_type_ || !roots_->Status(model_type_).ready) return;
        const auto type=roots_->Target(model_type_);
        if(!type.call.returned || !type.object) throw std::runtime_error("model component type root unavailable");
        status_.component=entities_.Component(owner->token.entity,type.object);
        if(!status_.component.call.returned || !status_.component.object) return;
        auto* model=static_cast<api::CharacterModelComponent*>(status_.component.object);
        status_.model=models_.Read(model);
        const auto& state=status_.model;
        // Only predicates actually consumed by this operation: loaded Art and
        // no model-input change underway. Root/gameplay/IK state is not a gate.
        if(!state.art.read.returned || !state.art.value || !state.art_loaded.read.returned || !state.art_loaded.value ||
            !state.changing.read.returned || state.changing.value) return;
        if(active_ && current_model_==model && current_art_==state.art.value) return;
        if(active_) Retire(true); // identity changed without the promised boundary
        Drain(true);
        for(const auto& retiring:retired_) if(!retiring->Status().detached) return;
        active_=std::make_unique<ModelEpoch>(services_);
        current_model_=model; current_art_=state.art.value; ++status_.incarnations;
        active_->PrepareAndApply(model,current_art_,current_art_,assets,scene,*input_);
    } catch(const std::exception& e) {
        // Current error, not an ever-growing per-frame diagnostic history.
        status_.errors={e.what()};
    }
}
}
