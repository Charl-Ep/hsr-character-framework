#include <hksr/framework/presentation/audio/ActorBgmScopes.hpp>
#include <map>
#include <stdexcept>
namespace hksr::framework {
struct ActorBgmScopes::State {
    std::unique_ptr<api::ActorBgmSync> api;
    std::map<std::uint64_t, std::vector<api::BgmSyncToken>> scopes;
    bool closing = false, closed = false;
    std::string error;
    explicit State(std::unique_ptr<api::ActorBgmSync> value) : api(std::move(value)) {
        if (!api)
            throw std::invalid_argument("owned BGM sync client required");
    }
    bool Prune() {
        bool good = true;
        for (auto scope = scopes.begin(); scope != scopes.end();) {
            for (auto token = scope->second.begin(); token != scope->second.end();)
                try {
                    const auto status = api->Observe(*token);
                    if (status.drained)
                        token = scope->second.erase(token);
                    else {
                        if (!status.error.empty()) {
                            error = status.error;
                            good = false;
                        }
                        ++token;
                    }
                } catch (const std::exception& e) {
                    error = e.what();
                    good = false;
                    ++token;
                }
            if (scope->second.empty())
                scope = scopes.erase(scope);
            else
                ++scope;
        }
        if (good)
            error.clear();
        return good;
    }
    bool Release(std::optional<std::uint64_t> scope) {
        bool done = true;
        for (auto it = scopes.begin(); it != scopes.end();) {
            if (scope && it->first != *scope) {
                ++it;
                continue;
            }
            for (auto token = it->second.begin(); token != it->second.end();)
                try {
                    if (api->Stop(*token))
                        token = it->second.erase(token);
                    else {
                        done = false;
                        const auto observation = api->Observe(*token);
                        if (!observation.error.empty())
                            error = observation.error;
                        ++token;
                    }
                } catch (const std::exception& e) {
                    error = e.what();
                    done = false;
                    ++token;
                }
            if (it->second.empty())
                it = scopes.erase(it);
            else
                ++it;
        }
        if (done)
            error.clear();
        return done;
    }
};
ActorBgmScopes::ActorBgmScopes(std::unique_ptr<api::ActorBgmSync> value)
    : state_(std::make_unique<State>(std::move(value))) {}
ActorBgmScopes::~ActorBgmScopes() = default;
VisualStepResult ActorBgmScopes::Execute(std::uint64_t scope, const VisualBgmSync& operation) {
    auto& s = *state_;
    if (!scope || s.closing)
        return {VisualStepState::Failed, "BGM sync scope closed or absent"};
    if (!operation.active) {
        if (s.Release(scope))
            return {VisualStepState::Done, {}};
        return {s.error.empty() ? VisualStepState::Pending : VisualStepState::Failed, s.error};
    }
    s.Prune();
    const auto result = s.api->Start(operation.config);
    if (result.token.value)
        s.scopes[scope].push_back(result.token); // also retain failed-start cleanup
    s.Prune();
    if (result.state == api::BgmSyncStartState::Pending)
        return {VisualStepState::Pending, result.detail};
    if (result.state == api::BgmSyncStartState::Unsupported)
        return {VisualStepState::Skipped, result.detail};
    if (result.state == api::BgmSyncStartState::Failed)
        return {VisualStepState::Failed, result.detail};
    return {VisualStepState::Done, {}};
}
bool ActorBgmScopes::Release(std::uint64_t scope) {
    return state_->Release(scope);
}
bool ActorBgmScopes::Service() {
    auto& s = *state_;
    try {
        if (!s.api->Current())
            return Close();
        return s.Prune();
    } catch (const std::exception& e) {
        s.error = e.what();
        return false;
    }
}
bool ActorBgmScopes::Current() const {
    return !state_->closing && state_->api->Current();
}
const std::string& ActorBgmScopes::Error() const {
    return state_->error;
}
bool ActorBgmScopes::Close() {
    auto& s = *state_;
    if (s.closed)
        return true;
    s.closing = true;
    if (!s.Release({}))
        return false;
    try {
        s.closed = s.api->Close();
        if (!s.closed)
            s.error = "BGM sync reference release pending";
    } catch (const std::exception& e) {
        s.error = e.what();
        return false;
    }
    return s.closed;
}
}
