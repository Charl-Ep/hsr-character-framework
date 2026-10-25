#pragma once
#include "ActionTypes.hpp"
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
#include "ReadyActions.hpp"
#include <hksr/presentation/effects/OwnedEffectRequest.hpp>
#include <hksr/presentation/effects/PresentationEffects.hpp>
#include "ActionRetirement.hpp"
#include "PresentationProfile.hpp"
#include "VisualProgram.hpp"
#include "AnimationRequests.hpp"
#include "PoseRequests.hpp"
#include "HitRequests.hpp"
#include "HitInbox.hpp"
#include <variant>

namespace hksr::framework {
// Engine-thread consumer for one owned entity. Queue is data-only; callbacks
// may queue a replacement, which is serviced at a later boundary. This is NOT
// an Animator transition graph or whole skill/gameplay executor.
class ActionPlayback {
public:
    ActionPlayback(api::EntityToken owner,api::GameWorld* world,std::unique_ptr<api::ActorAnimationSession> engine,
        std::shared_ptr<const api::PresentationEffects> effects={},const api::ComponentConfig* text=nullptr);
    ~ActionPlayback();
    std::shared_ptr<HitInbox> IncomingHits() const {return hit_inbox_;}
    void ConfigureHitResolver(HitResolver resolve){hit_resolver_=std::move(resolve);}
    std::uint64_t Queue(ActionRequest request);
    std::shared_ptr<const AnimationRequestStatus> QueueAnimation(AnimationCommand);
    void ReleaseAnimation(std::uint64_t serial){animation_requests_.Release(serial);}
    std::shared_ptr<const AnimationRequestStatus> QueuePose(PoseCommand);
    void ReleasePose(std::uint64_t serial,bool cancel=false);
    std::shared_ptr<const HitRequestStatus> QueueHit(HitCommand);
    void ReleaseHit(std::uint64_t serial,bool cancel=false){hit_requests_.Release(serial,cancel);}
    void ReleaseHitExpression(std::uint64_t serial){hit_requests_.ReleaseExpression(serial);}
    api::EntityToken Owner() const {return owner_;}
    void SetCompletionState(std::string state) { completion_state_=std::move(state); }
    std::string CurrentIdle() const {return CompletionState();}
    bool SuccessorSettled() const {
        return !program_playback_ || !program_playback_->returning || (program_ && program_->Status().completed);
    }
    void ConfigurePresentation(std::shared_ptr<const PresentationProfile> profile,const std::string& phase={}) {
        presentation_.Configure(std::move(profile),phase);SyncPresentation();
    }
    void SetPresentationDomain(bool battle) {presentation_enabled_=battle;if(!battle){presentation_.LeaveDomain();LeaveProgram();}SyncPresentation();}
    void ConfigureVisualProgram(std::shared_ptr<VisualProgramPlayer>);
    std::optional<VisualProgramSnapshot> CaptureVisualProgram() const;
    VisualBranchState BranchState() const;
    void RestoreVisualProgram(VisualProgramSnapshot);
    bool VisualRestoreReady() const;
    void CancelPending(bool interrupt_presentation=true);
    // inputs_ready comes from model/event/appearance handoff, not a UI cache.
    void Service(const api::WorldFrame& frame,bool inputs_ready,api::ManagedObjects& objects);
    // Continue cancellation during model/domain handoff; no old Animator calls.
    void ServiceEffects(const api::WorldFrame& frame);
    bool Close(const api::WorldFrame& frame);
    const ActionPlaybackStatus& Status() const { return status_; }
private:
    struct VisualPlayback;
    bool Ended() const;
    bool OwnFrame(const api::WorldFrame& frame) const;
    void CancelPose(const std::string& reason);
    void ObservePose(std::uint64_t serial,bool complete=false);
    void PoseResult(const ActionReceipt&,void* animator);
    void Complete(const api::WorldFrame& frame,api::ManagedObjects& objects);
    void StartVisual(const VisualState& request,void* animator,void* style,const api::WorldFrame& frame,
        api::ManagedObjects& objects,ActionReceipt& receipt);
    void TickVisual(void* animator,void* style,const api::WorldFrame& frame,api::ManagedObjects& objects);
    void ReleaseVisual(void* animator);
    void StartReady(const ReadyState& request,void* animator,void* style,const api::WorldFrame& frame,
        api::ManagedObjects& objects,ActionReceipt& receipt);
    void TickReady(void* animator,void* style,const api::WorldFrame& frame,api::ManagedObjects& objects);
    void ReleaseReady(void* animator,bool preserve_effect=false);
    void ReplaceReadyEffect(const ReadyPose& pose,api::ManagedObjects& objects);
    void CancelReadyEffect();
    void PrepareRetirements(const FreeStyleState&);
    void ServiceRetirements(const api::WorldFrame&);
    void RetireActionEvents();
    void LeaveProgram();
    bool PrepareProgram(ActionReceipt&);
    void CommitProgram(const ActionReceipt&,void* animator,std::uint64_t boundary);
    void TickProgram(const api::WorldFrame&,api::ManagedObjects&);
    int HashName(const std::string& name,api::ManagedObjects& objects);
    api::AnimationOverrideTarget OverrideTarget() { return {owner_,resources_,models_,actions_}; }
    api::EntityToken owner_;
    api::GameWorld* world_;
    std::unique_ptr<api::ActorAnimationSession> engine_;
    const api::EntityAccess& entities_;
    const api::ModelAccess& models_;
    const api::AnimationControl& actions_;
    const api::ProfileInputs& inputs_;
    api::AnimationResources& resources_;
    AnimationRequests animation_requests_;
    HitRequests hit_requests_;
    std::shared_ptr<HitInbox> hit_inbox_;
    HitResolver hit_resolver_;
    std::shared_ptr<AnimationRequestStatus> pose_request_;
    void* pose_animator_=nullptr;
    std::uint64_t pose_model_=0;
    const api::ComponentConfig* text_;
    bool servicing_=false;
    std::uint64_t intent_revision_=0;
    std::optional<ActionReceipt> pending_;
    ActionPlaybackStatus status_;
    struct Completion {void* animator=nullptr;int hash=0,layer=0;std::uint64_t boundary=0;bool observed=false;std::uint64_t serial=0;bool following_official=false;};
    std::optional<Completion> completion_;
    std::string completion_state_;
    PresentationState presentation_;
    bool presentation_enabled_=false;
    std::string CompletionState() const {
        if(presentation_enabled_ && program_ && program_->OwnsPhases()) {
            if(const auto phase=program_->CurrentPhase())
                if(phase->standby.available && !phase->standby.value.empty())return phase->standby.value;
            return completion_state_;
        }
        return presentation_enabled_?presentation_.Idle(completion_state_):completion_state_;
    }
    void SyncPresentation(){status_.presentation_phase=presentation_.Phase();status_.presentation_note=presentation_.Note();}
    struct VisualPlayback {
        VisualState request;
        void* animator=nullptr;
        int phase=0,body[3]{};
        bool body_seen=false;
        std::shared_ptr<api::ExpressionOverride> expression;
        std::shared_ptr<api::BlinkOverride> blink;
        std::uint64_t boundary=0;
        std::uint64_t serial=0;
        bool task_owned=false;
    };
    std::optional<VisualPlayback> visual_;
    bool visual_cancelled_=false;
    struct ReadyPlayback {
        ReadyState request;
        void* animator=nullptr;
        int from=-1,pose_hash=0,edge_hash=0;
        float reverse_until=0;
        std::shared_ptr<api::ReadySpeedOverride> speed;
        // Selected skill is intent, not the identity of its current clip. An
        // official controller successor must not erase that selection.
        bool edge_seen=false,pose_seen=false,successor_seen=false,reversing=false;
        std::optional<ReadyState> pending;
        std::uint64_t boundary=0;
        std::uint64_t serial=0;
    };
    std::optional<ReadyPlayback> ready_;
    bool ready_cancelled_=false;
    std::shared_ptr<const api::PresentationEffects> effect_services_;
    struct EffectEntry {std::uint64_t serial;std::string path;std::unique_ptr<api::OwnedEffectRequest> request;};
    std::vector<EffectEntry> effects_;
    api::OwnedEffectRequest* ready_effect_=nullptr;
    std::uint64_t effect_serial_=0;
    std::optional<std::uint64_t> effect_intent_revision_;
    std::unique_ptr<api::EventRegistrationLease> action_exit_;
    std::vector<std::unique_ptr<api::EventRegistrationLease>> retired_exits_;
    bool retire_action_events_=false;
    std::shared_ptr<VisualProgramPlayer> program_;
    struct ProgramPlayback {std::uint64_t action=0,intent=0,model=0;void* animator=nullptr;int hash=0;bool observed=false,complete=false;std::uint64_t boundary=0;bool returning=false;};
    std::optional<ProgramPlayback> program_playback_;
    std::uint64_t program_model_=0,program_preparing_=0,program_intent_=0,program_return_=0;
};
}
