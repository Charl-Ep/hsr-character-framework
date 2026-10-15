#pragma once
#include "../appearance/NpcAppearance.hpp"
#include <hksr/presentation/events/NpcEventSupplement.hpp>
#include "../presentation/ActionPlayback.hpp"
#include "../appearance/NpcModels.hpp"
#include <hksr/model/CharacterOutfit.hpp>
#include <hksr/presentation/PresentationContext.hpp>

namespace hksr::framework {
struct CharacterMethods {
    api::PresentationContext::NpcFactory create;
    std::shared_ptr<const api::ModelServices> models;
    std::function<std::unique_ptr<api::CharacterOutfit>()> outfit;
    api::PresentationContext::NpcInputFactory prepare;
};
struct CharacterInstanceStatus {
    bool exploration_inputs_enabled=true;
    bool exploration_events_enabled=true;
    bool alternate_payload_enabled=false;
    bool event_preparation_attempted=false;
    bool closed=false;
    std::vector<std::string> errors;
};
// One exploration NPC creation epoch. Official NPC uses the factory's Art;
// an Appearance uses independent private Art, optionally with external payload.
// NpcModels may overlay alternate official Art, never concurrently with the
// private Appearance registration on the same model.
// Source/asset lookup, saved intentions, action-domain changes and UI remain
// separate consumers. This is not an automatic runtime bootstrap.
class CharacterInstance {
public:
    CharacterInstance(api::GameWorld* world,const CharacterPackage& package,
        std::shared_ptr<const ModelAssetInput> payload,std::string npc_look_source,
        CharacterMethods methods,api::PresentationContext::NpcEventFactory events,
        VisualRoot root=VisualRoot::UniqueAnimator);
    void Create(const api::NpcCreateInput& input);
    void EnableActionPlayback(const api::PresentationContext::AnimationFactory& animations);
    // Exploration only; whole-model/battle handoff must use its own readiness
    // before submitting FreeStyleState to ActionPlayback.
    std::uint64_t QueueExplorationAction(std::variant<NpcMotion,AnimatorState> request);
    void Remove(); // intent only
    // Used by the action-domain coordinator before/after a whole-model change.
    // Only this framework's appearance/event supplements, NOT official actions.
    void SetExplorationInputsEnabled(bool enabled);
    // During restoration, adopt private Art first, then the official baseline
    // event arrays, and only then add this framework's event supplements.
    void SetExplorationEventsEnabled(bool enabled);
    bool ExplorationInputsDetached() const;
    // Whole-model coordinator selects this only AFTER the alternate model
    // loads. It overlays that official Art; it does not substitute Player Art.
    void SetAlternatePayloadEnabled(bool enabled);
    bool AlternatePayloadDetached() const;
    void Service(const api::WorldFrame& frame,AppearanceServices services,api::AnimationInputTypes event_types);
    const CharacterInstanceStatus& Status() const { return status_; }
    const api::NpcCreationStatus& Npc() const { return npc_->Status(); }
    const NpcAppearance* AppearanceConsumer() const { return appearance_.get(); }
    const api::NpcEventSupplement* Events() const { return events_.get(); }
    const ActionPlayback* Actions() const { return actions_.get(); }
    const NpcModels* AlternatePayload() const { return alternate_.get(); }
    const api::CharacterOutfitStatus* Outfit() const {return outfit_?&outfit_->Status():nullptr;}
private:
    api::GameWorld* world_;
    std::shared_ptr<const api::ModelServices> models_;
    const api::EntityAccess& entities_;
    std::unique_ptr<api::NpcCreationSession> npc_;
    // Reverse destruction keeps callback owners alive until their consumers
    // are gone. Explicit Service closure is still required before destruction.
    std::unique_ptr<NpcAppearance> appearance_;
    std::unique_ptr<api::NpcEventSupplement> events_;
    std::unique_ptr<ActionPlayback> actions_;
    std::unique_ptr<NpcModels> alternate_;
    std::uint32_t avatar_id_=0;
    bool outfit_attempted_=false;
    std::function<std::unique_ptr<api::CharacterOutfit>()> outfit_factory_;
    std::unique_ptr<api::CharacterOutfit> outfit_;
    CharacterInstanceStatus status_;
};
}
