#include "VisualBodyResources.hpp"
#include <hksr/framework/presentation/model/ActorModelScopes.hpp>
#include <hksr/framework/presentation/audio/ActorBgmScopes.hpp>
#include <hksr/framework/presentation/timing/ActorTimingScopes.hpp>
#include <hksr/presentation/attachments/AttachmentVisibility.hpp>
#include <hksr/presentation/attachments/AttachmentScale.hpp>
#include <hksr/model/ActorVisualConfiguration.hpp>
#include <hksr/framework/presentation/model/VisualModelTypes.hpp>
#include <stdexcept>

namespace hksr::framework::detail {
VisualBodyResources::VisualBodyResources() = default;
VisualBodyResources::~VisualBodyResources() = default;
bool VisualBodyResources::ConfigurationCurrent(const VisualModelOperation& operation) const {
    if (!visual_configuration)
        return false;
    if (std::holds_alternative<VisualEmotionEnable>(operation.command))
        return visual_configuration->Current(api::VisualConfigurationComponent::Emotion);
    const auto* configuration = std::get_if<VisualModelConfiguration>(&operation.command);
    if (!configuration)
        throw std::logic_error("configuration identity requires a configuration or emotion operation");
    return visual_configuration->Current(configuration->input);
}
BodyReleaseResult VisualBodyResources::Release(std::uint64_t scope, bool natural) {
    const bool visibility = !attachments || attachments->Release(scope);
    const bool scale = !attachment_scale || attachment_scale->Release(scope);
    const bool weight = ReleaseTiming(scope, natural);
    const bool model = !model_scopes || model_scopes->Release(scope);
    const bool sync = !bgm || bgm->Release(scope);
    if (visibility && scale && weight && model && sync)
        return {};
    return {false, !visibility ? attachments->Error()
                   : !scale    ? attachment_scale->Error()
                   : !model    ? model_scopes->Error()
                   : !sync     ? bgm->Error()
                               : TimingError()};
}
bool VisualBodyResources::ReleaseTiming(std::uint64_t scope, bool natural) {
    bool complete = !timing_scopes || timing_scopes->Release(scope, natural);
    for (auto& entry : foreign_timings)
        complete = entry.scopes->Release(scope, natural) && complete;
    return complete;
}
std::string VisualBodyResources::TimingError() const {
    if (timing_scopes && !timing_scopes->Error().empty())
        return timing_scopes->Error();
    for (const auto& entry : foreign_timings)
        if (!entry.scopes->Error().empty())
            return entry.scopes->Error();
    return {};
}
bool VisualBodyResources::CloseForeignTiming() {
    bool complete = true;
    for (auto& entry : foreign_timings) {
        const bool scopes = entry.scopes->Close();
        complete = (scopes && entry.timing->Close()) && complete;
    }
    if (complete)
        foreign_timings.clear();
    return complete;
}
bool VisualBodyResources::Close(BodyDrain mode) {
    bool complete = true;
    auto close = [&](auto& resource) {
        if (mode == BodyDrain::Retire && !complete)
            return false;
        const bool done = !resource || resource->Close();
        complete = done && complete;
        return done;
    };
    close(attachments);
    close(attachment_scale);
    close(model_scopes);
    close(visual_configuration);
    close(bgm);
    const bool scopes = close(timing_scopes);
    if (scopes)
        close(timing);
    if (mode == BodyDrain::ModelChange || complete)
        complete = CloseForeignTiming() && complete;
    return complete;
}
void VisualBodyResources::Reset() {
    attachments.reset();
    attachment_scale.reset();
    model_scopes.reset();
    visual_configuration.reset();
    bgm.reset();
    timing_scopes.reset();
    timing.reset();
    foreign_timings.clear();
}
}
