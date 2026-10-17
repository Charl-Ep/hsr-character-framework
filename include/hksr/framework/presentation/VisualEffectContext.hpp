#pragma once
#include <hksr/presentation/effects/OwnedEffectRequest.hpp>
#include <hksr/presentation/effects/PresentationEffects.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace hksr::framework {
using VisualEffectId=std::uint64_t;
struct VisualEffectResult {VisualEffectId id;api::EffectControlResult result;};
// One explicitly entered visual context. It may outlive several body actions;
// its owner decides when to End, then keeps servicing it until Drained.
// Keys are local program names, not global engine names or resource-path scans.
// Official animation-event effects require a separately attributed receipt.
class VisualEffectContext {
public:
    explicit VisualEffectContext(std::shared_ptr<const api::PresentationEffects>);
    VisualEffectId Start(const std::string& key,void* entity,void* rooted_path);
    VisualEffectId StartEntity(const std::string& key,void* caster,void* target,void* rooted_path,const api::EntityEffectOptions&);
    // Caller has attributed this submission to this context. Keeps official
    // callbacks; no re-creation or resource-name lookup. A late receipt while
    // Closing is retained for cancellation, not adopted by a newer action.
    VisualEffectId Track(const std::string& key,void* retained_manager,api::EffectReference);
    // Fade is a request, not completed destruction. Matching requests remain
    // addressable for progress/state changes until the engine releases them.
    void Remove(const std::string& key,bool fade=true);
    // Stop explicit creation without cancelling receipts or existing native
    // producers. End can still interrupt their remaining natural lifetime.
    void Finish();
    void End(bool fade=true);
    void Service();
    // One result per matching submitted request. An empty result means no
    // matching live receipt (or this context is closing), not successful work.
    std::vector<VisualEffectResult> SetProgress(const std::string& key,float progress,float transition_seconds=0);
    std::vector<VisualEffectResult> SetAnimatorState(const std::string& key,int hash,float transition=0,bool restart=true);
    // Receipt-scoped operations let a sequencer retry only pending members of
    // a batch, without restarting transitions already accepted by the engine.
    std::vector<VisualEffectId> Requests(const std::string& key) const;
    api::EffectControlResult SetProgress(VisualEffectId,float progress,float transition_seconds=0);
    api::EffectControlResult SetAnimatorState(VisualEffectId,int hash,float transition=0,bool restart=true);
    api::EffectControlResult SetAnimatorTrigger(VisualEffectId,const std::string& name,bool reset=false);
    void Remove(VisualEffectId,api::EffectRemovalOptions);
    const api::EffectRequestStatus* Status(VisualEffectId) const; // borrowed until Service
    bool Closing() const {return closing_;}
    bool Drained() const {return (finishing_ || closing_) && requests_.empty() && producers_==0;}
    std::size_t PendingRequests() const {return requests_.size();}
private:
    friend class VisualEffectBindings;
    std::size_t producers_=0;
    struct Entry {std::string key;std::unique_ptr<api::OwnedEffectRequest> request;bool retained=true;void* manager=nullptr;};
    std::vector<VisualEffectId> Matching(const std::string&) const;
    std::shared_ptr<const api::PresentationEffects> services_;
    std::map<VisualEffectId,Entry> requests_;
    VisualEffectId next_=0;
    bool finishing_=false,closing_=false,servicing_=false,end_fade_=true;
    unsigned operations_=0;
};
}
