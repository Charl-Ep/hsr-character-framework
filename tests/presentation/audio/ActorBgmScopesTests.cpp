#include "../../support/LegacyScriptFixture.hpp"
#include <hksr/framework/presentation/audio/ActorBgmScopes.hpp>
#include <hksr/framework/presentation/scene/VisualProgram.hpp>
#include "../../../src/serialization/VisualBgmCodec.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace hksr;
using namespace hksr::framework;
namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void Reject(F fn) {
    bool failed = false;
    try {
        fn();
    } catch (const std::exception&) {
        failed = true;
    }
    Check(failed, "invalid BGM instruction accepted");
}
struct Fixture {
    bool live = true, partial = false, unsupported = false, pending = false, stop_fault = false;
    std::uint64_t next = 0, active = 0;
    unsigned stops = 0, starts = 0, closed = 0;
    std::map<std::uint64_t, bool> held;
    std::unique_ptr<api::ActorBgmSync> Create() {
        return std::make_unique<api::ActorBgmSync>(api::ActorBgmSyncServices{
            [&](const auto&) {
                ++starts;
                if (unsupported)
                    return api::BgmSyncStartResult{api::BgmSyncStartState::Unsupported, {}, "no CFF"};
                if (pending)
                    return api::BgmSyncStartResult{api::BgmSyncStartState::Pending, {}, "loading"};
                active = ++next;
                held[active] = true;
                return api::BgmSyncStartResult{partial ? api::BgmSyncStartState::Failed
                                                       : api::BgmSyncStartState::Accepted,
                                               {active},
                                               partial ? "native start partially registered" : ""};
            },
            [&](auto token) {
                if (active != token.value)
                    held.erase(token.value);
                return api::BgmSyncObservation{true, active == token.value, !held.count(token.value),
                                               stop_fault ? "native stop failure" : ""};
            },
            [&](auto token) {
                if (stop_fault)
                    return false;
                if (active == token.value) {
                    active = 0;
                    ++stops;
                }
                held.erase(token.value);
                return true;
            },
            [&] { return live; },
            [&] {
                ++closed;
                return active == 0;
            }});
    }
};
VisualBgmSync Start() {
    return {true, {"Robin", 100, {{"Loop", {{0, 1, 0}}, std::nullopt}}}};
}
void Scopes() {
    Fixture f;
    ActorBgmScopes scopes(f.Create());
    Check(scopes.Execute(1, Start()).state == VisualStepState::Done &&
              scopes.Execute(2, Start()).state == VisualStepState::Done,
          "owned starts failed");
    Check(scopes.Release(1) && f.stops == 0 && f.active == 2, "older scope stopped newer config");
    Check(scopes.Execute(2, {false, {}}).state == VisualStepState::Done && f.active == 0 && f.stops == 1,
          "explicit stop did not stop own scope");
    f.partial = true;
    Check(scopes.Execute(3, Start()).state == VisualStepState::Failed && f.active == 3,
          "partial start was lost/skipped");
    f.stop_fault = true;
    Check(!scopes.Release(3) && !scopes.Error().empty(), "failed stop dropped cleanup responsibility");
    f.stop_fault = false;
    Check(scopes.Release(3) && f.active == 0, "failed-start cleanup did not retry");
    f.partial = false;
    f.unsupported = true;
    Check(scopes.Execute(4, Start()).state == VisualStepState::Skipped, "unsupported sync was not skipped");
    f.unsupported = false;
    f.pending = true;
    Check(scopes.Execute(5, Start()).state == VisualStepState::Pending, "unready sync was not pending");
    f.pending = false;
    Check(scopes.Execute(5, Start()).state == VisualStepState::Done, "ready sync retry failed");
    f.live = false;
    Check(!scopes.Current() && scopes.Service() && scopes.Close() && f.closed == 1 && !f.active,
          "model retirement did not withdraw its synchronization");
    Fixture repeat;
    ActorBgmScopes repeated(repeat.Create());
    for (int i = 0; i < 200; ++i) {
        Check(repeated.Execute(8, Start()).state == VisualStepState::Done && repeated.Service(),
              "same-scope repeated start failed");
        Check(repeat.held.size() == 1 && repeat.stops == 0,
              "repeated starts accumulated native requests/roots or stopped newer configuration");
    }
    Check(repeated.Close() && repeat.held.empty() && repeat.stops == 1, "last repeated synchronization did not drain");
}
void Codec() {
    using Json = nlohmann::json;
    auto row = Json::parse(
        R"({"op":"bgm.sync","source":"Robin/events/73","state":"Robin","bpm":100,"chapters":[{"cue":"Loop","phases":[{"start":0,"blend":0.2,"offset":0.1}],"double_speed_phases":null}]})");
    const auto value = visual_bgm_codec::Parse(row);
    Check(value && value->active && value->config.chapters[0].phases[0].blend == .2f &&
              !value->config.chapters[0].double_speed_phases,
          "BGM typed data/null did not survive parsing");
    row["chapters"][0]["double_speed_phases"] = Json::array();
    Check(visual_bgm_codec::Parse(row)->config.chapters[0].double_speed_phases.has_value(),
          "null and authored empty array were conflated");
    auto invalid = row;
    invalid["bpm"] = 0;
    Reject([&] { visual_bgm_codec::Parse(invalid); });
    invalid = row;
    invalid["chapters"][0]["speed"] = 1;
    Reject([&] { visual_bgm_codec::Parse(invalid); });
    Check(!visual_bgm_codec::Parse(Json{{"op", "bgm.sync"}, {"source", "stop"}, {"active", false}})->active,
          "stop required phantom configuration");
    const auto program = ParseLegacyVisualProgram(
        Json{{"format", "hksr.visual-program"},
             {"id", "bgm"},
             {"source", "fixture"},
             {"scope", "owned"},
             {"effects", Json::object()},
             {"actions", {{"show", {{"state", "StandBy"}, {"restart", true}, {"enter", Json::array({row})}}}}}}
            .dump());
    Check(std::holds_alternative<VisualBgmSync>(program.actions.at("show").enter.front().operation),
          "shared visual parser did not dispatch BGM synchronization");
}
}
int main() try {
    Scopes();
    Codec();
    std::cout << "BGM scopes, overlapping configs, partial cleanup and typed codec passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
