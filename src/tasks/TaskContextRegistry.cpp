#include <hksr/framework/tasks/TaskContextRegistry.hpp>
#include <algorithm>
#include <stdexcept>

namespace hksr::framework {
TaskContextEndpoint::TaskContextEndpoint(std::shared_ptr<const TaskCatalog> catalog) : catalog_(std::move(catalog)) {}
void TaskContextEndpoint::Reset(bool close) {
    ++epoch_;
    context_ = TaskContext{};
    events_.clear();
    if (close)
        active_ = false;
}
TaskContextToken TaskContextEndpoint::Apply(const std::string& name, const TaskContextDefinition& definition,
                                            const std::string& source_catalog) {
    if (!active_)
        throw std::logic_error("task receiver closed");
    const auto token = context_.Enter(name, source_catalog.empty() ? catalog_->id : source_catalog);
    try {
        for (const auto& [key, value] : definition.values)
            context_.SetValue(token, key, value);
        context_.SetPhase(token, definition.phase);
        context_.SetHitEffects(token, definition.hit_effects);
        context_.SetResilience(token, definition.resilience);
        context_.SetVisualReaction(token, definition.visual_reaction);
    } catch (...) {
        context_.Exit(token);
        throw;
    }
    return token;
}
void TaskContextEndpoint::Remove(std::uint64_t epoch, TaskContextToken token) {
    if (epoch == epoch_)
        context_.Exit(token);
}
void TaskContextEndpoint::Subscribe(const std::shared_ptr<TaskEventSubscription>& subscription) {
    events_.erase(std::remove_if(events_.begin(), events_.end(), [](const auto& item) { return item.expired(); }),
                  events_.end());
    events_.push_back(subscription);
}
std::shared_ptr<const TaskEventReceipt> TaskContextEndpoint::Dispatch(const TaskEventInput& input) {
    auto receipt = std::make_shared<TaskEventReceipt>();
    const auto snapshot = events_;
    for (const auto& weak : snapshot)
        if (const auto subscription = weak.lock()) {
            if (!input.current())
                break;
            try {
                if (auto callback = subscription->receive(input))
                    receipt->callbacks.push_back(std::move(callback));
            } catch (const std::exception& error) {
                // One invalid projection does not suppress independent subscribers.
                auto failed = std::make_shared<TaskLifetimeReceipt>();
                failed->state.error = error.what();
                failed->state.drained = true;
                receipt->callbacks.push_back(std::move(failed));
            }
        }
    return receipt;
}
void TaskContextRegistry::Register(std::uint64_t character, const std::shared_ptr<TaskContextEndpoint>& endpoint) {
    if (!character || !endpoint)
        throw std::invalid_argument("task receiver requires a character identity");
    if (const auto old = Find(character); old && old != endpoint)
        old->Reset(true);
    receivers_[character] = endpoint;
}
std::shared_ptr<TaskContextEndpoint> TaskContextRegistry::Find(std::uint64_t character) const {
    const auto found = receivers_.find(character);
    if (found == receivers_.end())
        return {};
    auto endpoint = found->second.lock();
    return endpoint && endpoint->Active() ? endpoint : nullptr;
}
std::shared_ptr<const TaskEventReceipt> TaskContextRegistry::Dispatch(TaskEventKind kind, std::uint64_t recipient,
                                                                      std::uint64_t source) const {
    const auto receiver = Find(recipient), caster = Find(source);
    if (!receiver || !caster)
        throw std::invalid_argument("visual event requires owned source and receiver");
    const auto receiver_epoch = receiver->Epoch(), caster_epoch = caster->Epoch();
    TaskEventInput input{kind, recipient, source, [receiver, caster, receiver_epoch, caster_epoch] {
                             return receiver->Active() && caster->Active() && receiver->Epoch() == receiver_epoch &&
                                    caster->Epoch() == caster_epoch;
                         }};
    return receiver->Dispatch(input);
}
}
