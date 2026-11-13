#include "../../support/LegacyScriptFixture.hpp"
#include <hksr/framework/presentation/timing/ActorTimingScopes.hpp>
#include <hksr/framework/presentation/scene/VisualProgramPlayer.hpp>
#include <hksr/presentation/effects/PresentationEffects.hpp>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace hksr;
using namespace hksr::framework;
namespace {
void Check(bool ok) {
    if (!ok)
        throw std::runtime_error("actor timing scope contract");
}
struct Fixture {
    api::ActorTimingModel model{1, 2, 3, 4};
    float layer = .25f, speed = -1, duration = 0;
    unsigned writes = 0;
    bool fail = false;
    std::uint64_t next = 0;
    unsigned removes = 0;
    std::map<std::uint64_t, bool> clocks;
    std::vector<std::string> keys;
    api::ActorTimingServices Calls() {
        api::ActorTimingServices calls;
        calls.layer = [&](const auto& name) -> std::optional<api::ActorTimingValue> {
            return name == "Additive" ? std::optional<api::ActorTimingValue>{{model, layer}} : std::nullopt;
        };
        calls.set_layer = [&](const auto& expected, const auto& name, float value, float seconds) {
            Check(expected == model && name == "Additive");
            ++writes;
            layer = value;
            duration = seconds;
            if (fail) {
                fail = false;
                return false;
            }
            return true;
        };
        calls.speed = [&]() -> std::optional<api::ActorTimingValue> { return api::ActorTimingValue{model, speed}; };
        calls.set_speed = [&](const auto& expected, float value) {
            Check(expected == model);
            ++writes;
            speed = value;
            return true;
        };
        calls.scale = [&](const auto&, const auto& key) {
            const auto id = ++next;
            clocks[id] = true;
            keys.push_back(key);
            return api::ActorTimeScaleToken{id};
        };
        calls.remove = [&](auto token, bool fade) {
            ++removes;
            if (!fade)
                clocks[token.value] = false;
            return true;
        };
        calls.contains = [&](auto token) -> std::optional<bool> { return clocks.at(token.value); };
        return calls;
    }
};
void Ownership() {
    Fixture f;
    api::ActorTiming timing(f.Calls());
    ActorTimingScopes scopes(timing);
    scopes.Layer(1, "Additive", .8f, 1);
    Check(f.layer == .8f && f.duration == 1);
    f.layer = .5f; // actual native tween is between start and target, not a new writer
    scopes.Layer(2, "Additive", .1f, .3f);
    const auto writes = f.writes;
    Check(scopes.Release(1) && f.writes == writes && f.layer == .1f);
    Check(scopes.Release(2) && f.layer == .25f && f.duration == 0);
    scopes.Speed(1, .4f);
    scopes.Speed(2, .8f);
    Check(scopes.Release(1) && f.speed == .8f);
    Check(scopes.Release(2) && f.speed == -1);
    scopes.Speed(1, .2f);
    f.speed = .9f;
    Check(scopes.Release(1) && f.speed == .9f);
    scopes.Layer(3, "Additive", .7f, 1);
    ++f.model.art;
    const auto before = f.writes;
    Check(scopes.Release(3) && f.writes == before); // never restore an unloaded model into its replacement
    api::ActorTimeScaleInput scale;
    scale.factor = .3f;
    scopes.Scale(1, "slow", scale);
    scopes.Scale(2, "slow", scale);
    Check(f.keys.size() == 2 && f.keys[0] != f.keys[1]);
    Check(scopes.ClearScale(1, "slow", true) == TimingClearState::Pending && f.clocks[1] && f.clocks[2]);
    const auto removing = f.removes;
    Check(scopes.ClearScale(1, "slow", true) == TimingClearState::Pending && f.removes == removing);
    Check(scopes.Release(1) && !f.clocks[1] && f.clocks[2]);
    Check(scopes.Close() && !f.clocks[2]);
    scale.duration = 2.0f;
    scopes.Scale(6, "finite", scale);
    Check(!scopes.Release(6, true) && f.clocks[3] && scopes.Error().empty()); // natural native tail is retained
    f.clocks[3] = false;
    Check(scopes.Release(6, true));
    scopes.Scale(7, "cancel", scale);
    Check(scopes.Release(7, false) && !f.clocks[4]);
    scale.duration.reset();
    scopes.Scale(9, "infinite", scale);
    scale.duration = 2.0f;
    scopes.Scale(9, "finite", scale);
    scopes.Scale(10, "other", scale);
    Check(scopes.ClearFinite(9, true) == TimingClearState::Pending && f.clocks[5] && f.clocks[6] && f.clocks[7]);
    f.clocks[6] = false;
    Check(scopes.ClearFinite(9, true) == TimingClearState::Done && f.clocks[5] && f.clocks[7]);
    Check(scopes.Release(9) && !f.clocks[5] && f.clocks[7]);
    Check(scopes.Release(10) && !f.clocks[7]);
    f.layer = .25f;
    f.fail = true;
    bool failed = false;
    try {
        scopes.Layer(8, "Additive", .6f, .5f);
    } catch (const std::exception&) {
        failed = true;
    }
    Check(failed && scopes.Release(8) && f.layer == .25f); // partial write keeps its restoration obligation
}
void ParserAndConsumer() {
    auto program = std::make_shared<const VisualProgram>(ParseLegacyVisualProgram(R"({
      "format":"hksr.visual-program","id":"timing","source":"fixture","scope":"owned","effects":{},"actions":{"play":{"state":"Hold","enter":[
      {"op":"layer_weight","source":"layer","layer":"Additive","weight":0.7,"duration":0.8},
      {"op":"lock_animator_speed","source":"speed","speed":0.4},
      {"op":"actor_time_scale","source":"clock","key":"slow","factor":0.3,"duration":2,"fade_in":0.1,"recipient":"Recipient"}
      ]},"clear":{"state":"Hold","enter":[
        {"op":"actor_time_scale","source":"clear","active":false,"finite_only":true,"fade":true}
      ]}}})"));
    const auto& clear = std::get<VisualActorTimeScale>(program->actions.at("clear").enter.front().operation);
    Check(!clear.active && clear.finite_only && clear.fade && clear.key.empty());
    Fixture f;
    api::ActorTiming timing(f.Calls());
    ActorTimingScopes scopes(timing);
    VisualProgramServices calls;
    calls.effects =
        std::make_shared<api::PresentationEffects>(api::EffectRequestServices{{}, api::EffectAccess({}), {}, {}, {}});
    calls.create = [](auto&, const auto&, const auto&) -> VisualEffectId {
        throw std::runtime_error("unexpected effect");
    };
    calls.layer_weight = [&](auto, const VisualLayerWeight& op) {
        scopes.Layer(1, op.layer, op.weight, op.duration);
        return VisualStepResult{VisualStepState::Done, {}};
    };
    calls.animator_speed = [&](auto, const VisualAnimatorSpeed& op) {
        scopes.Speed(1, op.speed);
        return VisualStepResult{VisualStepState::Done, {}};
    };
    calls.actor_time_scale = [&](auto, const VisualActorTimeScale& op) {
        Check(op.recipient == "Recipient");
        scopes.Scale(1, op.key, op.input);
        return VisualStepResult{VisualStepState::Done, {}};
    };
    VisualProgramPlayer player(program, std::move(calls));
    const auto sequence = player.BeginSequence({}, true, 0, false);
    for (const auto& instruction : program->actions.at("play").enter) {
        const auto receipt = player.SubmitInstruction(sequence, instruction);
        player.Service();
        Check(receipt->complete && receipt->issues.empty());
    }
    Check(f.layer == .7f && f.duration == .8f && f.speed == .4f && f.clocks[1]);
    player.EndSequence(sequence, true);
    Check(!scopes.Release(1, true) && f.clocks[1]);
    f.clocks[1] = false;
    Check(scopes.Release(1, true));
    player.Service();
    Check(f.layer == .25f && f.speed == -1 && !f.clocks[1]);
}
void IndependentSources() {
    // Two source scenes may use the same local scope number and logical key.
    // Their target clock indices and physical keys remain independent.
    Fixture receiver;
    api::ActorTiming first(receiver.Calls()), second(receiver.Calls());
    ActorTimingScopes source_a(first), source_b(second);
    api::ActorTimeScaleInput input;
    input.factor = .3f;
    source_a.Scale(1, "same", input);
    source_b.Scale(1, "same", input);
    Check(receiver.keys[0] != receiver.keys[1] && receiver.clocks[1] && receiver.clocks[2]);
    Check(source_a.Close() && !receiver.clocks[1] && receiver.clocks[2]);
    Check(source_b.Release(1) && !receiver.clocks[2]);
}
}
int main() {
    try {
        Ownership();
        ParserAndConsumer();
        IndependentSources();
        std::cout << "Actor timing scopes/parser/consumer passed; no game qualification.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
