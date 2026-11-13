#pragma once
#include <hksr/framework/character/CharacterSession.hpp>
#include <hksr/presentation/PresentationContext.hpp>
#include <hksr/presentation/events/AnimationEventPolicy.hpp>
#include <hksr/control/MouseMode.hpp>
#include <hksr/world/WorldMap.hpp>
#include <hksr/world/WorldPlacement.hpp>
#include <hksr/presentation/PresentationResources.hpp>
#include <hksr/runtime/RuntimeHost.hpp>
#include <hksr/framework/presentation/scene/VisualProgramRuntime.hpp>
#include <hksr/framework/catalog/ActorAudioCatalog.hpp>

namespace hksr::framework {
// Maps SDK capabilities to Framework session inputs. Resource wrappers remain
// owned by the SDK context and outlive all appearance/session consumers.
class RuntimeServices {
  public:
    RuntimeServices(api::ManagedObjects& objects, api::PresentationBindings bindings);
    RuntimeServices(api::RuntimeMetadata&, api::RuntimeCapabilities, api::PresentationIsolationPolicy,
                    std::function<void(const std::string&)> trace);
    CharacterSessionServices Sessions(bool event_tracking) const;
    void SetActorAudioEvents(std::vector<api::ActorAudioEvent> events) {
        audio_events_ = std::move(events);
    }
    void SetNativeVoiceCues(std::vector<NativeVoiceCue> cues,
                            std::vector<ActorAudioCatalog::NativeCueIssue> issues = {}) {
        native_voice_cues_ = std::move(cues);
        native_voice_issues_ = std::move(issues);
    }
    AppearanceServices Appearance() const;
    void ActivateIsolation(const api::WorldFrame&);
    void PrepareVisualPrograms(VisualEventRoutes::Trace = {}, VisualProgramRuntime::PhaseReader = {},
                               VisualProgramRuntime::ProjectileReader = {}, VisualProgramRuntime::RadiusReader = {});
    void CollectIsolation();
    void SetCameraPresentations(bool enabled) {
        context_->SetCameraPresentations(enabled);
    }
    bool Close();
    std::unique_ptr<api::ManagedRoots> CreateRoots() const {
        return context_->CreateRoots();
    }
    api::WorldMapSnapshot ReadMap(const api::WorldFrame& frame) const {
        return context_->ReadMap(frame);
    }
    api::PlacementSnapshot ReadPlayerPlacement(const api::WorldFrame& frame) const {
        return context_->ReadPlayerPlacement(frame);
    }
    const std::vector<std::string>& BindingErrors() const {
        return context_->BindingErrors();
    }
    std::unique_ptr<api::OfficialCatalog> CreateCatalog() const {
        return context_->CreateCatalog();
    }
    api::CharacterSkinCatalog ReadSkins() const {
        return context_->ReadSkins();
    }
    std::shared_ptr<api::CharacterSkin> PrepareSkin(std::uint32_t id) const {
        return context_->PrepareSkin(id);
    }
    std::unique_ptr<api::MouseModeLease> CreateMouseMode() const {
        return context_->CreateMouseMode();
    }
    std::optional<api::ActorAnimatorView> ReadAnimator(api::EntityToken owner) const {
        return context_->ReadAnimator(owner);
    }
    void* NpcMotionComponent(api::EntityToken owner, api::GameWorld* world) const {
        return context_->NpcMotionComponent(owner, world);
    }
    const api::EntityOwner& Owner() const {
        return context_->Owner();
    }
    api::PresentationContext::ControlFactory Controls() const {
        return context_->Controls();
    }
    api::PresentationContext::PlayerResolver Player() const {
        return context_->Player();
    }
    std::unique_ptr<api::AnimationEventPolicy> CreateAnimationPolicy(api::AnimationTaskPolicy policy) const {
        return context_->CreateAnimationPolicy(policy);
    }

  private:
    RuntimeServices(api::ManagedObjects&, std::unique_ptr<api::PresentationContext>);
    api::ManagedObjects& objects_;
    std::unique_ptr<api::PresentationContext> context_;
    std::unique_ptr<VisualProgramRuntime> programs_;
    std::vector<api::ActorAudioEvent> audio_events_;
    std::vector<NativeVoiceCue> native_voice_cues_;
    std::vector<ActorAudioCatalog::NativeCueIssue> native_voice_issues_;
};
}
