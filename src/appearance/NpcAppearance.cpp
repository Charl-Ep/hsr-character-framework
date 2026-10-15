#include <hksr/framework/appearance/NpcAppearance.hpp>
#include <stdexcept>

namespace hksr::framework {
NpcAppearance::NpcAppearance(api::EntityCreationSession& npc,api::GameWorld* world,Appearance source,
    std::shared_ptr<const ModelAssetInput> payload,VisualRoot root,std::string look_source,
    std::shared_ptr<const api::ModelServices> services)
    :npc_(npc),world_(world),source_(std::move(source)),payload_(std::move(payload)),root_(root),
     look_source_(std::move(look_source)),services_(std::move(services)),entities_(services_->Entities()),
     models_(services_->Models()),admission_(services_->Admission()),roots_(services_->CreateRoots()) {
    if(!world_ || source_.template_prefab.empty()) throw std::invalid_argument("NPC appearance needs owner world and independent source");
}
void NpcAppearance::Retire(bool owner_gone,bool superseded) {
    if(!active_) return;
    retired_.reserve(retired_.size()+1);
    if(owner_gone) active_->OwnerGone();
    else if(superseded) active_->Superseded();
    retired_.push_back(std::move(active_));
}
bool NpcAppearance::InputsDetached() const {
    if(active_) return false;
    for(const auto& appearance:retired_) {
        if(!appearance->Status().payload_detached) return false;
        const auto& slot=appearance->Slot();
        if(slot.binding.model && !slot.binding_detached) return false;
    }
    return true;
}
void NpcAppearance::Drain(bool own_frame,bool owner_gone,const api::VisualObjects& visuals) {
    for(auto it=path_roots_.begin();it!=path_roots_.end();) {
        if(roots_->Release(*it).complete){roots_->Forget(*it);it=path_roots_.erase(it);}else ++it;
    }
    for(auto it=retired_.begin();it!=retired_.end();) {
        auto& visual=**it;
        if(owner_gone) visual.OwnerGone();
        const auto& slot=visual.Slot();
        // Detached resources can finish on later engine frames. Never query
        // an old live model merely because another world is being serviced.
        if(!own_frame && !slot.owner_gone && !slot.superseded && !slot.release_seen && slot.supply_attempted) { ++it; continue; }
        if(visual.Close(visuals).complete) it=retired_.erase(it); else ++it;
    }
    status_.retiring=retired_.size();
    if(status_.closing && !active_ && retired_.empty()) {
        status_.closed=path_roots_.empty() && (!type_ || roots_->Release(type_).complete);
        if(status_.closed && type_) { roots_->Forget(type_); type_=0; }
    }
}
void NpcAppearance::Service(const api::WorldFrame& frame,AppearanceServices services) {
    if(status_.closed) return;
    const auto& npc=npc_.Status();
    const api::NpcEntityReceipt* owner=nullptr;
    for(const auto& receipt:npc.entities) if(receipt.token.entity==npc.factory.entity) { owner=&receipt; break; }
    bool gone=npc.closed;
    if(owner) gone=gone || owner->disposed || api::TrackedEntities().IsDisposed(owner->token) ||
        api::TrackedEntities().Current(owner->token.entity).generation!=owner->token.generation;
    const bool own_frame=frame.world==world_ && frame.disposing.read.returned && !frame.disposing.value;
    if(gone || npc.removal_requested) status_.closing=true;
    if(gone || status_.closing || !status_.enabled) {
        Retire(gone,false);
        attempted_model_=nullptr; attempted_art_=nullptr;
    }
    if(active_ && active_->Slot().release_seen) {
        Retire(false,false);
        // A real pre-release marks the end of this incarnation, even when an
        // object pool later reuses the same pointer value.
        attempted_model_=nullptr; attempted_art_=nullptr;
    }
    Drain(own_frame,gone,services.visuals);
    if(status_.closing || !status_.enabled || gone || !own_frame || !owner || !npc.create_returned) return;
    try {
        const auto entity=entities_.Read(owner->token.entity);
        if(!entity.owner_world.read.returned || entity.owner_world.value!=world_ || !entity.loaded.read.returned || !entity.loaded.value) return;
        if(!type_attempted_) {
            type_attempted_=true;
            const auto type=services_->ComponentType(services.assets.objects);
            if(!type.call.returned || !type.object) throw std::runtime_error("appearance model System.Type unavailable");
            type_=roots_->Keep(type.object);
            if(!roots_->Status(type_).ready) throw std::runtime_error("appearance model type retention failed");
        }
        if(!type_ || !roots_->Status(type_).ready) return;
        const auto type=roots_->Target(type_);
        if(!type.call.returned || !type.object) throw std::runtime_error("appearance model type root unavailable");
        status_.component=entities_.Component(owner->token.entity,type.object);
        if(!status_.component.call.returned || !status_.component.object) return;
        auto* model=static_cast<api::CharacterModelComponent*>(status_.component.object);
        status_.model=models_.Read(model);
        const auto& state=status_.model;
        if(active_ && active_->Slot().supply_attempted && state.art.read.returned &&
            (model!=attempted_model_ || state.art.value!=active_->Visual().art)) Retire(false,true);
        if(active_ && !active_->Slot().supply_attempted && state.art.read.returned &&
            (model!=attempted_model_ || state.art.value!=attempted_art_)) Retire(false,false);
        Drain(true,false,services.visuals);
        if(!state.art.read.returned || !state.art.value || !state.art_loaded.read.returned || !state.art_loaded.value ||
            !state.changing.read.returned || state.changing.value) return;
        for(const auto& old:retired_) {
            if(old->Slot().binding.model && !old->Slot().binding_detached) return;
            // A pre-release notification precedes the official call. If that
            // call faulted with the old Art still installed, do not count the
            // notification itself as a completed unload/new incarnation.
            if(old->Slot().binding.model==model && old->Visual().art==state.art.value) return;
        }
        if(!active_) {
            if(attempted_model_==model && attempted_art_==state.art.value) return;
            attempted_model_=model; attempted_art_=state.art.value;
            active_=std::make_unique<AppearanceModel>(services_);
            ++status_.incarnations;
            active_->Prepare(services.assets,services.scene,services.visuals,services.config,source_,root_,payload_.get());
            if(active_->Status().prepared) {
                // The factory's actual resource path is still available before
                // Supply. Use the authored asset, not live IK target state.
                auto source=look_source_;
                if(source.empty()) {
                    const auto path=admission_.LogicalPath(model);
                    if(path.call.returned && path.object) {
                        const auto root=roots_->Keep(path.object);path_roots_.push_back(root);
                        if(roots_->Status(root).ready) {
                            const auto text=services.components.ReadText(path.object);
                            if(text.read.returned)source=text.utf8;
                        }
                    }
                }
                status_.look_source=source;
                active_->PrepareLookSupport({services.assets.objects,services.scene,services.assets.arrays,services.visuals,services.components},source);
            }
        }
        if(!active_->Status().prepared) return;
        if(!active_->Slot().started) {
            attempted_model_=model;
            active_->Submit(services.assets.objects,model);
        }
        status_.errors=active_->Status().errors;
    } catch(const std::exception& error) { status_.errors={error.what()}; }
}
}
