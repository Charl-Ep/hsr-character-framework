#include "ExpressionPlayer.hpp"
#include <hksr/model/ModelAccess.hpp>
#include <hksr/animation/AnimationControl.hpp>
#include <hksr/animation/AnimationOverrides.hpp>
#include <hksr/animation/AnimationResources.hpp>
#include <hksr/entity/EntityAccess.hpp>
#include <hksr/model/ProfileInputs.hpp>
#include <hksr/entity/LifecycleHooks.hpp>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace hksr::framework::detail {
ExpressionPlayer::ExpressionPlayer(api::EntityToken owner, api::GameWorld* world, api::ActorAnimationSession& engine)
    : owner_(owner), world_(world), engine_(engine) {}
bool ExpressionPlayer::Ended() const {
    return api::TrackedEntities().IsDisposed(owner_) ||
           api::TrackedEntities().Current(owner_.entity).generation != owner_.generation;
}
std::shared_ptr<const AnimationRequestStatus> ExpressionPlayer::Submit(std::string state, float transition) {
    if (closing_)
        throw std::logic_error("expression owner closing");
    if (state.empty() || state.find('\0') != std::string::npos || !std::isfinite(transition) || transition < 0)
        throw std::invalid_argument("invalid expression write");
    auto entry = std::make_shared<Write>();
    entry->state = std::move(state);
    entry->transition = transition;
    entry->status = std::make_shared<AnimationRequestStatus>();
    entry->status->serial = ++next_;
    entry->status->entity_generation = owner_.generation;
    requests_.emplace(next_, entry);
    return entry->status;
}
void ExpressionPlayer::Release(std::uint64_t serial) {
    const auto found = requests_.find(serial);
    if (found == requests_.end())
        return;
    // A write establishes the requested face; releasing its observation does
    // not restore a previous expression or choose another presentation.
    found->second->status->released = true;
    requests_.erase(found);
}
void ExpressionPlayer::Cancel() {
    for (const auto& [id, entry] : requests_) {
        entry->status->cancelled = !entry->status->complete;
        entry->status->released = true;
        if (entry->status->cancelled)
            entry->status->error = "expression owner ended";
    }
    requests_.clear();
}
void ExpressionPlayer::Service(const api::WorldFrame& frame, api::ManagedObjects& objects) {
    auto& resources = engine_.Resources();
    const auto& models = engine_.Models();
    const auto& actions = engine_.Control();
    const auto& inputs = engine_.Inputs();
    std::vector<std::shared_ptr<Write>> pending;
    for (const auto& [id, entry] : requests_)
        pending.push_back(entry);
    for (const auto& entry : pending) {
        auto& status = *entry->status;
        if (status.released || status.complete)
            continue;
        auto live = [&] {
            return !status.released && !closing_ && !Ended() && frame.world == world_ &&
                   frame.disposing.read.returned && !frame.disposing.value;
        };
        try {
            if (!live())
                throw std::runtime_error("expression owner ended");
            const auto component = resources.Component(api::AnimationComponent::Model, objects);
            if (!live())
                continue;
            if (!component.call.returned || !component.object)
                throw std::runtime_error("expression model unavailable");
            const auto model = models.Read(static_cast<api::CharacterModelComponent*>(component.object));
            if (!live())
                continue;
            if (!model.animator.read.returned || !model.changing.read.returned)
                throw std::runtime_error("expression model unread");
            if (model.changing.value || !model.animator.value)
                continue;
            const auto initialized = inputs.AnimatorInitialized(model.animator.value);
            if (!live())
                continue;
            if (!initialized.read.returned)
                throw std::runtime_error("expression initialization unread");
            if (!initialized.value)
                continue;
            auto same_model = [&] {
                if (!live())
                    return false;
                const auto now = models.Read(static_cast<api::CharacterModelComponent*>(component.object));
                return live() && now.animator.read.returned && now.animator.value == model.animator.value &&
                       now.changing.read.returned && !now.changing.value;
            };
            const auto hash_result = actions.StateHash(resources.Text(entry->state, objects));
            if (!hash_result.read.returned)
                throw std::runtime_error("visual state hash unavailable");
            const auto hash = hash_result.value;
            if (!same_model())
                continue;
            const auto layers = actions.LayerCount(model.animator.value);
            if (!same_model())
                continue;
            if (!layers.read.returned || layers.value < 1)
                throw std::runtime_error("expression layer count unavailable");
            int matches = 0;
            for (int layer = 1; layer < layers.value; ++layer) {
                const auto present = actions.HasState(model.animator.value, layer, hash);
                if (!same_model())
                    break;
                if (!present.read.returned)
                    throw std::runtime_error("expression layer membership unread");
                if (present.value)
                    ++matches;
            }
            if (!same_model())
                continue;
            if (!matches) {
                status.skipped = true;
                status.skip_reason = "expression state is absent from this model: " + entry->state;
                Release(status.serial);
                continue;
            }
            if (matches != 1)
                throw std::runtime_error("expression state belongs to multiple model layers: " + entry->state);
            api::ExpressionOverride write({owner_, resources, models, actions}, model.animator.value, hash,
                                           entry->transition, same_model);
            if (!same_model())
                continue;
            const auto result = write.Apply(same_model);
            if (!result.attempted || !result.call.returned)
                throw std::runtime_error(result.detail.empty() ? "expression write failed" : result.detail);
            if (!same_model())
                throw std::runtime_error("expression model changed during write");
            status.accepted = status.complete = true;
            status.model_generation = reinterpret_cast<std::uintptr_t>(model.animator.value);
            // No Started/progress/end clock is invented for a setter.
        } catch (const std::exception& error) {
            status.error = error.what();
            status.cancelled = true;
            Release(status.serial);
        }
    }
}
}
