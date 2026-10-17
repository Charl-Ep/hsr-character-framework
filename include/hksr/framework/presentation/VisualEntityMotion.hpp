#pragma once
#include <hksr/entity/EntityLifecycle.hpp>
#include <hksr/presentation/effects/EffectEntityFollow.hpp>
#include <functional>
#include <memory>

namespace hksr::framework {
struct VisualEntityMotionServices {
    std::function<api::EffectEntityFollowObservation(api::EffectReference)> read;
    // The SDK rechecks this non-mutating lifetime predicate immediately before
    // the native write, after allocations and input preparation.
    std::function<api::EffectEntityFollowObservation(api::EffectReference,const std::vector<void*>&,const std::vector<void*>&,std::function<bool()>)> set;
    std::function<api::EffectMotionTargetObservation(void*)> target;
    // Must establish this client's ownership, owner world and model readiness.
    std::function<bool(api::EntityOrigin)> eligible;
    std::function<void(const std::string&)> trace;
};
// One host-wide arbitration table, shared by all visual contexts. A target's
// single official attached-motion accumulator cannot have concurrent owners.
// The context keeps each effect request retained until Release succeeds.
class VisualEntityMotion {
public:
    VisualEntityMotion(api::EntityLifecycle&,VisualEntityMotionServices);
    ~VisualEntityMotion(); // disconnects callbacks; no engine cleanup
    api::EffectControlResult Bind(api::EffectReference,api::EntityOrigin owner,const std::vector<api::EntityOrigin>&);
    api::EffectControlResult Release(api::EffectReference);
    // OwnedEffectRequest observed release/pool return. No access through the
    // retired request pointer; the official plugin's Dispose performs Clear.
    void RequestEnded(api::EffectReference);
    bool Contains(api::EffectReference) const;
    bool Empty() const;
private:
    struct State;std::shared_ptr<State> state_;
};
}
