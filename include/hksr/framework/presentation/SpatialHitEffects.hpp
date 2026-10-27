#pragma once
#include "HitInbox.hpp"
#include "VisualProgramState.hpp"
#include <hksr/presentation/effects/OwnedEffectRequest.hpp>

namespace hksr::framework {
struct SpatialHitStatus {
    bool submitted=false,admitted=false,ignored=false,cancelled=false,closed=false;
    std::string error;
};
struct SpatialHitEndpoint {
    std::shared_ptr<const SpatialHitStatus> status;
    std::function<void()> cancel;
};
struct SpatialHitServices {
    std::function<std::unique_ptr<api::OwnedEffectRequest>()> create;
    std::function<void(api::OwnedEffectRequest&,const std::string&,const api::SpatialEffectOptions&)> start;
};
// A point has no receiving actor, model, hitbox or modifier overrides. Receipts
// belong to the sending scope and are serviced through cancellation and tails.
class SpatialHitEffects {
public:
    SpatialHitEffects(VisualSpatialAnchor,SpatialHitServices);
    SpatialHitEndpoint Submit(const HitInput&);
    void Service(float delta);
    void Cancel();
    bool Drained() const;
private:
    struct Entry;
    VisualSpatialAnchor point_;
    SpatialHitServices services_;
    std::vector<std::shared_ptr<Entry>> entries_;
    std::map<std::string,float> last_;
    float time_=0;
    bool closing_=false,servicing_=false;
};
}
