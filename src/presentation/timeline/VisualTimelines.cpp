#include <hksr/framework/presentation/timeline/VisualTimelines.hpp>
#include <hksr/framework/presentation/scene/VisualProgramPlayer.hpp>
#include <algorithm>
#include <stdexcept>
#include <sstream>

namespace hksr::framework {
VisualTimelines::VisualTimelines(Factory factory, Trace trace, std::shared_ptr<SharedVisualServices> shared,
                                 Resource resource)
    : factory_(std::move(factory)), resource_(std::move(resource)), trace_(std::move(trace)),
      shared_(std::move(shared)) {
    if (!factory_)
        throw std::invalid_argument("Timeline factory missing");
}
VisualStepResult VisualTimelines::Execute(std::uint64_t scope, const VisualTimeline& input) {
    auto op = input;
    if (op.key.empty())
        return {VisualStepState::Failed, "Timeline key missing"};
    if (op.type != 0 || op.release_volume)
        return {VisualStepState::Failed, "only Common Timeline without task-level volume release is supported"};
    if (op.play && (op.path.empty() || op.path.find('\0') != std::string::npos || op.wrap_mode < 0 || op.wrap_mode > 2))
        return {VisualStepState::Failed, "invalid Common Timeline input"};
    const Key key{scope, op.key};
    const auto previous = current_.find(key);
    if (!op.play) {
        if (suppressed_.erase(key))
            return {VisualStepState::Done, {}};
        if (waiting_.erase(key))
            return {VisualStepState::Done, {}};
        if (previous == current_.end())
            return {VisualStepState::Failed, "no scene Timeline receipt: " + op.key};
        const auto request = previous->second;
        request->Cancel();
        request->Service();
        // Stop acceptance is not a pool return. Retain until Service drains.
        const auto& status = request->Status();
        if (status.drained)
            ReleaseClaim(*request);
        for (const auto& stop : status.stops)
            if (stop.exception || (!stop.returned && !status.detail.empty()))
                return {VisualStepState::Failed, status.detail};
        return {VisualStepState::Done, {}};
    }
    if (previous != current_.end()) {
        previous->second->Cancel();
        retired_.push_back({scope, previous->second});
        current_.erase(previous);
    }
    suppressed_.erase(key);
    if (resource_) {
        const auto path = resource_(op.path);
        if (!path) {
            waiting_.erase(key);
            suppressed_.insert(key);
            return {VisualStepState::Skipped, "Timeline resource suppressed by selected skin"};
        }
        op.path = *path;
    }
    SharedVisualServices::Claim claim = 0;
    if (op.shared_environment) {
        if (!shared_)
            return {VisualStepState::Failed, "shared Timeline environment service unavailable"};
        auto [pending, created] = waiting_.try_emplace(key, 0);
        if (created)
            pending->second = shared_->Identify();
        claim = pending->second;
        if (!shared_->Acquire(SharedVisualService::Environment, claim))
            return {VisualStepState::Pending, "waiting for shared Timeline environment"};
    }
    std::shared_ptr<api::OwnedTimeline> request;
    try {
        request = std::shared_ptr<api::OwnedTimeline>(factory_());
        if (!request)
            throw std::runtime_error("Timeline factory returned no request");
    } catch (...) {
        if (claim) {
            shared_->Release(SharedVisualService::Environment, claim);
            waiting_.erase(key);
        }
        throw;
    }
    if (claim && (!waiting_.count(key) || waiting_.at(key) != claim)) {
        shared_->Release(SharedVisualService::Environment, claim);
        return {VisualStepState::Skipped, "Timeline admission cancelled before start"};
    }
    waiting_.erase(key);
    if (claim)
        claims_[request.get()] = claim;
    current_[key] = request; // publish before native start can reenter cancellation
    request->Start(op.path, op.wrap_mode);
    request->Service();
    Report(std::to_string(scope) + ":" + op.key, *request);
    if (!request->Status().accepted)
        return {VisualStepState::Failed, request->Status().detail};
    return {VisualStepState::Done, {}};
}
void VisualTimelines::Cancel(std::uint64_t scope) {
    for (auto it = suppressed_.begin(); it != suppressed_.end();)
        if (it->first == scope)
            it = suppressed_.erase(it);
        else
            ++it;
    for (auto it = waiting_.begin(); it != waiting_.end();) {
        if (it->first.first == scope)
            it = waiting_.erase(it);
        else
            ++it;
    }
    for (auto it = current_.begin(); it != current_.end();) {
        if (it->first.first != scope) {
            ++it;
            continue;
        }
        it->second->Cancel();
        retired_.push_back({scope, it->second});
        it = current_.erase(it);
    }
}
void VisualTimelines::Service() {
    // Keep receipts stable through native callback reentry.
    std::vector<std::shared_ptr<api::OwnedTimeline>> requests;
    for (const auto& retired : retired_)
        requests.push_back(retired.request);
    for (const auto& [key, request] : current_) {
        (void)key;
        requests.push_back(request);
    }
    for (const auto& request : requests) {
        request->Service();
        const auto& detail = request->Status().detail;
        if (request->Status().drained)
            ReleaseClaim(*request);
        if (!detail.empty() && std::find(issues_.begin(), issues_.end(), detail) == issues_.end())
            issues_.push_back(detail);
    }
    for (const auto& [key, request] : current_)
        Report(std::to_string(key.first) + ":" + key.second, *request);
    for (const auto& retired : retired_)
        Report("retired", *retired.request);
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(),
                                  [this](const auto& r) {
                                      if (!r.request->Status().drained)
                                          return false;
                                      last_trace_.erase(r.request.get());
                                      return true;
                                  }),
                   retired_.end());
}
void VisualTimelines::ReleaseClaim(const api::OwnedTimeline& request) {
    const auto found = claims_.find(&request);
    if (found == claims_.end())
        return;
    shared_->Release(SharedVisualService::Environment, found->second);
    claims_.erase(found);
}
void VisualTimelines::Report(const std::string& key, const api::OwnedTimeline& request) {
    if (!trace_)
        return;
    const auto& value = request.Status();
    std::ostringstream text;
    text << "timeline key=" << key << " playback=" << value.start.playback << " accepted=" << value.accepted
         << " cancelled=" << value.cancelled << " drained=" << value.drained << " detail=" << value.detail;
    for (std::size_t i = 0; i < value.start.references.size(); ++i) {
        const auto& ref = value.start.references[i];
        text << " [registry=" << ref.registry << " record=" << ref.record << " borrow=" << ref.borrow;
        if (i < value.phases.size())
            text << " phase=" << static_cast<int>(value.phases[i]);
        text << "]";
    }
    auto& previous = last_trace_[&request];
    const auto next = text.str();
    if (previous != next) {
        previous = next;
        trace_(next);
    }
}
bool VisualTimelines::Drained() const {
    if (!waiting_.empty())
        return false;
    for (const auto& [key, request] : current_) {
        (void)key;
        if (!request->Status().drained)
            return false;
    }
    return retired_.empty();
}
bool VisualTimelines::Drained(std::uint64_t scope) const {
    for (const auto& [key, claim] : waiting_) {
        (void)claim;
        if (key.first == scope)
            return false;
    }
    for (const auto& [key, request] : current_)
        if (key.first == scope && !request->Status().drained)
            return false;
    for (const auto& retired : retired_)
        if (retired.scope == scope && !retired.request->Status().drained)
            return false;
    return true;
}
void VisualTimelines::Forget(std::uint64_t scope) {
    for (auto it = suppressed_.begin(); it != suppressed_.end();)
        if (it->first == scope)
            it = suppressed_.erase(it);
        else
            ++it;
    if (!Drained(scope))
        throw std::logic_error("Timeline scope is not drained");
    for (auto it = current_.begin(); it != current_.end();) {
        if (it->first.first != scope) {
            ++it;
            continue;
        }
        ReleaseClaim(*it->second);
        last_trace_.erase(it->second.get());
        it = current_.erase(it);
    }
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(),
                                  [&](const auto& value) {
                                      if (value.scope != scope)
                                          return false;
                                      last_trace_.erase(value.request.get());
                                      return true;
                                  }),
                   retired_.end());
}
void ConnectVisualTimelines(VisualProgramServices& services, std::shared_ptr<VisualTimelines> timelines) {
    if (!timelines)
        throw std::invalid_argument("Timeline scene missing");
    services.timeline = [timelines](std::uint64_t scope, const VisualTimeline& op) {
        return timelines->Execute(scope, op);
    };
    services.cancel_timelines = [timelines](std::uint64_t scope) { timelines->Cancel(scope); };
    services.timelines_drained = [timelines] {
        timelines->Service();
        return timelines->Drained();
    };
    services.timeline_scope_drained = [timelines](auto scope) { return timelines->Drained(scope); };
    services.forget_timelines = [timelines](auto scope) { timelines->Forget(scope); };
}
}
