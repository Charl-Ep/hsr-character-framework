#include <hksr/framework/presentation/HitInbox.hpp>
#include <algorithm>
#include <stdexcept>

namespace hksr::framework {
struct HitInbox::Entry {
    HitInput input;
    std::shared_ptr<HitDeliveryStatus> status;
    bool committing=false,admitted=false;
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
        if(const auto current=weak.lock();current && !current->committing && !current->admitted)current->status->cancelled=true;
    }};
}
void HitInbox::CancelPending() {
    for(const auto& entry:pending_)if(!entry->committing && !entry->status->delivered)entry->status->cancelled=true;
}
void HitInbox::Close() {
    closed_=true;CancelPending();
}
void HitInbox::Service(const HitResolver& resolve,const std::function<std::shared_ptr<const HitRequestStatus>(HitCommand)>& submit,
    const HitEffectReceiver& effects) {
    if(servicing_)return;
    servicing_=true;struct Guard{bool& value;~Guard(){value=false;}} guard{servicing_};
    const auto snapshot=pending_;
    for(const auto& entry:snapshot) {
        auto& status=*entry->status;
        if(status.cancelled || status.delivered)continue;
        // Commit each channel at most once. Effect acceptance does not depend
        // on a reaction name, Animator readiness or its repeat-hit throttle.
        if(!status.effects_delivered) {
            if(!entry->input.effect)status.effects_delivered=status.effects_ignored=true;
            else {
                entry->committing=true;
                try {
                    if(!effects)throw std::runtime_error("receiver hit effects are not configured");
                    const auto value=effects(entry->input);
                    if(value.ready) {
                        status.effects_delivered=true;status.effects_ignored=value.ignored;status.effect_errors=value.errors;
                        entry->admitted=true;
                    }
                }catch(const std::exception& e){status.effects_delivered=true;status.effect_errors.push_back(e.what());entry->admitted=true;}
                entry->committing=false;
            }
        }
        if(!status.animation_delivered && !status.cancelled && !closed_) {
        try {
            if(!resolve || !submit)throw std::runtime_error("receiver hit policy is not configured");
            const auto value=resolve(entry->input);
            if(status.cancelled || closed_ || !value.ready)continue;
            entry->committing=true;
            if(value.command) {
                status.playback=submit(*value.command);
                if(!status.playback || status.playback->entity_generation!=owner_)throw std::runtime_error("hit policy returned another receiver");
            }else status.ignored=true;
            status.animation_delivered=true;entry->admitted=true;
        }catch(const std::exception& error){status.error=error.what();status.animation_delivered=true;entry->admitted=true;}
        entry->committing=false;
        }
        status.delivered=status.animation_delivered && status.effects_delivered;
        if(closed_ && !status.delivered)status.cancelled=true;
        if(status.delivered && status.error.empty() && !status.effect_errors.empty())status.error=status.effect_errors.front();
    }
    pending_.erase(std::remove_if(pending_.begin(),pending_.end(),[](const auto& entry) {
        return entry->status->cancelled || entry->status->delivered;
    }),pending_.end());
}
}
