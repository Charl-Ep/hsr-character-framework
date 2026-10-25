#include <hksr/framework/presentation/HitInbox.hpp>
#include <algorithm>
#include <stdexcept>

namespace hksr::framework {
struct HitInbox::Entry {
    HitInput input;
    std::shared_ptr<HitDeliveryStatus> status;
    bool committing=false;
};
HitInbox::HitInbox(std::uint64_t owner):owner_(owner) {
    if(!owner)throw std::invalid_argument("hit inbox requires an owned generation");
}
HitInbox::~HitInbox(){Close();}
HitDelivery HitInbox::Post(HitInput input) {
    if(closed_)throw std::logic_error("hit receiver closed");
    if(input.animation.find('\0')!=std::string::npos)throw std::invalid_argument("invalid source hit animation");
    auto entry=std::make_shared<Entry>();entry->input=std::move(input);entry->status=std::make_shared<HitDeliveryStatus>();
    entry->status->serial=++next_;entry->status->entity_generation=owner_;pending_.push_back(entry);
    return {entry->status,[weak=std::weak_ptr<Entry>(entry)] {
        if(const auto current=weak.lock();current && !current->committing && !current->status->delivered)current->status->cancelled=true;
    }};
}
void HitInbox::CancelPending() {
    for(const auto& entry:pending_)if(!entry->committing && !entry->status->delivered)entry->status->cancelled=true;
}
void HitInbox::Close() {
    closed_=true;CancelPending();
}
void HitInbox::Service(const HitResolver& resolve,const std::function<std::shared_ptr<const HitRequestStatus>(HitCommand)>& submit) {
    if(servicing_)return;
    servicing_=true;struct Guard{bool& value;~Guard(){value=false;}} guard{servicing_};
    const auto snapshot=pending_;
    for(const auto& entry:snapshot) {
        auto& status=*entry->status;
        if(status.cancelled || status.delivered || !status.error.empty())continue;
        try {
            if(!resolve || !submit)throw std::runtime_error("receiver hit policy is not configured");
            const auto value=resolve(entry->input);
            if(status.cancelled || closed_ || !value.ready)continue;
            entry->committing=true;
            if(value.command) {
                status.playback=submit(*value.command);
                if(!status.playback || status.playback->entity_generation!=owner_)throw std::runtime_error("hit policy returned another receiver");
            }else status.ignored=true;
            status.delivered=true;
        }catch(const std::exception& error){status.error=error.what();}
        entry->committing=false;
    }
    pending_.erase(std::remove_if(pending_.begin(),pending_.end(),[](const auto& entry) {
        return entry->status->cancelled || entry->status->delivered || !entry->status->error.empty();
    }),pending_.end());
}
}
