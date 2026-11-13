#include "../../../src/presentation/scene/VisualBodyResources.hpp"
#include <hksr/framework/presentation/model/ActorModelScopes.hpp>
#include <hksr/framework/presentation/audio/ActorBgmScopes.hpp>
#include <hksr/framework/presentation/timing/ActorTimingScopes.hpp>
#include <hksr/presentation/attachments/AttachmentVisibility.hpp>
#include <hksr/presentation/attachments/AttachmentScale.hpp>
#include <hksr/model/ActorVisualConfiguration.hpp>
#include <hksr/framework/presentation/model/VisualModelTypes.hpp>
#include <iostream>
#include <stdexcept>

using namespace hksr;
using namespace hksr::framework;
namespace {
void Check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
void Drain() {
    detail::VisualBodyResources body;
    bool visible_ready = false, timing_ready = false;
    std::vector<std::string> closed;
    auto close = [&](const char* name) {
        closed.push_back(name);
        return true;
    };
    body.attachments = std::make_unique<api::AttachmentVisibility>(api::AttachmentVisibilityServices{
        [] { return true; }, [](const auto&) { return true; }, [](const auto&, bool, int) {},
        [&] {
            closed.push_back("visible");
            return visible_ready;
        }});
    body.attachment_scale = std::make_unique<api::AttachmentScale>(api::AttachmentScaleServices{
        [] { return true; }, [](const auto&, auto, bool, auto) {}, [](const auto&) {}, [&] { return close("scale"); }});
    api::ActorModelPresentationServices model;
    model.current = [] { return true; };
    model.close = [&] { return close("model"); };
    body.model_scopes =
        std::make_unique<ActorModelScopes>(std::make_unique<api::ActorModelPresentation>(std::move(model)));
    body.visual_configuration = std::make_unique<api::ActorVisualConfiguration>(api::ActorVisualConfigurationServices{
        [] { return true; }, [](const auto&) { return api::ActorVisualConfigurationResult{}; },
        [](bool, float, float) { return true; }, [&] { return close("configuration"); }});
    body.bgm = std::make_unique<ActorBgmScopes>(std::make_unique<api::ActorBgmSync>(api::ActorBgmSyncServices{
        [](const auto&) { return api::BgmSyncStartResult{}; }, [](auto) { return api::BgmSyncObservation{}; },
        [](auto) { return true; }, [] { return true; }, [&] { return close("bgm"); }}));
    api::ActorTimingServices timing;
    timing.close = [&] {
        closed.push_back("timing");
        return timing_ready;
    };
    body.timing = std::make_unique<api::ActorTiming>(std::move(timing));
    body.timing_scopes = std::make_unique<ActorTimingScopes>(*body.timing);
    api::ActorTimingServices foreign;
    foreign.close = [&] { return close("foreign"); };
    auto foreign_timing = std::make_unique<api::ActorTiming>(std::move(foreign));
    auto foreign_scopes = std::make_unique<ActorTimingScopes>(*foreign_timing);
    body.foreign_timings.push_back({{}, std::move(foreign_timing), std::move(foreign_scopes)});
    Check(!body.Close(detail::BodyDrain::Retire) && closed == std::vector<std::string>{"visible"},
          "retirement did not preserve pending owner and serial drain");
    closed.clear();
    Check(!body.Close(detail::BodyDrain::ModelChange), "pending resources reported drained");
    Check(closed == std::vector<std::string>{"visible", "scale", "model", "configuration", "bgm", "timing", "foreign"},
          "handoff omitted a resource or failed to advance independent siblings");
    Check(body.attachments && body.timing && body.foreign_timings.empty(), "pending resource owner was destroyed");
    visible_ready = true;
    timing_ready = true;
    closed.clear();
    Check(body.Close(detail::BodyDrain::ModelChange), "ready resource owners did not drain");
    const auto calls = closed.size();
    body.Reset();
    Check(closed.size() == calls && !body.attachments && !body.attachment_scale && !body.model_scopes &&
              !body.visual_configuration && !body.bgm && !body.timing && !body.timing_scopes,
          "reset left a model-bound resource or called the engine");
}
void TimingDependency() {
    detail::VisualBodyResources body;
    bool remove_ready = false, alive = true;
    unsigned provider_closed = 0;
    api::ActorTimingServices calls;
    calls.scale = [](const auto&, const auto&) { return api::ActorTimeScaleToken{1}; };
    calls.remove = [&](auto, bool) {
        if (remove_ready)
            alive = false;
        return remove_ready;
    };
    calls.contains = [&](auto) { return std::optional<bool>{alive}; };
    calls.close = [&] {
        ++provider_closed;
        return true;
    };
    body.timing = std::make_unique<api::ActorTiming>(std::move(calls));
    body.timing_scopes = std::make_unique<ActorTimingScopes>(*body.timing);
    api::ActorTimeScaleInput input;
    input.factor = .5f;
    input.duration = 1.0f;
    body.timing_scopes->Scale(8, "clock", input);
    Check(!body.Release(8, true).complete && body.timing_scopes, "finite natural tail was discarded");
    Check(!body.Close(detail::BodyDrain::ModelChange) && provider_closed == 0,
          "provider closed before its pending scopes");
    remove_ready = true;
    Check(body.Close(detail::BodyDrain::ModelChange) && provider_closed == 1 && !alive,
          "timing dependency failed to drain");
    body.Reset();
}
void ConfigurationIdentity() {
    using Part = api::VisualConfigurationComponent;
    detail::VisualBodyResources body;
    bool model = true, data = true, events = true, emotion = true, read_failure = false, reset_events = true;
    std::vector<Part> reads;
    api::ActorVisualConfigurationServices calls;
    calls.current = [&] { return model; };
    auto component = [&](Part use) {
        reads.push_back(use);
        if (read_failure)
            throw std::runtime_error("component lookup failed");
        return use == Part::Data ? data : use == Part::Events ? events : emotion;
    };
    calls.current_component = component;
    calls.current_input = [&, component](const api::ActorVisualConfigurationInput& input) {
        return component(Part::Data) &&
               (!(input.event_files || (input.reset && reset_events)) || component(Part::Events));
    };
    calls.apply = [](const auto&) { return api::ActorVisualConfigurationResult{}; };
    calls.emotion = [](bool, float, float) { return true; };
    calls.close = [] { return true; };
    body.visual_configuration = std::make_unique<api::ActorVisualConfiguration>(std::move(calls));
    VisualModelConfiguration radius;
    radius.input.radius = 1.0f;
    VisualModelConfiguration files;
    files.input.event_files = std::vector<std::string>{"events.json"};
    VisualModelConfiguration reset;
    reset.input.reset = true;
    auto current = [&](VisualModelOperation operation, std::vector<Part> expected) {
        reads.clear();
        const auto result = body.ConfigurationCurrent(operation);
        Check(reads == expected, "configuration queried an unrelated component");
        return result;
    };
    Check(current({radius}, {Part::Data}) && current({files}, {Part::Data, Part::Events}) &&
              current({reset}, {Part::Data, Part::Events}) && current({VisualEmotionEnable{}}, {Part::Emotion}),
          "configuration identity routing omitted a required component");
    events = false;
    Check(current({radius}, {Part::Data}) && !current({files}, {Part::Data, Part::Events}) &&
              current({VisualEmotionEnable{}}, {Part::Emotion}),
          "event replacement ended unrelated configuration operations");
    reset_events = false;
    Check(current({reset}, {Part::Data}), "configuration consumer rederived a broader reset dependency");
    events = true;
    data = false;
    Check(!current({files}, {Part::Data}) && current({VisualEmotionEnable{}}, {Part::Emotion}),
          "Data replacement ended independent emotion");
    data = true;
    emotion = false;
    Check(current({files}, {Part::Data, Part::Events}) && !current({VisualEmotionEnable{}}, {Part::Emotion}),
          "emotion replacement ended event configuration");
    read_failure = true;
    bool rejected = false;
    try {
        body.ConfigurationCurrent({radius});
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    Check(rejected, "unknown lookup was reported as a replacement to retry");
    read_failure = false;
    model = false;
    Check(!current({radius}, {}) && !current({VisualEmotionEnable{}}, {}), "ended model queried stale components");
    Check(body.Close(detail::BodyDrain::ModelChange), "configuration identity fixture did not drain");
}
}
int main() try {
    Drain();
    TimingDependency();
    ConfigurationIdentity();
    std::cout << "Visual body resource ownership, retirement and model-handoff drain passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
