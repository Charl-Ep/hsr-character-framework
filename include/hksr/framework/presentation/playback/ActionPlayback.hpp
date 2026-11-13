#pragma once
#include <hksr/framework/presentation/playback/ActionTypes.hpp>
#include <hksr/model/ModelAccess.hpp>
#include <hksr/animation/AnimationControl.hpp>
#include <hksr/entity/EntityAccess.hpp>
#include <hksr/entity/LifecycleHooks.hpp>
#include <hksr/animation/AnimationResources.hpp>
#include <hksr/animation/ActorAnimationSession.hpp>
#include <hksr/animation/AnimationOverrides.hpp>
#include <hksr/model/ProfileInputs.hpp>
#include <hksr/world/WorldFrames.hpp>
#include <hksr/animation/OfficialCatalog.hpp>
#include <optional>
#include <hksr/framework/presentation/playback/ReadyActions.hpp>
#include <hksr/presentation/effects/OwnedEffectRequest.hpp>
#include <hksr/presentation/effects/PresentationEffects.hpp>
#include <hksr/framework/presentation/playback/ActionRetirement.hpp>
#include <hksr/framework/presentation/scene/PresentationProfile.hpp>
#include <hksr/framework/presentation/scene/VisualProgramPlayer.hpp>
#include <hksr/framework/presentation/playback/AnimationRequests.hpp>
#include <hksr/framework/presentation/playback/PoseRequests.hpp>
#include <hksr/framework/presentation/reactions/HitRequests.hpp>
#include <hksr/framework/presentation/reactions/HitInbox.hpp>
#include <hksr/framework/presentation/reactions/ReactionPolicy.hpp>
#include <hksr/framework/presentation/playback/AnimatorLayerWeights.hpp>
#include <hksr/framework/presentation/audio/ActorVoice.hpp>
#include <hksr/presentation/audio/OwnedSoundRequest.hpp>
#include <variant>

namespace hksr::framework {
namespace detail {
class ActorVoicePlayer;
class ExpressionPlayer;
}
// Engine-thread consumer for one owned entity. Queue is data-only; callbacks
// may queue a replacement, which is serviced at a later boundary. This is NOT
// an Animator transition graph or whole skill/gameplay executor.
class ActionPlayback {
  public:
    ActionPlayback(api::EntityToken owner, api::GameWorld* world, std::unique_ptr<api::ActorAnimationSession> engine,
                   std::shared_ptr<const api::PresentationEffects> effects = {},
                   const api::ComponentConfig* text = nullptr);
    ~ActionPlayback();
    std::shared_ptr<HitInbox> IncomingHits() const {
        return hit_inbox_;
    }
    void ConfigureHitResolver(HitResolver resolve) {
        hit_resolver_ = std::move(resolve);
    }
    using VoiceFactory = std::function<std::unique_ptr<api::OwnedSoundRequest>(const ActorVoiceCue&)>;
    using ReadyVoice = std::function<std::optional<api::VoiceCue>(std::int32_t)>;
    void ConfigureVoices(std::uint32_t avatar, std::string source, VoiceFactory start, ReadyVoice ready);
    void ConfigureReceiverPolicy(HitCatalogResolver catalog, std::function<HitControlInputs(bool behaviors)> control) {
        hit_catalog_ = std::move(catalog);
        hit_control_ = std::move(control);
    }
    std::uint64_t Queue(ActionRequest request);
    std::shared_ptr<const AnimationRequestStatus> QueueAnimation(AnimationCommand);
    void ReleaseAnimation(std::uint64_t serial) {
        animation_requests_.Release(serial);
    }
    api::AnimationParameterResult
    SetAnimationParameter(const api::AnimationParameter&,
                          api::AnimationParameterPropagation = api::AnimationParameterPropagation::BodyOnly);
    std::shared_ptr<const AnimationRequestStatus> QueueExpression(std::string state, float transition);
    void ReleaseExpression(std::uint64_t serial);
    std::shared_ptr<const AnimationRequestStatus> QueuePose(PoseCommand);
    void ReleasePose(std::uint64_t serial, bool cancel = false);
    std::shared_ptr<const HitRequestStatus> QueueHit(HitCommand);
    void ReleaseHit(std::uint64_t serial, bool cancel = false) {
        hit_requests_.Release(serial, cancel);
    }
    void ReleaseHitExpression(std::uint64_t serial) {
        hit_requests_.ReleaseExpression(serial);
    }
    api::EntityToken Owner() const {
        return owner_;
    }
    void SetCompletionState(std::string state) {
        completion_state_ = std::move(state);
    }
    std::string CurrentIdle() const {
        return CompletionState();
    }
    bool HasPhaseVisualOverride() const {
        return program_ && bool(program_->CurrentPhase());
    }
    std::optional<CurrentIdleResult> CurrentVisualIdle() const;
    bool SuccessorSettled() const {
        return !program_playback_ || !program_playback_->returning || (program_ && program_->Status().completed);
    }
    void ConfigurePresentation(std::shared_ptr<const PresentationProfile> profile, const std::string& phase = {}) {
        presentation_.Configure(std::move(profile), phase);
        SyncPresentation();
    }
    void SetPresentationDomain(bool battle) {
        if (presentation_enabled_ == battle)
            return;
        CancelHitEffects();
        presentation_enabled_ = battle;
        if (!battle) {
            presentation_.LeaveDomain();
            LeaveProgram();
        }
        SyncPresentation();
    }
    void CancelHitEffects(); // receiver model/domain retirement; not a sender action tail
    void CancelVoices();
    void ConfigureVisualProgram(std::shared_ptr<VisualProgramPlayer>);
    std::optional<VisualProgramSnapshot> CaptureVisualProgram() const;
    VisualBranchState BranchState() const;
    void RestoreVisualProgram(VisualProgramSnapshot);
    bool VisualRestoreReady() const;
    void CancelPending(bool interrupt_presentation = true);
    // Replace a legacy action with a task whose first step need not play a
    // body clip. Keep persistent visual contexts and independent task scopes.
    void PrepareTaskPlayback();
    // inputs_ready comes from model/event/appearance handoff, not a UI cache.
    void Service(const api::WorldFrame& frame, bool inputs_ready, api::ManagedObjects& objects,
                 double playback_rate = 1);
    // Continue cancellation during model/domain handoff; no old Animator calls.
    void ServiceEffects(const api::WorldFrame& frame);
    bool Close(const api::WorldFrame& frame);
    const ActionPlaybackStatus& Status() const {
        return status_;
    }
    api::Observed<bool> InspectState(void* animator, const std::string& name, int layer,
                                     api::ManagedObjects& objects) const;

