#pragma once
#include "HitRequests.hpp"
#include <functional>
#include <hksr/entity/EntityLifecycle.hpp>

namespace hksr::framework {
struct HitAngles {float horizontal=0,vertical=0;};
struct HitOrigin {api::Position3 position;bool self=false;api::EntityOrigin caster;};
struct HitEffectInput {
    std::string path;
    float height=-1,angle=0,delay=0,minimum_interval=0;
    int maximum_count=0;
    bool offset_by_scale=false;
};
struct HitInput {
    std::string animation;
    std::optional<HitAngles> direction;
    std::optional<HitOrigin> origin; // captured by the sending scene, not authored coordinates
    std::optional<HitEffectInput> effect;
};
struct HitResolution {bool ready=false;std::optional<HitCommand> command;};
using HitResolver=std::function<HitResolution(const HitInput&)>;
struct HitEffectAdmission {bool ready=false,ignored=false;std::vector<std::string> errors;};
using HitEffectReceiver=std::function<HitEffectAdmission(const HitInput&)>;
struct HitDeliveryStatus {
    std::uint64_t serial=0,entity_generation=0;
    bool delivered=false,ignored=false,cancelled=false;
    std::string error;
    std::shared_ptr<const HitRequestStatus> playback;
    bool animation_delivered=false,effects_delivered=false,effects_ignored=false;
    std::vector<std::string> effect_errors;
};
struct HitDelivery {
    std::shared_ptr<const HitDeliveryStatus> status;
    std::function<void()> cancel; // only before receiver admission
};
// A generation-bound value mailbox. Posting does not call the receiver, mutate
// an Animator or transfer ownership of a delivered hit back to the attacker.
class HitInbox {
public:
    explicit HitInbox(std::uint64_t entity_generation);
    ~HitInbox(); // closes pending deliveries without native calls
    HitDelivery Post(HitInput);
    std::uint64_t Generation() const {return owner_;}
    void Service(const HitResolver&,const std::function<std::shared_ptr<const HitRequestStatus>(HitCommand)>&,
        const HitEffectReceiver& effects={});
    void CancelPending();
    void Close();
private:
    struct Entry;
    std::vector<std::shared_ptr<Entry>> pending_;
    std::uint64_t owner_,next_=0;
    bool closed_=false,servicing_=false;
};
}
