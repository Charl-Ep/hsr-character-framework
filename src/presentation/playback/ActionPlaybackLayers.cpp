#include <hksr/framework/presentation/playback/ActionPlayback.hpp>
#include <hksr/model/ModelAccess.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace hksr::framework {
bool ActionPlayback::PlaySecondaryLayers(void* animator, int hash, float start, std::uint64_t frame,
                                         api::ManagedObjects& objects, api::CallResult& result) {
    const auto revision = intent_revision_;
    auto live = [&] { return !Ended() && !status_.closing && revision == intent_revision_; };
    if (!live())
        return false;
    const auto count = actions_.LayerCount(animator);
    if (!count.read.returned)
        throw std::runtime_error("Animator layer count unread");
    std::vector<int> layers;
    for (int layer = 1; layer < count.value; ++layer) {
        const auto present = actions_.HasState(animator, layer, hash);
        if (!live())
            return false;
        if (!present.read.returned)
            throw std::runtime_error("Animator layer membership unread");
        if (present.value)
            layers.push_back(layer);
    }
    if (layers.empty())
        return false;
    if (layers.size() > 1) {
        std::vector<int> hit_layers;
        if (hit_catalog_)
            if (const auto catalog = hit_catalog_(false); catalog && catalog->animations)
                for (const auto* name :
                     {&catalog->animations->hit_additive_layer, &catalog->animations->hit_override_layer})
                    if (name->available && !name->value.empty()) {
                        const auto index = actions_.LayerIndex(animator, resources_.Text(name->value, objects));
                        if (index.read.returned && index.value >= 0)
                            hit_layers.push_back(index.value);
                    }
        std::vector<int> bound;
        for (const auto layer : layers)
            if (std::find(hit_layers.begin(), hit_layers.end(), layer) != hit_layers.end())
                bound.push_back(layer);
        if (bound.empty())
            throw std::runtime_error(
                "animation has ambiguous secondary-layer bindings; select an explicit controller layer");
        layers = std::move(bound);
    }
    if (!secondary_weights_) {
        secondary_model_ = [this]() -> std::optional<LayerWeightModel> {
            if (Ended())
                return {};
            const auto component = resources_.CurrentComponent(api::AnimationComponent::Model);
            if (!component.call.returned || !component.object)
                return {};
            const auto model = models_.Read(static_cast<api::CharacterModelComponent*>(component.object));
            if (Ended() || !model.animator.read.returned || !model.animator.value || !model.changing.read.returned ||
                model.changing.value)
                return {};
            const auto controller = actions_.Controller(model.animator.value);
            if (!controller.call.returned)
                return {};
            return LayerWeightModel{model.animator.value, controller.object, component.object};
        };
        secondary_weights_ = std::make_unique<AnimatorLayerWeights>(
            LayerWeightServices{secondary_model_, [](void*, const std::string& layer) { return std::stoi(layer); },
                                [this](void* value, int layer) {
                                    const auto weight = actions_.LayerWeight(value, layer);
                                    if (!weight.read.returned)
                                        throw std::runtime_error("preview layer weight unread");
                                    return weight.value;
                                },
                                [this](void* value, int layer, float weight) {
                                    if (!actions_.SetLayerWeight(value, layer, weight).returned)
                                        throw std::runtime_error("preview layer weight write failed");
                                }});
    }
    const auto captured = secondary_model_();
    if (!captured || captured->animator != animator)
        throw std::runtime_error("preview model changed");
    secondary_ending_ = false;
    result = {true, 0};
    for (int layer : layers) {
        const auto weight = actions_.LayerWeight(animator, layer);
        if (!weight.read.returned || !std::isfinite(weight.value))
            throw std::runtime_error("preview layer weight unread");
        const auto current = secondary_model_();
        if (!current || !(*current == *captured))
            throw std::runtime_error("preview model changed");
        const auto token = ++secondary_serial_;
        secondary_layers_.push_back({*captured, layer, hash, token, frame});
        try {
            secondary_weights_->Set(token, std::to_string(layer), weight.value == 0 ? 1 : weight.value);
            const auto current_model = secondary_model_();
            if (!current_model || !(*current_model == *captured) || revision != intent_revision_)
                throw std::runtime_error("preview model changed");
            result = actions_.PlayAnimator(animator, hash, layer, start);
            if (!result.returned)
                throw std::runtime_error("secondary animation playback failed");
        } catch (...) {
            secondary_ending_ = true;
            throw;
        }
    }
    return true;
}
void ActionPlayback::ServiceSecondaryLayers(std::uint64_t frame) {
    if (!secondary_weights_)
        return;
    try {
        for (auto it = secondary_layers_.begin(); it != secondary_layers_.end();) {
            bool end = secondary_ending_ || Ended() || status_.closing;
            const auto current = secondary_model_();
            end = end || !current || !(*current == it->model);
            if (!end) {
                if (frame <= it->boundary) {
                    ++it;
                    continue;
                }
                const auto state = actions_.CurrentState(it->model.animator, it->layer);
                if (!state.read.returned)
                    throw std::runtime_error("secondary animation progress unread");
                const bool matches = state.short_name_hash == it->hash || state.full_path_hash == it->hash;
                end = !matches || (!state.loop && std::isfinite(state.normalized_time) && state.normalized_time >= 1);
            }
            if (end && secondary_weights_->Release(it->scope))
                it = secondary_layers_.erase(it);
            else
                ++it;
        }
        if (secondary_layers_.empty())
            secondary_ending_ = false;
    } catch (const std::exception& error) {
        status_.effect_note = error.what();
        secondary_ending_ = true;
    }
}
}
