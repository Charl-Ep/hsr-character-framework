#include <hksr/framework/presentation/ActionPlayback.hpp>
#include <stdexcept>

namespace hksr::framework {
std::shared_ptr<const AnimationRequestStatus> ActionPlayback::QueueSkillRecovery() {
    if(status_.closing || !presentation_enabled_)throw std::logic_error("skill recovery requires an owned battle presentation");
    auto entry=std::make_shared<SkillRecovery>();entry->intent=intent_revision_;
    entry->status=std::make_shared<AnimationRequestStatus>();
    entry->status->serial=++recovery_serial_;entry->status->entity_generation=owner_.generation;
    recoveries_.emplace(recovery_serial_,entry);return entry->status;
}
void ActionPlayback::ReleaseSkillRecovery(std::uint64_t serial) {
    const auto found=recoveries_.find(serial);if(found==recoveries_.end())return;
    const auto entry=found->second;
    entry->status->released=true;
    if(entry->body)animation_requests_.Release(entry->body->serial);
    recoveries_.erase(found);
}
void ActionPlayback::CancelSkillRecovery() {
    for(const auto& [id,entry]:recoveries_) {
        entry->status->cancelled=!entry->status->complete;entry->status->released=true;
        if(entry->status->cancelled)entry->status->error="skill recovery owner ended";
        if(entry->body)animation_requests_.Release(entry->body->serial);
    }
    recoveries_.clear();
}
void ActionPlayback::ServiceSkillRecovery(const api::WorldFrame& frame,api::ManagedObjects& objects) {
    std::vector<std::shared_ptr<SkillRecovery>> pending;
    for(const auto& [id,entry]:recoveries_)pending.push_back(entry);
    for(const auto& entry:pending) {
        auto& status=*entry->status;
        if(status.released || status.complete)continue;
        auto live=[&]{return !status.released && !status_.closing && presentation_enabled_ && !Ended() &&
            OwnFrame(frame) && entry->intent==intent_revision_;};
        try {
            if(!live())throw std::runtime_error("skill recovery superseded");
            if(entry->body) {
                if(entry->body->cancelled || !entry->body->error.empty())throw std::runtime_error("skill return animation superseded: "+entry->body->error);
                status.model_generation=entry->body->model_generation;
                // Queue admission and actual return submission are separate.
                status.entered=entry->body->entered;
                status.complete=entry->body->accepted || entry->body->request_finished;
                continue;
            }
            const auto alive=entities_.AliveState(owner_.entity);if(!live())continue;
            if(!alive.read.returned)throw std::runtime_error("skill recovery alive state unread");
            if(alive.value!=1) {status.accepted=status.complete=true;continue;}
            const auto destination=ResolveHitReturn(true,frame,objects,false);if(!live() || !destination)continue;
            const auto component=resources_.Component(api::AnimationComponent::Model,objects);if(!live())continue;
            if(!component.call.returned || !component.object)throw std::runtime_error("skill recovery model unread");
            const auto model=models_.Read(static_cast<api::CharacterModelComponent*>(component.object));if(!live())continue;
            if(!model.animator.read.returned || !model.changing.read.returned)throw std::runtime_error("skill recovery Animator unread");
            if(model.changing.value || !model.animator.value)continue;
            const auto initialized=inputs_.AnimatorInitialized(model.animator.value);if(!live())continue;
            if(!initialized.read.returned)throw std::runtime_error("skill recovery initialization unread");
            if(!initialized.value)continue;
            const auto hash=destination->body.empty()?0:HashName(destination->body,objects);if(!live())continue;
            const auto current=actions_.CurrentState(model.animator.value,0);if(!live())continue;
            const auto transition=actions_.InTransition(model.animator.value,0);if(!live())continue;
            if(!current.read.returned || !transition.read.returned)throw std::runtime_error("skill recovery current body unread");
            if(!transition.value && hash && (current.short_name_hash==hash || current.full_path_hash==hash)) {
                status.accepted=status.complete=status.entered=true;continue; // do not replay a reached return state
            }
            auto same_model=[&] {
                if(!live())return false;
                const auto now=models_.Read(static_cast<api::CharacterModelComponent*>(component.object));
                return live() && now.animator.read.returned && now.animator.value==model.animator.value &&
                    now.changing.read.returned && !now.changing.value;
            };
            std::unique_ptr<api::ExpressionOverride> expression;
            if(!destination->expression.empty()) {
                const auto face=HashName(destination->expression,objects);if(!same_model())continue;
                expression=std::make_unique<api::ExpressionOverride>(OverrideTarget(),model.animator.value,face,destination->expression_transition,same_model);
            }
            if(!same_model())continue;
            if(!destination->body.empty()) {
                AnimationCommand command{destination->body,0,0,0,0.f};command.track=AnimationTrack{1,6,{},false};
                entry->body=animation_requests_.Submit(std::move(command));
            }
            if(expression) {
                const auto result=expression->Apply(same_model);
                if(result.attempted && !result.call.returned)throw std::runtime_error(result.detail);
                if(!same_model())throw std::runtime_error("skill recovery model changed during return");
                // A return is the new persistent expression, not a temporary
                // hit lease. Its destructor must not restore the old skill face.
            }
            if(!live())continue;
            status.accepted=true;status.complete=!entry->body;
        }catch(const std::exception& error) {
            status.error=error.what();status.cancelled=true;ReleaseSkillRecovery(status.serial);
        }
    }
}
}
