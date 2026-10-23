#pragma once
#include <hksr/entity/EntityLifecycle.hpp>
#include <hksr/presentation/effects/EffectEntityFollow.hpp>
#include <hksr/presentation/movement/OwnedMovementHost.hpp>
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
    std::function<std::shared_ptr<api::OwnedMovementHost>(api::EntityOrigin)> acquire;
};
// One host-wide arbitration table, shared by all visual contexts. A target's
// single official attached-motion accumulator cannot have concurrent owners.
// Production hosts also acquire the SDK's exclusive movement lease, shared
// with animation movement. Pending preparation retains its reservation.
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
    void Collect();
    // Before this exact actor changes model/controller. Includes bindings
    // owned by other scenes that move it; a different generation is unrelated.
    bool ReleaseParticipant(api::EntityOrigin);
private:
    struct State;std::shared_ptr<State> state_;
};
}
