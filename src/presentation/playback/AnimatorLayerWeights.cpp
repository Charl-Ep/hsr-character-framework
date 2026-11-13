#include <hksr/framework/presentation/playback/AnimatorLayerWeights.hpp>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>

namespace hksr::framework {
AnimatorLayerWeights::AnimatorLayerWeights(LayerWeightServices services) : services_(std::move(services)) {}
bool AnimatorLayerWeights::Current(const LayerWeightModel& model) const {
    const auto current = services_.current();
    return current && *current == model;
}
void AnimatorLayerWeights::Set(std::uint64_t scope, const std::string& layer, float weight) {
    if (!scope || layer.empty() || layer.find('\0') != std::string::npos || !std::isfinite(weight))
        throw std::invalid_argument("invalid layer weight contribution");
    const auto model = services_.current();
    if (!model || !model->animator)
        throw std::runtime_error("owned Animator unavailable");
    const auto index = services_.index(model->animator, layer);
    if (!Current(*model))
        throw std::runtime_error("layer weight model changed");
    if (index < 0)
        throw std::runtime_error("Animator layer unavailable: " + layer);
    const auto baseline = services_.read(model->animator, index);
    if (!Current(*model))
        throw std::runtime_error("layer weight model changed");
    if (!std::isfinite(baseline))
        throw std::runtime_error("Animator layer weight unread");
    auto found = std::find_if(entries_.begin(), entries_.end(),
                              [&](const auto& e) { return e.model == *model && e.index == index; });
    if (found == entries_.end()) {
        entries_.push_back({*model, index, baseline, baseline, {}});
        found = std::prev(entries_.end());
    } else if (baseline != found->last) {
        // An intervening writer superseded every old contribution to this slot.
        found->baseline = baseline;
        found->contributions.clear();
    }
    auto& contributions = found->contributions;
    contributions.erase(
        std::remove_if(contributions.begin(), contributions.end(), [&](const auto& c) { return c.scope == scope; }),
        contributions.end());
    contributions.push_back({scope, weight});
    found->pending_restore.reset();
    // Keep ownership before calling: a native failure can follow a partial write.
    found->last = weight;
    services_.write(model->animator, index, weight);
}
bool AnimatorLayerWeights::Release(std::uint64_t scope) {
    error_.clear();
    bool complete = true;
    for (auto it = entries_.begin(); it != entries_.end();) {
        auto& entry = *it;
        const auto found = std::find_if(entry.contributions.begin(), entry.contributions.end(),
                                        [&](const auto& c) { return c.scope == scope; });
        if (found == entry.contributions.end()) {
            ++it;
            continue;
        }
        try {
            if (!Current(entry.model)) {
                it = entries_.erase(it);
                continue;
            }
            const auto actual = services_.read(entry.model.animator, entry.index);
            if (!Current(entry.model)) {
                it = entries_.erase(it);
                continue;
            }
            if (!std::isfinite(actual))
                throw std::runtime_error("Animator layer weight unread during release");
            if (actual != entry.last && (!entry.pending_restore || actual != *entry.pending_restore)) {
                it = entries_.erase(it);
                continue;
            }
            auto remaining = entry.contributions;
            remaining.erase(remaining.begin() + std::distance(entry.contributions.begin(), found));
            const auto desired = remaining.empty() ? entry.baseline : remaining.back().value;
            if (desired != actual) {
                entry.pending_restore = desired;
                services_.write(entry.model.animator, entry.index, desired);
            }
            entry.last = desired;
            entry.contributions = std::move(remaining);
            entry.pending_restore.reset();
            if (entry.contributions.empty()) {
                it = entries_.erase(it);
                continue;
            }
        } catch (const std::exception& e) {
            error_ = e.what();
            complete = false;
        }
        ++it;
    }
    return complete;
}
bool AnimatorLayerWeights::Close() {
    std::vector<std::uint64_t> scopes;
    for (const auto& entry : entries_)
        for (const auto& c : entry.contributions)
            scopes.push_back(c.scope);
    bool complete = true;
    std::string error;
    for (const auto scope : scopes)
        if (!Release(scope)) {
            complete = false;
            error = error_;
        }
    if (!complete)
        error_ = std::move(error);
    return complete;
}
}
