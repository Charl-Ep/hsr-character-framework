#pragma once
#include "HandoffStatus.hpp"
#include "../character/CharacterInstance.hpp"
#include <hksr/model/ModelTransition.hpp>
#include <hksr/presentation/PresentationContext.hpp>

namespace hksr::framework {
struct DomainControllers { std::optional<api::ControllerSource> exploration,alternate; };
struct HandoffServices {
    api::PresentationContext::AnimationFactory animations;
    api::PresentationContext::ModelTransitionFactory transition;
    std::shared_ptr<const api::PresentationEffects> effects;
    const api::ComponentConfig* text=nullptr;
};
// Coordinates actual consumers, not a visual/gameplay state-machine emulator.
// One retained exploration baseline + an optional alternate profile. Sources/catalog
// build the immutable profile first; this class never invents skill states.
// Owns action playback: do not also enable CharacterInstance's standalone
// exploration player. All operations run on the official world thread.
class ModelActionHandoff {
public:
    ModelActionHandoff(CharacterInstance& character,api::EntityToken owner,api::GameWorld* world,
        std::unique_ptr<api::ModelProfileRequest> request,HandoffServices services,DomainControllers controllers={},
        std::string alternate_initial_state={});
    std::uint64_t Queue(ActionDomain domain,ActionRequest action);
    void Remove(); // data-only; no game call
    void Service(const api::WorldFrame& frame,AppearanceServices appearance,api::AnimationInputTypes types);
    const HandoffStatus& Status() const { return status_; }
    const ActionPlaybackStatus& Playback() const { return actions_.Status(); }
    void ConfigurePresentation(std::shared_ptr<const PresentationProfile> profile,const std::string& phase={}) {actions_.ConfigurePresentation(std::move(profile),phase);}
    void ConfigureVisualProgram(std::shared_ptr<VisualProgramPlayer> program){actions_.ConfigureVisualProgram(std::move(program));}
    const api::ModelRequestStatus* ModelRequest() const { return transition_->RequestStatus(); }
private:
    bool Ended() const;
    bool OwnFrame(const api::WorldFrame& frame) const;
    void UpdateTransitionStatus();
    bool VisualReady(ActionDomain domain) const;
    void Fail(std::string message);
    void Finish(const api::WorldFrame& frame,AppearanceServices appearance,api::AnimationInputTypes types);
    struct Intent { std::uint64_t serial; ActionDomain domain; ActionRequest action; };
    CharacterInstance& character_;
    api::EntityToken owner_;
    api::GameWorld* world_;
    std::unique_ptr<api::ModelTransition> transition_;
    DomainControllers controllers_;
    ActionPlayback actions_;
    std::optional<Intent> desired_,active_;
    bool busy_=false,removing_=false;
    std::string initial_state_name_;
    bool initial_requested_=false;
    HandoffStatus status_;
};
}
