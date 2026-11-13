#pragma once
#include <hksr/framework/presentation/effects/VisualEffectContext.hpp>
#include <hksr/framework/presentation/scene/VisualStepResult.hpp>
#include <hksr/presentation/effects/EffectSubmissions.hpp>

namespace hksr::framework {
// Receipts of this creation's native animation-event effects not already owned
// by a declared program context. No manager enumeration or path-wide deletion.
class ActorEventEffects {
  public:
    ActorEventEffects(api::EntityOrigin, std::shared_ptr<const api::PresentationEffects>);
    bool Accepts(const api::EffectSubmission&) const;
    bool Track(const api::EffectSubmission&, const std::string& resolved_path);
    void Returned(const api::EffectSubmission&, bool);
    VisualStepResult Remove(const std::string& resolved_path, api::EffectRemovalOptions);
    void Service(bool end = false);
    bool Drained() const;

  private:
    bool Owns(api::EntityOrigin) const;
    api::EntityOrigin owner_;
    std::shared_ptr<VisualEffectContext> context_;
    std::map<std::uint64_t, VisualEffectId> pending_;
};
}
