#pragma once
#include <hksr/presentation/effects/OwnedEffectRequest.hpp>
#include <hksr/presentation/effects/PresentationEffects.hpp>
#include <hksr/presentation/effects/EffectLayouts.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <set>

namespace hksr::framework {
class VisualEntityMotion;
class VisualContextWork;
using VisualEffectId = std::uint64_t;
struct VisualEffectResult {
    VisualEffectId id;
    api::EffectControlResult result;
};
enum class VisualEffectAdmissionKind { Created, Replayed, Suppressed };
struct VisualEffectAdmission {
    VisualEffectAdmissionKind kind = VisualEffectAdmissionKind::Created;
    VisualEffectId id = 0;
    VisualEffectAdmission(VisualEffectId created) : id(created) {}
    VisualEffectAdmission(VisualEffectAdmissionKind result, VisualEffectId receipt = 0) : kind(result), id(receipt) {}
};
// One explicitly entered visual context. It may outlive several body actions;
// its owner decides when to End, then keeps servicing it until Drained.
// Keys are local program names, not global engine names or resource-path scans.
// Official animation-event effects require a separately attributed receipt.
class VisualEffectContext {
  public:
    explicit VisualEffectContext(std::shared_ptr<const api::PresentationEffects>);
    VisualEffectId Start(const std::string& key, void* entity, void* rooted_path);
    VisualEffectId StartEntity(const std::string& key, void* caster, void* target, void* rooted_path,
                               const api::EntityEffectOptions&, const api::EffectSubmissionOptions& = {});
    VisualEffectId StartSpatial(const std::string& key, void* caster, void* rooted_path,
                                const api::SpatialEffectOptions&, const api::EffectSubmissionOptions& = {});
    // nullopt permits a new request. Global queries can observe foreign
    // effects, but replay is restricted to this owner's key-matched receipts.
    // The path is already mapped and retained; target may be null for global scope.
    std::optional<VisualEffectAdmission> CheckMutex(const std::string& key, void* target, void* rooted_path,
                                                    const std::string& mapped_path, api::EffectMutexRule);
    // Caller has attributed this submission to this context. Keeps official
    // callbacks; no re-creation or resource-name lookup. A late receipt while
    // Closing is retained for cancellation, not adopted by a newer action.
    VisualEffectId Track(const std::string& key, void* retained_manager, api::EffectReference);
    // Fade is a request, not completed destruction. Matching requests remain
    // addressable for progress/state changes until the engine releases them.
    void Remove(const std::string& key, bool fade = true);
    // Stop explicit creation without cancelling receipts or existing native
    // producers. End can still interrupt their remaining natural lifetime.
    void Finish();
    void End(bool fade = true);
    void Service();
    // One result per matching submitted request. An empty result means no
    // matching live receipt (or this context is closing), not successful work.
    std::vector<VisualEffectResult> SetProgress(const std::string& key, float progress, float transition_seconds = 0);
    std::vector<VisualEffectResult> SetAnimatorState(const std::string& key, int hash, float transition = 0,
                                                     bool restart = true);
    // Receipt-scoped operations let a sequencer retry only pending members of
    // a batch, without restarting transitions already accepted by the engine.
    std::vector<VisualEffectId> Requests(const std::string& key) const;
    // Explicitly selected persistent effects, in submission order. Ended,
    // cancelled and borrowed animation-event receipts are not reconstructed.
    std::vector<std::string> CaptureEffects(const std::set<std::string>& keys) const;
    api::EffectControlResult SetProgress(VisualEffectId, float progress, float transition_seconds = 0);
    api::EffectControlResult SetAnimatorState(VisualEffectId, int hash, float transition = 0, bool restart = true);
    api::EffectControlResult SetAnimatorState(VisualEffectId, const std::string& state, float transition = 0,
                                              bool restart = true);
    api::EffectControlResult SetAnimatorTrigger(VisualEffectId, const std::string& name, bool reset = false);
    api::EffectControlResult SetAnimatorParameter(VisualEffectId, const api::EffectAnimatorParameter&);
    api::EffectControlResult OperateLayout(api::EffectLayouts&, VisualEffectId source, VisualEffectId target,
                                           api::EffectLayoutCommand);
    void Remove(VisualEffectId, api::EffectRemovalOptions);
    void UseEntityMotion(std::shared_ptr<VisualEntityMotion>, api::EntityOrigin);
    api::EffectControlResult BindTargets(VisualEffectId, const std::vector<api::EntityOrigin>&);
    const api::EffectRequestStatus* Status(VisualEffectId) const; // borrowed until Service
    bool Closing() const {
        return closing_;
    }
    bool Finishing() const {
        return finishing_;
    }
    bool Drained() const {
        return (finishing_ || closing_) && requests_.empty() && producers_ == 0;
    }
    std::size_t PendingRequests() const {
        return requests_.size();
    }

  private:
    friend class VisualEffectBindings;
    friend class VisualContextWork;
    std::size_t producers_ = 0;
    struct Entry {
        std::string key;
        std::unique_ptr<api::OwnedEffectRequest> request;
        bool retained = true;
        void* manager = nullptr;
    };
    std::vector<VisualEffectId> Matching(const std::string&) const;
    std::shared_ptr<const api::PresentationEffects> services_;
    std::shared_ptr<VisualEntityMotion> motion_;
    api::EntityOrigin owner_;
    std::map<VisualEffectId, Entry> requests_;
    VisualEffectId next_ = 0;
    bool finishing_ = false, closing_ = false, servicing_ = false, end_fade_ = true;
    unsigned operations_ = 0;
};
// Keeps native work in the context's drain contract. Destruction only releases
// the bookkeeping hold; the owner must first finish its native cleanup.
class VisualContextWork {
  public:
    explicit VisualContextWork(std::shared_ptr<VisualEffectContext>);
    ~VisualContextWork();
    VisualContextWork(const VisualContextWork&) = delete;
    VisualContextWork& operator=(const VisualContextWork&) = delete;

  private:
    std::shared_ptr<VisualEffectContext> context_;
};
}
