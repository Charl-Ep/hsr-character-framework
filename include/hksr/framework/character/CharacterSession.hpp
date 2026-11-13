#pragma once
#include <hksr/framework/character/SessionStatus.hpp>
#include <hksr/framework/character/CharacterPlan.hpp>
#include <hksr/framework/character/CharacterRestoreState.hpp>
#include <hksr/framework/presentation/scene/VisualProgramBindings.hpp>
#include <hksr/framework/presentation/model/ModelActionHandoff.hpp>
#include <hksr/framework/catalog/ActionCatalog.hpp>
#include <hksr/model/ModelBaselineCapture.hpp>
#include <hksr/animation/ActorTiming.hpp>

namespace hksr::framework {
struct CharacterSessionServices {
    CharacterMethods character;
    api::PresentationContext::EntityEventFactory events;
    HandoffServices handoff;
    api::PresentationContext::BaselineFactory baseline;
    std::function<std::shared_ptr<VisualProgramPlayer>(api::EntityOrigin, const VisualProgramInput&,
                                                       const VisualProgramBindings&,
                                                       const std::shared_ptr<const PresentationProfile>&)>
        visual_program;
    std::function<std::optional<PoseCommand>(const std::vector<ActionSourceRoot>&, const PoseSelection&)> pose;
    std::function<std::optional<HitCatalogInputs>(const std::string&, bool)> hit_inputs;
    std::function<std::unique_ptr<api::ActorTiming>(api::EntityToken, api::GameWorld*)> timing;
    std::function<void(api::EntityToken, api::GameWorld*, unsigned)> audio;
    std::function<void(api::EntityToken, api::GameWorld*)> clear_audio;
    std::function<void(api::EntityToken, api::GameWorld*, unsigned)> audio_rate;
};
// Actual source -> CharacterInstance -> model/action handoff assembly. No UI,
// scanning, player borrowing or implicit game launch. Create/Service execute
// on the official thread. Queue/Remove only change intent, but are not a
// cross-thread queue: a host must marshal UI commands to this thread.
class CharacterSession {
  public:
    CharacterSession(api::GameWorld* world, CharacterPlan plan, CharacterSessionServices services,
                     CharacterRestoreState restore = {}, VisualProgramBindings bindings = {});
    void Create(const api::NpcCreateInput& input);
    void Create(const api::StoryCreateInput& input);
    std::uint64_t Queue(ActionDomain domain, ActionRequest action);
    // Correlated to Queue's session-local identity, not the handoff's ticket.
    std::optional<ActionExecutionStatus> ActionStatus() const;
    TaskControlResult ControlTask(TaskControlInput input);
    void SupersedeQueuedAction() {
        desired_.reset();
    } // leave already-playing output to its owner
    void Remove();
    void SetPlaybackRate(unsigned rate);
    void Service(const api::WorldFrame& frame, AppearanceServices appearance, api::AnimationInputTypes types);
    const CharacterSessionStatus& Status() const {
        return status_;
    }
    const CharacterInstance& Character() const {
        return character_;
    }
    const ModelActionHandoff* Handoff() const {
        return handoff_.get();
    }
    const CharacterPlan& Plan() const {
        return plan_;
    }
    unsigned PlaybackRate() const noexcept {
        return playback_rate_;
    }

  private:
    bool Ended() const;
    bool PrepareBaseline(const api::WorldFrame& frame, AppearanceServices appearance, api::AnimationInputTypes types);
    void StartHandoff(std::unique_ptr<api::ModelProfileRequest> request);
    bool CloseBaseline();
    void PreparePlayback();
    void SetAudioRate(unsigned);
    bool ClosePlayback();
    api::GameWorld* world_;
    CharacterPlan plan_;
    CharacterSessionServices services_;
    VisualProgramBindings program_bindings_;
    unsigned playback_rate_ = 1;
    std::optional<CharacterRestoreState> restore_;
    CharacterInstance character_;
    std::unique_ptr<ModelActionHandoff> handoff_;
    std::unique_ptr<api::ModelBaselineCapture> baseline_;
    api::EntityToken owner_;
    std::unique_ptr<api::ActorTiming> timing_;
    api::ActorTimeScaleToken playback_scale_;
    bool playback_prepared_ = false, audio_registered_ = false;
    struct Intent {
        std::uint64_t serial;
        ActionDomain domain;
        ActionRequest action;
    };
    std::optional<Intent> desired_;
    bool removing_ = false, busy_ = false;
    CharacterSessionStatus status_;
};
}