  private:
    struct ServiceScope {
        bool& active;
        explicit ServiceScope(bool& value) : active(value) {
            active = true;
        }
        ServiceScope(const ServiceScope&) = delete;
        ~ServiceScope() {
            active = false;
        }
    };
    void CollectEffects(const api::WorldFrame&);
    enum class BodyTakeover { Animation, Hit };
    void BeginActionIntent();
    void ReleasePendingAction();
    void TakeBodyControl(BodyTakeover);
    bool PlaySecondaryLayers(void*, int, float, std::uint64_t, api::ManagedObjects&, api::CallResult&);
    void ServiceSecondaryLayers(std::uint64_t);
    std::unique_ptr<AnimatorLayerWeights> secondary_weights_;
    std::function<std::optional<LayerWeightModel>()> secondary_model_;
    struct SecondaryLayer {
        LayerWeightModel model;
        int layer, hash;
        std::uint64_t scope, boundary;
    };
    std::vector<SecondaryLayer> secondary_layers_;
    std::uint64_t secondary_serial_ = 0;
    bool secondary_ending_ = false;
    struct VisualPlayback;
    bool Ended() const;
    bool OwnFrame(const api::WorldFrame& frame) const;
    HitResolution ResolveIncomingHit(const HitInput&, const api::WorldFrame&, api::ManagedObjects&);
    HitEffectAdmission ReceiveHitEffects(const HitInput&, const api::WorldFrame&, api::ManagedObjects&);
    void ServiceHitEffects(const api::WorldFrame&);
    std::optional<HitReturn> ResolveHitReturn(bool body, const api::WorldFrame&, api::ManagedObjects&,
                                              bool require_grounded = true);
    void PlayVoice(api::VoiceCue, const std::string& state, const std::string& source = {});
    void PlayReadyVoice(const ReadyState&, int previous_index);
    void ServiceVoices(const api::WorldFrame&);
    void CancelPose(const std::string& reason);
    void ObservePose(std::uint64_t serial, bool complete = false);
    void PoseResult(const ActionReceipt&, void* animator);
    void Complete(const api::WorldFrame& frame, api::ManagedObjects& objects);
    void StartVisual(const VisualState& request, void* animator, void* style, const api::WorldFrame& frame,
                     api::ManagedObjects& objects, ActionReceipt& receipt);
    void TickVisual(void* animator, void* style, const api::WorldFrame& frame, api::ManagedObjects& objects);
    void ReleaseVisual(void* animator);
    bool OwnsVisualState() const;
    void YieldVisualBody(void* animator);
    void StartReady(const ReadyState& request, void* animator, void* style, const api::WorldFrame& frame,
                    api::ManagedObjects& objects, ActionReceipt& receipt);
    void TickReady(void* animator, void* style, const api::WorldFrame& frame, api::ManagedObjects& objects);
    void ReleaseReady(void* animator, bool preserve_effect = false);
    void ReplaceReadyEffect(const ReadyPose& pose, api::ManagedObjects& objects);
    void CancelReadyEffect();
    void PrepareRetirements(const FreeStyleState&);
    void ServiceRetirements(const api::WorldFrame&);
    void RetireActionEvents();
    void LeaveProgram();
    bool PrepareProgram(ActionReceipt&);
    void CommitProgram(const ActionReceipt&, void* animator, std::uint64_t boundary);
    void TickProgram(const api::WorldFrame&, api::ManagedObjects&);
    int HashName(const std::string& name, api::ManagedObjects& objects);
    api::AnimationOverrideTarget OverrideTarget() {
        return {owner_, resources_, models_, actions_};
    }
    api::EntityToken owner_;
    api::GameWorld* world_;
    std::unique_ptr<api::ActorAnimationSession> engine_;
    const api::EntityAccess& entities_;
    const api::ModelAccess& models_;
    const api::AnimationControl& actions_;
    const api::ProfileInputs& inputs_;
    api::AnimationResources& resources_;
    AnimationRequests animation_requests_;
    std::unique_ptr<detail::ExpressionPlayer> expressions_;
    HitRequests hit_requests_;
    std::shared_ptr<HitInbox> hit_inbox_;
    HitResolver hit_resolver_;
    HitCatalogResolver hit_catalog_;
    std::function<HitControlInputs(bool behaviors)> hit_control_;
    std::optional<PreviousHit> previous_hit_;
    struct ResolvedHit {
        HitDecision decision;
        std::uint64_t frame = 0;
    };
    std::optional<ResolvedHit> resolved_hit_;
    struct ReceivedHit {
        ResolvedHit resolved;
        std::shared_ptr<const HitRequestStatus> playback;
        bool voice_sent = false;
    };
    std::optional<ReceivedHit> received_hit_;
    std::unique_ptr<detail::ActorVoicePlayer> voice_player_;
    ReadyVoice ready_voice_;
    void* hit_policy_animator_ = nullptr;
    std::shared_ptr<AnimationRequestStatus> pose_request_;
    void* pose_animator_ = nullptr;
    std::uint64_t pose_model_ = 0;
    const api::ComponentConfig* text_;
    bool servicing_ = false;
    std::uint64_t intent_revision_ = 0;
    std::optional<ActionReceipt> pending_;
    ActionPlaybackStatus status_;
    struct Completion {
        void* animator = nullptr;
        int hash = 0, layer = 0;
        std::uint64_t boundary = 0;
        bool observed = false;
        std::uint64_t serial = 0;
        bool following_official = false;
    };
    std::optional<Completion> completion_;
    std::string completion_state_;
    PresentationState presentation_;
    bool presentation_enabled_ = false;
    std::string CompletionState() const {
        if (presentation_enabled_ && program_ && program_->OwnsPhases()) {
            if (const auto phase = program_->CurrentPhase())
                if (phase->standby.available && !phase->standby.value.empty())
                    return phase->standby.value;
            return completion_state_;
        }
        return presentation_enabled_ ? presentation_.Idle(completion_state_) : completion_state_;
    }
    void SyncPresentation() {
        status_.presentation_phase = presentation_.Phase();
        status_.presentation_note = presentation_.Note();
    }
    struct VisualPlayback {
        VisualState request;
        void* animator = nullptr;
        int phase = 0, body[3]{};
        bool body_seen = false;
        std::shared_ptr<api::ExpressionOverride> expression;
        std::shared_ptr<api::BlinkOverride> blink;
        std::uint64_t boundary = 0;
        std::uint64_t serial = 0;
        bool task_owned = false, body_suspended = false;
    };
    std::optional<VisualPlayback> visual_;
    bool visual_cancelled_ = false;
    struct ReadyPlayback {
        ReadyState request;
        void* animator = nullptr;
        int from = -1, pose_hash = 0, edge_hash = 0;
        float reverse_until = 0;
        std::shared_ptr<api::ReadySpeedOverride> speed;
        // Selected skill is intent, not the identity of its current clip. An
        // official controller successor must not erase that selection.
        bool edge_seen = false, pose_seen = false, successor_seen = false, reversing = false, voice_sent = false;
        std::optional<ReadyState> pending;
        std::uint64_t boundary = 0;
        std::uint64_t serial = 0;
    };
    std::optional<ReadyPlayback> ready_;
    bool ready_cancelled_ = false;
    std::shared_ptr<const api::PresentationEffects> effect_services_;
    struct EffectEntry {
        std::uint64_t serial;
        std::string path;
        std::unique_ptr<api::OwnedEffectRequest> request;
        std::uint64_t reported_completion = 0;
    };
    std::vector<EffectEntry> effects_;
    std::vector<std::shared_ptr<EffectEntry>> hit_effects_;
    std::uint64_t hit_effect_revision_ = 0;
    api::OwnedEffectRequest* ready_effect_ = nullptr;
    std::uint64_t effect_serial_ = 0;
    std::optional<std::uint64_t> effect_intent_revision_;
    std::unique_ptr<api::EventRegistrationLease> action_exit_;
    std::vector<std::unique_ptr<api::EventRegistrationLease>> retired_exits_;
    bool retire_action_events_ = false;
    std::shared_ptr<VisualProgramPlayer> program_;
    std::uint64_t observed_model_revision_ = 0;
    struct ProgramPlayback {
        std::uint64_t action = 0, intent = 0, model = 0;
        void* animator = nullptr;
        int hash = 0;
        bool observed = false, complete = false;
        std::uint64_t boundary = 0;
        bool returning = false;
    };
    std::optional<ProgramPlayback> program_playback_;
    std::uint64_t program_model_ = 0, program_preparing_ = 0, program_intent_ = 0, program_return_ = 0;
};
}
