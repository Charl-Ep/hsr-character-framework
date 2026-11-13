#include <hksr/framework/presentation/effects/ActorEventEffects.hpp>
#include <stdexcept>

namespace hksr::framework {
ActorEventEffects::ActorEventEffects(api::EntityOrigin owner, std::shared_ptr<const api::PresentationEffects> services)
    : owner_(owner), context_(std::make_shared<VisualEffectContext>(std::move(services))) {}
bool ActorEventEffects::Owns(api::EntityOrigin value) const {
    return owner_.token.entity == value.token.entity && owner_.token.generation == value.token.generation &&
           owner_.world == value.world;
}
bool ActorEventEffects::Accepts(const api::EffectSubmission& value) const {
    return Owns(value.origin) && value.trigger.serial;
}
bool ActorEventEffects::Track(const api::EffectSubmission& value, const std::string& path) {
    if (!Accepts(value) || !value.request.id || !value.request.parameters || path.empty())
        return false;
    pending_[value.serial] = context_->Track(path, value.manager, value.request);
    return true;
}
void ActorEventEffects::Returned(const api::EffectSubmission& value, bool returned) {
    if (!Owns(value.origin))
        return;
    const auto found = pending_.find(value.serial);
    if (found == pending_.end())
        return;
    if (!returned)
        context_->Remove(found->second, {true, true, false});
    pending_.erase(found);
}
VisualStepResult ActorEventEffects::Remove(const std::string& path, api::EffectRemovalOptions options) {
    if (path.empty())
        throw std::invalid_argument("native event removal path is empty");
    const auto requests = context_->Requests(path);
    for (const auto id : requests)
        context_->Remove(id, options);
    context_->Service(); // task boundary, never called from a producer observer
    for (const auto id : requests)
        if (const auto* state = context_->Status(id); state && !state->closed && !state->request_ended) {
            if (!state->removal_attempts)
                return {VisualStepState::Pending, "waiting for native event removal"};
            if (!state->removal.read.returned || !state->removal.value)
                return {VisualStepState::Failed, state->detail.empty() ? "native event removal failed" : state->detail};
        }
    return {VisualStepState::Done, {}};
}
void ActorEventEffects::Service(bool end) {
    if (end)
        context_->End();
    context_->Service();
}
bool ActorEventEffects::Drained() const {
    return pending_.empty() && context_->Drained();
}
}
