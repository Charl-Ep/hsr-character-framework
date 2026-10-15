#include <hksr/framework/presentation/PresentationProfile.hpp>
#include <cmath>

namespace hksr::framework {
void PresentationState::Configure(std::shared_ptr<const PresentationProfile> profile,const std::string& saved) {
    profile_=std::move(profile);action_=nullptr;next_cue_=0;note_.clear();phase_.clear();
    if(!profile_)return;
    phase_=profile_->initial_phase;
    if(!saved.empty()) {
        if(profile_->phases.count(saved))phase_=saved;
        else note_="saved presentation phase unavailable: "+saved+"; using profile initial phase";
    }
}
void PresentationState::Begin(const std::string& state) {
    Interrupt();if(!profile_)return;
    const auto found=profile_->actions.find(state);
    if(found==profile_->actions.end())return;
    action_=&found->second;next_cue_=0;
    if(!action_->enter_phase.empty())phase_=action_->enter_phase;
}
void PresentationState::Observe(float progress) {
    if(!action_ || !std::isfinite(progress))return;
    while(next_cue_<action_->cues.size() && progress>=action_->cues[next_cue_].normalized_time)
        phase_=action_->cues[next_cue_++].phase;
}
void PresentationState::Complete() {
    if(action_ && !action_->complete_phase.empty())phase_=action_->complete_phase;
    action_=nullptr;next_cue_=0;
}
void PresentationState::Interrupt() {
    if(action_ && !action_->interrupt_phase.empty())phase_=action_->interrupt_phase;
    action_=nullptr;next_cue_=0;
}
void PresentationState::LeaveDomain() {
    Interrupt();if(profile_ && profile_->reset_on_domain_exit)phase_=profile_->initial_phase;
}
std::string PresentationState::Idle(const std::string& fallback) const {
    if(!profile_)return fallback;
    const auto found=profile_->phases.find(phase_);return found==profile_->phases.end()?fallback:found->second.idle;
}
}
