#include <hksr/framework/host/RuntimeServices.hpp>
#include <hksr/presentation/PresentationBindings.hpp>
#include <stdexcept>
#include <algorithm>

namespace hksr::framework {
RuntimeServices::RuntimeServices(api::ManagedObjects& objects, api::PresentationBindings bindings)
    : RuntimeServices(objects, std::make_unique<api::PresentationContext>(objects, std::move(bindings))) {}
RuntimeServices::RuntimeServices(api::RuntimeMetadata& metadata, api::RuntimeCapabilities capabilities,
                                 api::PresentationIsolationPolicy policy, std::function<void(const std::string&)> trace)
    : RuntimeServices(metadata, api::PresentationContext::Create(metadata, capabilities, policy, std::move(trace))) {}
RuntimeServices::RuntimeServices(api::ManagedObjects& objects, std::unique_ptr<api::PresentationContext> context)
    : objects_(objects), context_(std::move(context)) {}
AppearanceServices RuntimeServices::Appearance() const {
    const auto& r = context_->Resources();
    return {{objects_, r.Mesh(), r.Skinning(), r.MeshTemplates(), r.Texture(), r.Material(), r.Shader(), r.Arrays(),
             r.Hierarchy()},
            r.Scene(),
            r.Visuals(),
            r.Config(),
            r.Components()};
}
void RuntimeServices::PrepareVisualPrograms(VisualEventRoutes::Trace trace, VisualProgramRuntime::PhaseReader phases,
                                            VisualProgramRuntime::ProjectileReader projectiles,
                                            VisualProgramRuntime::RadiusReader radii) {
    if (programs_)
        throw std::logic_error("visual program runtime already prepared");
    programs_ = std::make_unique<VisualProgramRuntime>(*context_, objects_, std::move(trace));
    programs_->SetPhaseReader(std::move(phases));
    programs_->SetProjectileReader(std::move(projectiles));
    programs_->SetRadiusReader(std::move(radii));
}
void RuntimeServices::ActivateIsolation(const api::WorldFrame& frame) {
    context_->Activate();
    if (programs_) {
        programs_->Activate();
        programs_->BeginFrame(frame);
    }
}
void RuntimeServices::CollectIsolation() {
    if (programs_)
        programs_->Collect();
    context_->Collect();
}
bool RuntimeServices::Close() {
    return (!programs_ || programs_->Close()) && context_->Close();
}
CharacterSessionServices RuntimeServices::Sessions(bool event_tracking) const {
    const auto& r = context_->Resources();
    auto npc_events = event_tracking ? context_->EntityEvents() : api::PresentationContext::EntityEventFactory{};
    CharacterSessionServices services{
        {context_->Entities(), context_->Models(), context_->Outfits(), context_->NpcInputs(),
         [this](auto skin) { return context_->Entities(std::move(skin))(); },
         [this](auto owner, auto* world) { return context_->CreateActorVisualConfiguration(owner, world); },
         [this](auto owner, auto* world) { return context_->ReadEventReceiver(owner, world) != nullptr; }},
        std::move(npc_events),
        {context_->Animations(), context_->ModelTransitions(), context_->EffectServices(), &r.Components(),
         [this](auto entity, auto* world, const auto& event) { return context_->StartSound(entity, world, event); },
         [this](auto origin) { return !programs_ || programs_->PrepareModelChange(origin); },
         [this](const ActorVoiceCue& input) -> std::unique_ptr<api::OwnedSoundRequest> {
             const auto event =
                 context_->ReadVoiceEvent(input.actor.token, input.actor.world, input.avatar_id, input.cue);
             if (event.empty())
                 return {};
             if (const auto error = NativeVoiceCoverageError(native_voice_issues_, input); !error.empty())
                 throw std::runtime_error("automatic voice coverage unavailable: " + error);
             if (std::any_of(native_voice_cues_.begin(), native_voice_cues_.end(),
                             [&](const auto& row) { return NativeVoiceMatches(row, input, event); }) &&
                 UsesNativeVoice(native_voice_cues_, input, event,
                                 context_->ReadActorEventFiles(input.actor.token, input.actor.world)))
                 return {};
             return context_->StartSound(input.actor.token, input.actor.world, event);
         },
         [this](std::int32_t type) { return context_->ReadReadyVoiceCue(type); }},
        context_->Baselines(),
        [this](auto origin, const auto& input, const auto& bindings, const auto& profile) {
            if (!programs_)
                throw std::logic_error("visual program runtime was not prepared");
            return programs_->Create(origin, input, bindings, profile);
        },
        {},
        {},
        [this](auto owner, auto* world) { return context_->CreateActorTiming(owner, world); },
        {},
        {},
        {}};
    if (!audio_events_.empty()) {
        services.audio = [this](auto owner, auto* world, unsigned rate) {
            context_->SetActorAudio(owner, world, audio_events_, rate);
        };
        services.clear_audio = [this](auto owner, auto* world) { context_->ClearActorAudio(owner, world); };
        services.audio_rate = [this](auto owner, auto* world, unsigned rate) {
            context_->SetActorAudioRate(owner, world, rate);
        };
    }
    return services;
}
}
