#include <hksr/framework/host/RuntimeServices.hpp>
#include <hksr/presentation/PresentationBindings.hpp>
#include <stdexcept>

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
void RuntimeServices::PrepareVisualPrograms(VisualEventRoutes::Trace trace,VisualProgramRuntime::PhaseReader phases) {
    if(programs_)throw std::logic_error("visual program runtime already prepared");
    programs_=std::make_unique<VisualProgramRuntime>(*context_,objects_,std::move(trace));
    programs_->SetPhaseReader(std::move(phases));
}
void RuntimeServices::ActivateIsolation(const api::WorldFrame& frame){context_->Activate();if(programs_){programs_->Activate();programs_->BeginFrame(frame);}}
void RuntimeServices::CollectIsolation(){if(programs_)programs_->Collect();context_->Collect();}
bool RuntimeServices::Close(){return (!programs_ || programs_->Close()) && context_->Close();}
CharacterSessionServices RuntimeServices::Sessions(bool event_tracking) const {
    const auto& r=context_->Resources();
    auto npc_events=event_tracking?context_->EntityEvents():api::PresentationContext::EntityEventFactory{};
    return {
        {context_->Entities(),context_->Models(),context_->Outfits(),context_->NpcInputs()},
        std::move(npc_events),
        {context_->Animations(),context_->ModelTransitions(),context_->EffectServices(),&r.Components(),
            [this](auto entity,auto* world,const auto& event){return context_->StartSound(entity,world,event);}},
        context_->Baselines(),
        [this](auto origin,const auto& input,const auto& profile){
            if(!programs_)throw std::logic_error("visual program runtime was not prepared");
            return programs_->Create(origin,input,profile);
        }
    };
}
}
