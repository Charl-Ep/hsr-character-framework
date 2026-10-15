#pragma once
#include "SessionStatus.hpp"
#include "CharacterPlan.hpp"
#include "../presentation/ModelActionHandoff.hpp"
#include "../catalog/ActionCatalog.hpp"
#include <hksr/model/ModelBaselineCapture.hpp>

namespace hksr::framework {
struct CharacterSessionServices {
    CharacterMethods character;
    api::PresentationContext::NpcEventFactory events;
    HandoffServices handoff;
    api::PresentationContext::BaselineFactory baseline;
};
// Actual source -> CharacterInstance -> model/action handoff assembly. No UI,
// scanning, player borrowing or implicit game launch. Create/Service execute
// on the official thread. Queue/Remove only change intent, but are not a
// cross-thread queue: a host must marshal UI commands to this thread.
class CharacterSession {
public:
    CharacterSession(api::GameWorld* world,CharacterPlan plan,CharacterSessionServices services);
    void Create(const api::NpcCreateInput& input);
    std::uint64_t Queue(ActionDomain domain,ActionRequest action);
    void Remove();
    void Service(const api::WorldFrame& frame,AppearanceServices appearance,api::AnimationInputTypes types);
    const CharacterSessionStatus& Status() const { return status_; }
    const CharacterInstance& Character() const { return character_; }
    const ModelActionHandoff* Handoff() const { return handoff_.get(); }
    const CharacterPlan& Plan() const { return plan_; }
private:
    bool Ended() const;
    bool PrepareBaseline(const api::WorldFrame& frame,AppearanceServices appearance,api::AnimationInputTypes types);
    void StartHandoff(std::unique_ptr<api::ModelProfileRequest> request);
    bool CloseBaseline();
    api::GameWorld* world_;
    CharacterPlan plan_;
    CharacterSessionServices services_;
    CharacterInstance character_;
    std::unique_ptr<ModelActionHandoff> handoff_;
    std::unique_ptr<api::ModelBaselineCapture> baseline_;
    api::EntityToken owner_;
    struct Intent { std::uint64_t serial; ActionDomain domain; ActionRequest action; };
    std::optional<Intent> desired_;
    bool removing_=false,busy_=false;
    CharacterSessionStatus status_;
};
}
