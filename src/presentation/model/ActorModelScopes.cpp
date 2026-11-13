#include <hksr/framework/presentation/model/ActorModelScopes.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <optional>
#include <vector>

namespace hksr::framework {
namespace {
bool Same(api::EntityOrigin a, api::EntityOrigin b) {
    return a.token.entity == b.token.entity && a.token.generation == b.token.generation && a.world == b.world;
}
template <class T> struct Contribution {
    std::uint64_t scope;
    T value;
};
template <class T> void Put(std::vector<Contribution<T>>& values, std::uint64_t scope, T value) {
    if (!scope)
        throw std::invalid_argument("model contribution requires scope");
    values.erase(std::remove_if(values.begin(), values.end(), [&](const auto& row) { return row.scope == scope; }),
                 values.end());
    values.push_back({scope, std::move(value)});
}
template <class T> bool Remove(std::vector<Contribution<T>>& values, std::optional<std::uint64_t> scope) {
    const auto before = values.size();
    values.erase(
        std::remove_if(values.begin(), values.end(), [&](const auto& row) { return !scope || row.scope == *scope; }),
        values.end());
    return before != values.size();
}
}
struct ActorModelScopes::State {
    std::unique_ptr<api::ActorModelPresentation> api;
    std::vector<Contribution<api::FaceMaterialTransition>> faces;
    std::vector<Contribution<float>> scales;
    std::vector<Contribution<api::EntityOrigin>> follows;
    std::optional<api::FaceMaterialValue> face_baseline, face_written;
    std::optional<api::EntityOrigin> follow_baseline, follow_written;
    std::string scale_key, error;
    bool closing = false, closed = false, face_uncertain = false;
    explicit State(std::unique_ptr<api::ActorModelPresentation> value) : api(std::move(value)) {
        if (!api)
            throw std::invalid_argument("owned model presentation required");
        static std::atomic<std::uint64_t> serial{0};
        scale_key = "HKSR.Framework.ModelScale." + std::to_string(++serial);
    }
    void Check() {
        if (closing || !api->Current())
            throw std::runtime_error("model contribution owner ended or replaced");
    }
    api::EntityOrigin Available(api::EntityOrigin target) {
        return target.token.entity && !api->TargetCurrent(target) ? api::EntityOrigin{} : target;
    }
    void Clear() {
        faces.clear();
        scales.clear();
        follows.clear();
        face_baseline.reset();
        face_written.reset();
        face_uncertain = false;
        follow_baseline.reset();
        follow_written.reset();
    }
    void WriteFace(api::FaceMaterialTransition value) {
        const auto prior = face_written;
        const bool uncertain = face_uncertain;
        face_written = value.value;
        face_uncertain = true;
        try {
            api->SetFace(value);
            face_uncertain = false;
        } catch (const api::ModelPresentationUnsupported&) {
            face_written = prior;
            face_uncertain = uncertain;
            throw;
        } catch (...) {
            try {
                face_written = api->Face();
                face_uncertain = false;
            } catch (...) {
            }
            throw;
        }
    }
    bool Release(std::optional<std::uint64_t> scope) {
        if (closed)
            return true;
        if (!api->Current()) {
            Clear();
            error.clear();
            return true;
        }
        bool complete = true;
        try {
            auto next = faces;
            if (Remove(next, scope)) {
                const auto actual = api->Face();
                if (face_uncertain) {
                    face_written = actual;
                    face_uncertain = false;
                }
                if (face_written && actual == *face_written) {
                    const auto desired =
                        next.empty() ? api::FaceMaterialTransition{*face_baseline, 0} : next.back().value;
                    WriteFace(desired);
                } else {
                    face_baseline = actual;
                    face_written.reset();
                }
                faces = std::move(next);
                if (faces.empty()) {
                    face_baseline.reset();
                    face_written.reset();
                    face_uncertain = false;
                }
            }
        } catch (const std::exception& e) {
            complete = false;
            error = e.what();
        }
        try {
            auto next = scales;
            if (Remove(next, scope)) {
                api->SetScaleFactor(scale_key, next.empty() ? 1 : next.back().value);
                scales = std::move(next);
            }
        } catch (const std::exception& e) {
            complete = false;
            error = e.what();
        }
        try {
            auto next = follows;
            if (Remove(next, scope)) {
                const auto actual = api->FollowTarget();
                if (follow_written && Same(actual, *follow_written)) {
                    const auto desired = Available(next.empty() ? *follow_baseline : next.back().value);
                    api->Follow(desired);
                    follow_written = desired;
                } else {
                    follow_baseline = actual;
                    follow_written.reset();
                }
                follows = std::move(next);
                if (follows.empty()) {
                    follow_baseline.reset();
                    follow_written.reset();
                }
            }
        } catch (const std::exception& e) {
            complete = false;
            error = e.what();
        }
        if (complete)
            error.clear();
        return complete;
    }
};
ActorModelScopes::ActorModelScopes(std::unique_ptr<api::ActorModelPresentation> value)
    : state_(std::make_unique<State>(std::move(value))) {}
ActorModelScopes::~ActorModelScopes() = default;
bool ActorModelScopes::Current() const {
    return !state_->closing && state_->api->Current();
}
const std::string& ActorModelScopes::Error() const {
    return state_->error;
}
void ActorModelScopes::Face(std::uint64_t scope, api::FaceMaterialTransition value) {
    auto& s = *state_;
    s.Check();
    const auto before = s.faces;
    const auto baseline = s.face_baseline, written = s.face_written;
    const bool uncertain = s.face_uncertain;
    const auto actual = s.api->Face();
    if (s.face_uncertain) {
        s.face_written = actual;
        s.face_uncertain = false;
    }
    if (!s.face_baseline || (s.face_written && !(actual == *s.face_written)))
        s.face_baseline = actual;
    Put(s.faces, scope, value);
    try {
        s.WriteFace(value);
    } catch (const api::ModelPresentationUnsupported&) {
        s.faces = before;
        s.face_baseline = baseline;
        s.face_written = written;
        s.face_uncertain = uncertain;
        throw;
    }
}
void ActorModelScopes::Scale(std::uint64_t scope, float value) {
    auto& s = *state_;
    s.Check();
    if (!std::isfinite(value))
        throw std::invalid_argument("invalid model scale factor");
    const auto before = s.scales;
    Put(s.scales, scope, value);
    try {
        s.api->SetScaleFactor(s.scale_key, value);
    } catch (const api::ModelPresentationUnsupported&) {
        s.scales = before;
        throw;
    }
}
void ActorModelScopes::Follow(std::uint64_t scope, api::EntityOrigin target) {
    auto& s = *state_;
    s.Check();
    const auto before = s.follows;
    const auto baseline = s.follow_baseline, written = s.follow_written;
    const auto actual = s.api->FollowTarget();
    if (!s.follow_baseline || (s.follow_written && !Same(actual, *s.follow_written)))
        s.follow_baseline = actual;
    if (target.token.entity && !s.api->TargetCurrent(target))
        throw std::runtime_error("model scale target is no longer current");
    Put(s.follows, scope, target);
    s.follow_written = target;
    try {
        s.api->Follow(target);
    } catch (const api::ModelPresentationUnsupported&) {
        s.follows = before;
        s.follow_baseline = baseline;
        s.follow_written = written;
        throw;
    }
}
void ActorModelScopes::Service() {
    auto& s = *state_;
    if (!s.api->Current()) {
        s.Clear();
        return;
    }
    if (s.follow_written && s.follow_written->token.entity && !s.api->TargetCurrent(*s.follow_written)) {
        // Target retirement ends this contribution; it never retargets a new
        // entity generation which happens to share the same Framework name.
        const auto next = s.Available(s.follow_baseline.value_or(api::EntityOrigin{}));
        s.api->Follow(next);
        s.follow_written = next;
    }
}
bool ActorModelScopes::Release(std::uint64_t scope) {
    return state_->Release(scope);
}
bool ActorModelScopes::Close() {
    auto& s = *state_;
    if (s.closed)
        return true;
    s.closing = true;
    if (!s.Release({}))
        return false;
    try {
        s.closed = s.api->Close();
        if (!s.closed)
            s.error = "model presentation references pending";
    } catch (const std::exception& e) {
        s.error = e.what();
    }
    return s.closed;
}
}
