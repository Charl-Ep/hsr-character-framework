#pragma once
#include "HitRequests.hpp"
#include <functional>

namespace hksr::framework {
struct HitInput {std::string animation;};
struct HitResolution {bool ready=false;std::optional<HitCommand> command;};
using HitResolver=std::function<HitResolution(const HitInput&)>;
struct HitDeliveryStatus {
    std::uint64_t serial=0,entity_generation=0;
    bool delivered=false,ignored=false,cancelled=false;
    std::string error;
    std::shared_ptr<const HitRequestStatus> playback;
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
    void Service(const HitResolver&,const std::function<std::shared_ptr<const HitRequestStatus>(HitCommand)>&);
    void CancelPending();
    void Close();
private:
    struct Entry;
    std::vector<std::shared_ptr<Entry>> pending_;
    std::uint64_t owner_,next_=0;
    bool closed_=false,servicing_=false;
};
}
