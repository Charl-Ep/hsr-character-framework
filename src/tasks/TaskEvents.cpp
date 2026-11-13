#include <hksr/framework/tasks/TaskEvents.hpp>
#include <array>
#include <algorithm>
#include <stdexcept>

namespace hksr::framework {
namespace {
constexpr std::array<std::pair<TaskEventKind, std::string_view>, 12> names{
    {{TaskEventKind::AfterHit, "after_hit"},
     {TaskEventKind::AfterHitAll, "after_hit_all"},
     {TaskEventKind::AfterHitMiss, "after_hit_miss"},
     {TaskEventKind::AfterAttackedAdv, "after_attacked_adv"},
     {TaskEventKind::AfterAttack, "after_attack"},
     {TaskEventKind::AfterAttacked, "after_attacked"},
     {TaskEventKind::AfterAttackEnd, "after_attack_end"},
     {TaskEventKind::AfterAttackedEnd, "after_attacked_end"},
     {TaskEventKind::AfterDamageChunk, "after_damage_chunk"},
     {TaskEventKind::AfterBeingDamageChunk, "after_being_damage_chunk"},
     {TaskEventKind::BeingLimbo, "being_limbo"},
     {TaskEventKind::BeforeDying, "before_dying"}}};
}
TaskEventKind ParseTaskEvent(std::string_view name) {
    for (const auto& item : names)
        if (item.second == name)
            return item.first;
    throw std::invalid_argument("unknown visual event: " + std::string(name));
}
std::string_view TaskEventName(TaskEventKind kind) {
    for (const auto& item : names)
        if (item.first == kind)
            return item.second;
    throw std::invalid_argument("unknown visual event kind");
}
TaskRequestState TaskEventReceipt::State() const {
    TaskRequestState result;
    result.submitted = result.request_complete = true;
    result.performance_complete = result.drained = true;
    for (const auto& callback : callbacks) {
        const auto& state = callback->state;
        result.started = result.started || state.started;
        result.performance_complete = result.performance_complete && state.performance_complete;
        result.drained = result.drained && state.drained;
        result.superseded = result.superseded || state.superseded;
        if (result.error.empty())
            result.error = state.error;
    }
    return result;
}
void TaskAttackHistory::Record(std::uint64_t receiver, Emit emit) {
    if (phase_ == Phase::Idle)
        phase_ = Phase::Active;
    const auto append = [&](auto& list) {
        if (receiver && std::none_of(list.begin(), list.end(), [&](const auto& row) { return row.id == receiver; }))
            list.push_back({receiver, emit});
    };
    if (phase_ == Phase::Active)
        append(receivers_);
    if (chunk_)
        append(chunk_receivers_);
}
std::shared_ptr<const TaskEventReceipt> TaskAttackHistory::Boundary(TaskAttackBoundary boundary, Emit sender) {
    auto receipt = std::make_shared<TaskEventReceipt>();
    const auto send = [&](const Emit& emit, TaskEventKind kind) {
        try {
            if (!emit)
                throw std::logic_error("attack event sender unavailable");
            const auto result = emit(kind);
            if (!result)
                throw std::logic_error("attack event returned no receipt");
            receipt->callbacks.insert(receipt->callbacks.end(), result->callbacks.begin(), result->callbacks.end());
        } catch (const std::exception& error) {
            auto failed = std::make_shared<TaskLifetimeReceipt>();
            failed->state.submitted = failed->state.request_complete = failed->state.drained = true;
            failed->state.error = error.what();
            receipt->callbacks.push_back(std::move(failed));
        }
    };
    const auto finish = [&] {
        if (phase_ != Phase::Active)
            return;
        phase_ = Phase::Finished;
        const auto targets = receivers_;
        for (const auto& target : targets)
            send(target.emit, TaskEventKind::AfterAttackedAdv);
        send(sender, TaskEventKind::AfterAttack);
        for (const auto& target : targets)
            send(target.emit, TaskEventKind::AfterAttacked);
    };
    switch (boundary) {
    case TaskAttackBoundary::Record:
        throw std::invalid_argument("attack record requires a target");
    case TaskAttackBoundary::Finish:
        finish();
        break;
    case TaskAttackBoundary::Complete:
        finish();
        if (phase_ == Phase::Finished) {
            const auto targets = std::move(receivers_);
            receivers_.clear();
            phase_ = Phase::Idle;
            send(sender, TaskEventKind::AfterAttackEnd);
            for (const auto& target : targets)
                send(target.emit, TaskEventKind::AfterAttackedEnd);
        }
        break;
    case TaskAttackBoundary::BeginChunk:
        chunk_ = true;
        chunk_receivers_.clear();
        break;
    case TaskAttackBoundary::EndChunk: {
        const auto targets = std::move(chunk_receivers_);
        chunk_receivers_.clear();
        chunk_ = false;
        send(sender, TaskEventKind::AfterDamageChunk);
        for (const auto& target : targets)
            send(target.emit, TaskEventKind::AfterBeingDamageChunk);
        break;
    }
    }
    return receipt;
}
}
