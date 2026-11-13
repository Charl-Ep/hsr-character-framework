#include "VisualSceneTransients.hpp"
#include <hksr/framework/presentation/effects/SpatialHitEffects.hpp>
#include <hksr/framework/presentation/effects/VisualEffectContext.hpp>
#include <hksr/framework/presentation/projectiles/VisualProjectiles.hpp>
#include <hksr/model/ActorModelChange.hpp>
#include <algorithm>

namespace hksr::framework::detail {
VisualSceneTransients::VisualSceneTransients(api::EntityOrigin owner) : owner_(owner) {}
VisualSceneTransients::~VisualSceneTransients() = default;
std::shared_ptr<SpatialHitEffects>
VisualSceneTransients::FindPointHit(const std::shared_ptr<VisualEffectContext>& context, const std::string& target) const {
    const auto found = std::find_if(point_hits_.begin(), point_hits_.end(),
                                     [&](const auto& row) { return row.context == context && row.target == target; });
    return found == point_hits_.end() ? nullptr : found->effects;
}
void VisualSceneTransients::KeepPointHit(std::shared_ptr<VisualEffectContext> context, std::string target,
                                        std::shared_ptr<SpatialHitEffects> effects, std::uint64_t frame) {
    point_hits_.push_back({std::move(context), std::move(target), std::move(effects), frame});
}
void VisualSceneTransients::KeepProjectile(std::shared_ptr<VisualProjectileBatch> batch) {
    projectiles_.push_back(std::move(batch));
}
api::ActorModelChange& VisualSceneTransients::KeepModelChange(std::unique_ptr<api::ActorModelChange> request) {
    model_changes_.push_back(std::move(request));
    return *model_changes_.back();
}
void VisualSceneTransients::CollectModelChanges(const std::function<void(const std::string&)>& report) {
    for (auto change = model_changes_.begin(); change != model_changes_.end();) {
        if ((*change)->Close())
            change = model_changes_.erase(change);
        else {
            const auto& error = (*change)->Status().cleanup_error;
            if (report && !error.empty() && cleanup_error_ != error) {
                cleanup_error_ = error;
                report(error);
            }
            ++change;
        }
    }
}
void VisualSceneTransients::Service(const api::WorldFrame* frame, bool retiring) {
    for (auto hit = point_hits_.begin(); hit != point_hits_.end();) {
        if (retiring || hit->context->Closing())
            hit->effects->Cancel();
        float delta = 0;
        if (frame && hit->frame != frame->sequence) {
            hit->frame = frame->sequence;
            if (frame->world == owner_.world && frame->paused.read.returned && !frame->paused.value)
                delta = frame->delta_time;
        }
        hit->effects->Service(delta);
        if (hit->effects->Drained() && (retiring || hit->context->Closing() || hit->context->Finishing()))
            hit = point_hits_.erase(hit);
        else
            ++hit;
    }
    for (auto projectile = projectiles_.begin(); projectile != projectiles_.end();) {
        if (retiring)
            (*projectile)->Cancel();
        if (frame)
            (*projectile)->Service(*frame);
        if ((*projectile)->Status().closed)
            projectile = projectiles_.erase(projectile);
        else
            ++projectile;
    }
}
bool VisualSceneTransients::ReleaseParticipant(api::EntityOrigin origin) {
    bool drained = true;
    for (const auto& batch : projectiles_)
        drained = batch->ReleaseParticipant(origin) && drained;
    if (owner_.world == origin.world && owner_.token.entity == origin.token.entity &&
        owner_.token.generation == origin.token.generation)
        for (const auto& hit : point_hits_) {
            hit.effects->Cancel();
            drained = hit.effects->Drained() && drained;
        }
    return drained;
}
bool VisualSceneTransients::Drained() const {
    return point_hits_.empty() && projectiles_.empty() && model_changes_.empty();
}
}
