#include "../../support/LegacyScriptFixture.hpp"
#include <hksr/framework/presentation/timeline/VisualTimelines.hpp>
#include <hksr/framework/presentation/scene/VisualProgramPlayer.hpp>
#include <stdexcept>

namespace {
void Require(bool value, const char* expression, int line) {
    if (!value)
        throw std::runtime_error("visual Timeline line " + std::to_string(line) + ": " + expression);
}
#define Check(...) Require((__VA_ARGS__), #__VA_ARGS__, __LINE__)
}
void TestVisualTimelines() {
    using namespace hksr;
    using namespace hksr::framework;
    int next = 0, stops = 0;
    std::vector<std::string> trace;
    std::function<void()> on_start;
    std::map<int, api::TimelinePlaybackPhase> phases;
    auto timelines = std::make_shared<VisualTimelines>(
        [&] {
            const int id = ++next;
            api::TimelineRequestServices services;
            services.start = [&, id](const std::string& path, int wrap, double time) {
                Check(path == "scene" && wrap == 0 && time == 0);
                phases[id] = api::TimelinePlaybackPhase::Playing;
                api::TimelineStartResult result;
                result.call.returned = true;
                result.playback = id;
                result.attributed = true;
                result.references.push_back(
                    {reinterpret_cast<void*>(1), reinterpret_cast<void*>(2), id, static_cast<std::uint64_t>(id)});
                if (on_start)
                    on_start();
                return result;
            };
            services.observe = [&](api::TimelineReference ref) { return phases.at(ref.playback); };
            services.stop = [&](api::TimelineReference ref) {
                ++stops;
                phases[ref.playback] = api::TimelinePlaybackPhase::Stopped;
                return api::CallResult{true, 0};
            };
            return std::make_unique<api::OwnedTimeline>(std::move(services));
        },
        [&](const auto& text) { trace.push_back(text); });
    VisualProgramServices services;
    ConnectVisualTimelines(services, timelines);
    Check(services.timeline(0, {"gift", "scene"}).state == VisualStepState::Done && !services.timelines_drained());
    Check(!trace.empty());
    const auto trace_once = trace.size();
    services.timelines_drained();
    Check(trace.size() == trace_once);
    Check(services.timeline(0, {"gift", "scene"}).state == VisualStepState::Done);
    Check(!services.timelines_drained() && stops == 1); // old receipt retained
    Check(services.timeline(0, {"gift", {}, false}).state == VisualStepState::Done && stops == 2);
    Check(!services.timelines_drained()); // stopped is not pooled
    phases[1] = api::TimelinePlaybackPhase::Retired;
    Check(!services.timelines_drained());
    phases[2] = api::TimelinePlaybackPhase::Retired;
    Check(services.timelines_drained());
    Check(trace.back().find("drained=1") != std::string::npos);
    Check(services.timeline(0, {"resolution", "scene"}).state == VisualStepState::Done);
    services.cancel_timelines(0);
    Check(!services.timelines_drained() && stops == 3);
    phases[3] = api::TimelinePlaybackPhase::Retired;
    Check(services.timelines_drained());
    Check(services.timeline(0, {"missing", {}, false}).state == VisualStepState::Failed);
    // Identical local keys in separate lifetime tokens are independent. A
    // retired token cannot stop a re-entered context with the same display name.
    Check(services.timeline(11, {"same", "scene"}).state == VisualStepState::Done);
    Check(services.timeline(12, {"same", "scene"}).state == VisualStepState::Done);
    services.cancel_timelines(0);
    services.timelines_drained();
    Check(stops == 3);
    services.cancel_timelines(11);
    services.timelines_drained();
    Check(stops == 4);
    Check(phases[4] == api::TimelinePlaybackPhase::Stopped && phases[5] == api::TimelinePlaybackPhase::Playing);
    phases[4] = api::TimelinePlaybackPhase::Retired;
    services.timelines_drained();
    Check(services.timeline_scope_drained(11) && !services.timeline_scope_drained(12));
    Check(services.timeline(13, {"same", "scene"}).state == VisualStepState::Done);
    services.cancel_timelines(11);
    services.timelines_drained();
    Check(stops == 4 && phases[6] == api::TimelinePlaybackPhase::Playing);
    services.cancel_timelines(12);
    services.cancel_timelines(13);
    services.timelines_drained();
    Check(stops == 6);
    phases[4] = phases[5] = phases[6] = api::TimelinePlaybackPhase::Retired;
    Check(services.timelines_drained());

    auto program = std::make_shared<VisualProgram>(ParseLegacyVisualProgram(R"({
      "format":"hksr.visual-program","id":"timeline-lifetimes","source":"config","scope":"action","effects":{},
      "contexts":{
        "persistent":{"enter":[{"op":"timeline_play","key":"same","path":"scene","source":"persistent.enter"}]},
        "other":{"enter":[{"op":"timeline_play","key":"same","path":"scene","source":"other.enter"}]}
      },
      "actions":{
        "start":{"state":"Start","restart":true,"enter":[
          {"op":"context_enter","name":"persistent","source":"start.persistent"},
          {"op":"context_enter","name":"other","source":"start.other"},
          {"op":"timeline_play","key":"same","path":"scene","source":"start.action"}]},
        "next":{"state":"Next","restart":true,"completion_state":"Idle"},
        "stop":{"state":"Stop","restart":true,"enter":[
          {"op":"timeline_stop","context":"persistent","key":"same","source":"stop.timeline"},
          {"op":"context_exit","name":"persistent","source":"stop.context"}]},
        "enter":{"state":"Enter","restart":true,"enter":[
          {"op":"context_enter","name":"persistent","source":"enter.context"}]}
      },
      "signals":{"stop_other":{"context":"other","instructions":[
        {"op":"timeline_stop","key":"same","source":"signal.stop"}]}}
    })"));
    services.effects =
        std::make_shared<api::PresentationEffects>(api::EffectRequestServices{{}, api::EffectAccess({}), {}, {}, {}});
    services.create = [](auto&, const auto&, const auto&) -> VisualEffectId {
        throw std::logic_error("unexpected effect");
    };
    VisualProgramPlayer player(program, services);
    player.Select("start");
    player.Service();
    Check(player.Status().prepared && next == 9);
    const auto persistent = player.ContextToken("persistent"), other = player.ContextToken("other");
    const auto intent = player.Select("next");
    player.Service();
    player.Service(); // service the queued cancellation
    Check(player.Status().prepared && player.ContextToken("persistent") == persistent);
    Check(phases[7] == api::TimelinePlaybackPhase::Playing && phases[8] == api::TimelinePlaybackPhase::Playing);
    Check(phases[9] == api::TimelinePlaybackPhase::Stopped);
    Check(player.Played(intent, 1, 1));
    player.Observe(1, 1, "Next", 1);
    player.Service();
    Check(player.SuccessorPlayed(intent, 2, 1));
    player.Observe(2, 1, "Idle", 0);
    player.Service();
    Check(player.Status().completed && phases[7] == api::TimelinePlaybackPhase::Playing);
    player.InterruptAction();
    player.Service();
    Check(phases[8] == api::TimelinePlaybackPhase::Playing);
    player.Select("stop");
    player.Service();
    Check(player.Status().prepared && !player.ContextToken("persistent"));
    Check(phases[7] == api::TimelinePlaybackPhase::Stopped && phases[8] == api::TimelinePlaybackPhase::Playing);
    player.Select("enter");
    player.Service();
    Check(player.ContextToken("persistent") != persistent && next == 10);
    Check(player.Signal(other, "stop_other"));
    player.Service();
    Check(phases[8] == api::TimelinePlaybackPhase::Stopped);
    player.End();
    player.Service();
    Check(phases[10] == api::TimelinePlaybackPhase::Stopped && !player.Drained());
    for (int id = 7; id <= 10; ++id)
        phases[id] = api::TimelinePlaybackPhase::Retired;
    player.Service();
    Check(player.Drained());
    VisualProgramPlayer reentrant(program, services);
    on_start = [&] {
        reentrant.End();
        reentrant.Service();
    };
    reentrant.Select("enter");
    reentrant.Service();
    on_start = {};
    Check(next == 11);
    reentrant.Service();
    Check(phases[11] == api::TimelinePlaybackPhase::Stopped && !reentrant.Drained());
    phases[11] = api::TimelinePlaybackPhase::Retired;
    reentrant.Service();
    Check(reentrant.Drained());

    // Only authored shared-environment operations contend. Actor-local work
    // remains independent, and Stop acceptance must not release the service.
    auto shared = std::make_shared<SharedVisualServices>();
    auto factory = [&] {
        const int id = ++next;
        api::TimelineRequestServices calls;
        calls.start = [&, id](const std::string&, int, double) {
            phases[id] = api::TimelinePlaybackPhase::Playing;
            api::TimelineStartResult result;
            result.call.returned = result.attributed = true;
            result.playback = id;
            result.references.push_back(
                {reinterpret_cast<void*>(1), reinterpret_cast<void*>(2), id, static_cast<std::uint64_t>(id)});
            return result;
        };
        calls.observe = [&](api::TimelineReference ref) { return phases.at(ref.playback); };
        calls.stop = [&](api::TimelineReference ref) {
            phases[ref.playback] = api::TimelinePlaybackPhase::Stopped;
            return api::CallResult{true, 0};
        };
        return std::make_unique<api::OwnedTimeline>(std::move(calls));
    };
    VisualTimelines a(factory, {}, shared), b(factory, {}, shared), local(factory, {}, shared);
    VisualTimeline environment{"env", "scene"};
    environment.shared_environment = true;
    const auto initial = next;
    Check(a.Execute(1, environment).state == VisualStepState::Done);
    Check(b.Execute(1, environment).state == VisualStepState::Pending && next == initial + 1);
    Check(local.Execute(1, {"local", "scene"}).state == VisualStepState::Done && next == initial + 2);
    a.Cancel(1);
    a.Service();
    Check(b.Execute(1, environment).state == VisualStepState::Pending);
    Check(!b.Drained(1));
    phases[initial + 1] = api::TimelinePlaybackPhase::Retired;
    a.Service();
    Check(b.Execute(1, environment).state == VisualStepState::Done && next == initial + 3);
    a.Cancel(1);
    a.Service();
    Check(a.Execute(2, environment).state == VisualStepState::Pending);
    a.Cancel(2);
    Check(a.Drained(2));
    b.Cancel(1);
    b.Service();
    Check(!b.Drained());
    phases[initial + 3] = api::TimelinePlaybackPhase::Retired;
    b.Service();
    Check(a.Execute(3, environment).state == VisualStepState::Done);
    // A cancelled admission cannot start after a factory callback reenters.
    std::unique_ptr<VisualTimelines> cancelled;
    cancelled = std::make_unique<VisualTimelines>(
        [&] {
            cancelled->Cancel(4);
            return factory();
        },
        VisualTimelines::Trace{}, shared);
    a.Cancel(3);
    a.Service();
    phases[next] = api::TimelinePlaybackPhase::Retired;
    a.Service();
    const auto allocated = next;
    Check(cancelled->Execute(4, environment).state == VisualStepState::Skipped && next == allocated + 1);
    Check(!phases.count(next) && cancelled->Drained());
    auto throwing = std::make_unique<VisualTimelines>(
        []() -> std::unique_ptr<api::OwnedTimeline> { throw std::runtime_error("factory failed"); },
        VisualTimelines::Trace{}, shared);
    bool failed = false;
    try {
        throwing->Execute(5, environment);
    } catch (const std::runtime_error&) {
        failed = true;
    }
    Check(failed && throwing->Drained() && b.Execute(6, environment).state == VisualStepState::Done);
    b.Cancel(6);
    local.Cancel(1);
    b.Service();
    local.Service();
    phases[next] = phases[initial + 2] = api::TimelinePlaybackPhase::Retired;
    b.Service();
    local.Service();
    Check(a.Drained() && b.Drained() && local.Drained());
    {
        int starts = 0;
        auto phase = api::TimelinePlaybackPhase::Playing;
        auto environment_owner = std::make_shared<SharedVisualServices>();
        VisualTimelines selected(
            [&] {
                api::TimelineRequestServices calls;
                calls.start = [&](const auto& path, int, double) {
                    Check(path == "skin-scene");
                    ++starts;
                    phase = api::TimelinePlaybackPhase::Playing;
                    api::TimelineStartResult result;
                    result.call.returned = result.attributed = true;
                    result.playback = 77;
                    result.references.push_back({reinterpret_cast<void*>(1), reinterpret_cast<void*>(2), 77, 77});
                    return result;
                };
                calls.observe = [&](auto) { return phase; };
                calls.stop = [&](auto) {
                    phase = api::TimelinePlaybackPhase::Retired;
                    return api::CallResult{true, 0};
                };
                return std::make_unique<api::OwnedTimeline>(std::move(calls));
            },
            {}, environment_owner,
            [](const std::string& path) -> std::optional<std::string> {
                if (path == "removed")
                    return {};
                return "skin-" + path;
            });
        Check(selected.Execute(1, {"key", "removed"}).state == VisualStepState::Skipped && starts == 0 &&
              selected.Drained());
        Check(selected.Execute(1, {"key", {}, false}).state == VisualStepState::Done);
        Check(selected.Execute(1, {"key", "scene"}).state == VisualStepState::Done && starts == 1);
        Check(selected.Execute(1, {"key", "removed"}).state == VisualStepState::Skipped);
        selected.Service();
        Check(!selected.Drained() && phase == api::TimelinePlaybackPhase::Retired);
        // The first service observes Playing and submits Stop. Even this
        // synchronous fixture needs the next observation to prove retirement.
        selected.Service();
        Check(selected.Drained());
        selected.Cancel(1);
        selected.Forget(1);
        const auto claim = environment_owner->Identify();
        Check(environment_owner->Acquire(SharedVisualService::Environment, claim));
        VisualTimeline pending{"waiting", "scene"};
        pending.shared_environment = true;
        Check(selected.Execute(2, pending).state == VisualStepState::Pending);
        pending.path = "removed";
        Check(selected.Execute(2, pending).state == VisualStepState::Skipped && selected.Drained());
        environment_owner->Release(SharedVisualService::Environment, claim);
    }
}
