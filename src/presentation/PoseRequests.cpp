#include <hksr/framework/presentation/ActionPlayback.hpp>
#include <stdexcept>

namespace hksr::framework {
std::shared_ptr<const AnimationRequestStatus> ActionPlayback::QueuePose(PoseCommand command) {
    if(const auto* ready=std::get_if<ReadyState>(&command))
        if(!ready->actions || ready->pose>=ready->actions->poses.size())
            throw std::invalid_argument("ready pose source missing");
    const auto serial=std::visit([&](auto value){return Queue(std::move(value));},std::move(command));
    pending_->task_owned=true;
    pose_request_=std::make_shared<AnimationRequestStatus>();
    pose_request_->serial=serial;pose_request_->entity_generation=owner_.generation;
    return pose_request_;
}
void ActionPlayback::CancelPose(const std::string& reason) {
    if(!pose_request_)return;
    pose_request_->cancelled=!pose_request_->complete;pose_request_->released=true;
    if(!pose_request_->complete && pose_request_->error.empty())pose_request_->error=reason;
    pose_request_.reset();
}
void ActionPlayback::ReleasePose(std::uint64_t serial,bool cancel) {
    if(!pose_request_ || pose_request_->serial!=serial)return;
    pose_request_->released=true;pose_request_.reset();
    // A natural finish/replacement releases observation, not the selected pose.
    // Retaining it lets the next ready request reverse an unfinished edge.
    if(cancel)CancelPending(false);
}
void ActionPlayback::ObservePose(std::uint64_t serial,bool complete) {
    if(!pose_request_ || pose_request_->serial!=serial || pose_request_->released)return;
    pose_request_->entered=true;pose_request_->complete=pose_request_->complete || complete;
}
void ActionPlayback::PoseResult(const ActionReceipt& receipt,void* animator) {
    if(!pose_request_ || pose_request_->serial!=receipt.serial)return;
    if(receipt.outcome==ActionOutcome::Waiting)return;
    if(receipt.outcome!=ActionOutcome::Returned) {
        CancelPose(receipt.detail.empty()?"pose request failed (outcome="+std::to_string(static_cast<int>(receipt.outcome))+")":receipt.detail);
        return;
    }
    if(animator!=pose_animator_){pose_animator_=animator;++pose_model_;}
    pose_request_->model_generation=pose_model_;pose_request_->accepted=true;
}
}
