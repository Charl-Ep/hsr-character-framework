#pragma once
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "VisualEventRule.hpp"

namespace hksr::framework {
struct PresentationPhase {std::string idle;};
struct PresentationCue {float normalized_time=0;std::string phase;};
enum class PresentationReturn { PhaseIdle, Hold };
struct PresentationAction {
    std::string enter_phase,complete_phase,interrupt_phase;
    std::vector<PresentationCue> cues;
    float complete_at=1;
    PresentationReturn finish=PresentationReturn::PhaseIdle;
};
// Authored visual policy, not an executable skill/Modifier graph. Source binds
// the exact official character configuration, independently of display names.
struct PresentationProfile {
    std::string id,source,initial_phase;
    bool reset_on_domain_exit=true;
    std::map<std::string,PresentationPhase> phases;
    std::map<std::string,PresentationAction> actions;
    std::vector<VisualEventRule> native_events;
};
PresentationProfile ParsePresentationProfile(const std::string& json);
struct PresentationProfiles {
    std::map<std::string,std::shared_ptr<const PresentationProfile>> sources;
    std::vector<std::string> errors;
};
// Value-only per-instance policy. No engine calls, clocks, static character
// tables or file IO. Begin must follow successful playback, not UI submission.
class PresentationState {
public:
    void Configure(std::shared_ptr<const PresentationProfile>,const std::string& saved_phase={});
    void Begin(const std::string& state);
    void Observe(float normalized_time);
    void Complete();
    void Interrupt();
    void Abandon(){action_=nullptr;next_cue_=0;} // engine-owned successor; no authored terminal event asserted
    void LeaveDomain();
    const std::string& Phase() const {return phase_;}
    const std::string& Note() const {return note_;}
    std::string Idle(const std::string& fallback) const;
    float CompletionTime() const {return action_?action_->complete_at:1;}
    bool ReturnToIdle() const {return !action_ || action_->finish==PresentationReturn::PhaseIdle;}
    bool PendingCues() const {return action_ && next_cue_<action_->cues.size();}
private:
    std::shared_ptr<const PresentationProfile> profile_;
    const PresentationAction* action_=nullptr;
    std::string phase_,note_;
    std::size_t next_cue_=0;
};
}
