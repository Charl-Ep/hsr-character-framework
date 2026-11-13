#include <hksr/framework/presentation/timing/ActorTimingScopes.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>

namespace hksr::framework {
ActorTimingScopes::ActorTimingScopes(api::ActorTiming& timing) : timing_(timing) {
    static std::atomic<std::uint64_t> next{0};
    prefix_ = "HKSR.Timing." + std::to_string(++next) + ".";
}
void ActorTimingScopes::Set(std::uint64_t scope, const std::string& name, float value, float duration, bool speed) {
    if (!scope)
        throw std::invalid_argument("timing contribution requires a scope");
    const auto current = speed ? timing_.Speed() : timing_.Layer(name);
    if (!current)
        throw std::runtime_error(speed ? "owned animator lock component unavailable"
                                       : "owned layer tween component unavailable");
    if (!std::isfinite(current->value) || (speed && current->value < 0 && current->value != -1))
        throw std::runtime_error("owned actor timing baseline is invalid");
    const auto key = speed ? std::string() : name;
    auto found = slots_.find(key);
    if (found != slots_.end() &&
        (!(found->second.model == current->model) || (speed && current->value != found->second.last))) {
        slots_.erase(found);
        found = slots_.end();
    }
    if (found == slots_.end())
        found = slots_.emplace(key, Slot{current->model, current->value, current->value, {}}).first;
    auto& slot = found->second;
    slot.values.erase(
        std::remove_if(slot.values.begin(), slot.values.end(), [&](const auto& row) { return row.scope == scope; }),
        slot.values.end());
    // Retain the restoration obligation before a native call that may partially write.
    slot.values.push_back({scope, value});
    slot.last = value;
    const auto applied =
        speed ? timing_.SetSpeed(slot.model, value) : timing_.SetLayer(slot.model, name, value, duration);
    if (!applied)
        throw std::runtime_error("owned actor timing write failed");
}
void ActorTimingScopes::Layer(std::uint64_t scope, const std::string& name, float value, float duration) {
    Set(scope, name, value, duration, false);
}
void ActorTimingScopes::Speed(std::uint64_t scope, float value) {
    Set(scope, {}, value, 0, true);
}
void ActorTimingScopes::Scale(std::uint64_t scope, const std::string& key, const api::ActorTimeScaleInput& input) {
    if (!scope || key.empty())
        throw std::invalid_argument("actor clock requires scope and key");
    if (ClearScale(scope, key, false) != TimingClearState::Done)
        throw std::runtime_error("previous actor clock could not retire");
    const auto token = timing_.Scale(input, prefix_ + std::to_string(scope) + "." + key);
    if (!token.value)
        throw std::runtime_error("owned actor time scale was not accepted");
    clocks_.push_back({scope, key, token, input.duration.has_value(), false});
}
TimingClearState ActorTimingScopes::ClearScale(std::uint64_t scope, const std::string& key, bool fade) {
    auto result = TimingClearState::Done;
    for (auto it = clocks_.begin(); it != clocks_.end();) {
        if (it->scope != scope || it->key != key) {
            ++it;
            continue;
        }
        if ((!it->fading || !fade) && !timing_.Remove(it->token, fade)) {
            result = TimingClearState::Failed;
            ++it;
            continue;
        }
        if (fade) {
            it->fading = true;
            const auto remains = timing_.Contains(it->token);
            if (!remains) {
                result = TimingClearState::Failed;
                ++it;
                continue;
            }
            if (*remains) {
                if (result == TimingClearState::Done)
                    result = TimingClearState::Pending;
                ++it;
                continue;
            }
            if (!timing_.Remove(it->token, false)) {
                result = TimingClearState::Failed;
                ++it;
                continue;
            }
        }
        it = clocks_.erase(it);
    }
    return result;
}
TimingClearState ActorTimingScopes::ClearFinite(std::uint64_t scope, bool fade) {
    std::vector<std::string> keys;
    for (const auto& clock : clocks_)
        if (clock.scope == scope && clock.finite)
            keys.push_back(clock.key);
    auto result = TimingClearState::Done;
    for (const auto& key : keys) {
        const auto value = ClearScale(scope, key, fade);
        if (value == TimingClearState::Failed ||
            (value == TimingClearState::Pending && result == TimingClearState::Done))
            result = value;
    }
    return result;
}
bool ActorTimingScopes::Release(std::uint64_t scope, bool natural) {
    error_.clear();
    bool complete = true;
    for (auto it = slots_.begin(); it != slots_.end();) {
        auto& slot = it->second;
        const auto found =
            std::find_if(slot.values.begin(), slot.values.end(), [&](const auto& row) { return row.scope == scope; });
        if (found == slot.values.end()) {
            ++it;
            continue;
        }
        const auto current = it->first.empty() ? timing_.Speed() : timing_.Layer(it->first);
        if (!current || !(current->model == slot.model) || (it->first.empty() && current->value != slot.last)) {
            it = slots_.erase(it);
            continue;
        }
        auto remaining = slot.values;
        remaining.erase(remaining.begin() + std::distance(slot.values.begin(), found));
        const auto desired = remaining.empty() ? slot.baseline : remaining.back().value;
        if (found == std::prev(slot.values.end())) {
            const auto restored = it->first.empty() ? timing_.SetSpeed(slot.model, desired)
                                                    : timing_.SetLayer(slot.model, it->first, desired, 0);
            if (!restored) {
                error_ = "owned actor timing restoration failed";
                complete = false;
                ++it;
                continue;
            }
            slot.last = desired;
        }
        slot.values = std::move(remaining);
        if (slot.values.empty())
            it = slots_.erase(it);
        else
            ++it;
    }
    for (auto it = clocks_.begin(); it != clocks_.end();) {
        if (it->scope != scope) {
            ++it;
            continue;
        }
        if (natural && it->finite) {
            const auto remains = timing_.Contains(it->token);
            if (!remains || *remains) {
                if (!remains)
                    error_ = "owned actor clock status unavailable";
                complete = false;
                ++it;
                continue;
            }
        }
        if (timing_.Remove(it->token, false))
            it = clocks_.erase(it);
        else {
            error_ = "owned actor clock cancellation failed";
            complete = false;
            ++it;
        }
    }
    return complete;
}
bool ActorTimingScopes::Close() {
    std::vector<std::uint64_t> scopes;
    for (const auto& [name, slot] : slots_) {
        (void)name;
        for (const auto& value : slot.values)
            scopes.push_back(value.scope);
    }
    for (const auto& clock : clocks_)
        scopes.push_back(clock.scope);
    bool complete = true;
    for (const auto scope : scopes)
        if (!Release(scope))
            complete = false;
    return complete;
}
}
