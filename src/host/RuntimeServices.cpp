#include <hksr/framework/host/RuntimeServices.hpp>
#include <hksr/presentation/PresentationBindings.hpp>

namespace hksr::framework {
RuntimeServices::RuntimeServices(api::ManagedObjects& objects,api::PresentationBindings bindings)
    :RuntimeServices(objects,std::make_unique<api::PresentationContext>(objects,std::move(bindings))) {}
RuntimeServices::RuntimeServices(api::RuntimeMetadata& metadata,api::RuntimeCapabilities capabilities,
    api::PresentationIsolationPolicy policy,std::function<void(const std::string&)> trace)
    :RuntimeServices(metadata,api::PresentationContext::Create(metadata,capabilities,policy,std::move(trace))) {}
RuntimeServices::RuntimeServices(api::ManagedObjects& objects,std::unique_ptr<api::PresentationContext> context)
    :objects_(objects),context_(std::move(context)) {}
AppearanceServices RuntimeServices::Appearance() const {
    const auto& r=context_->Resources();
    return {{objects_,r.Mesh(),r.Skinning(),r.MeshTemplates(),r.Texture(),r.Material(),r.Shader(),r.Arrays(),r.Hierarchy()},
        r.Scene(),r.Visuals(),r.Config(),r.Components()};
}
void RuntimeServices::ActivateIsolation(){context_->Activate();}
void RuntimeServices::CollectIsolation(){context_->Collect();}
bool RuntimeServices::Close(){return context_->Close();}
CharacterSessionServices RuntimeServices::Sessions(bool event_tracking) const {
    const auto& r=context_->Resources();
    auto npc_events=event_tracking?context_->EntityEvents():api::PresentationContext::EntityEventFactory{};
    return {
        {context_->Entities(),context_->Models(),context_->Outfits(),context_->NpcInputs()},
        std::move(npc_events),
        {context_->Animations(),context_->ModelTransitions(),context_->EffectServices(),&r.Components()},
        context_->Baselines()
    };
}
}
