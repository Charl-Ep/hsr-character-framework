#include "../../../src/presentation/audio/ActorVoicePlayer.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace hksr::framework;
using hksr::framework::detail::ActorVoicePlayer;
int metadata, native_calls = 0;
void Check(bool value) {
    if (!value)
        throw std::runtime_error("actor voice player assertion failed");
}
int __fastcall Retain(void*, int, int, void*) {
    ++native_calls;
    return 0;
}
void* __fastcall Target(int, void*) {
    ++native_calls;
    return nullptr;
}
void __fastcall Free(int, void*) {
    ++native_calls;
}
template <class F> hksr::api::Method M(F value) {
    return {reinterpret_cast<void*>(value), &metadata};
}
std::unique_ptr<hksr::api::OwnedSoundRequest> Request(bool cancelled = false) {
    hksr::api::ReferenceMethods refs{M(Retain), M(Target), M(Free)};
    auto value = std::make_unique<hksr::api::OwnedSoundRequest>(
        hksr::api::SoundRequestServices{refs, hksr::api::SoundAccess({}), {}, {}, {}});
    if (cancelled)
        value->Cancel();
    return value;
}
void Test() {
    const hksr::api::EntityOrigin actor{{reinterpret_cast<hksr::api::GameEntity*>(1), 7},
                                        reinterpret_cast<hksr::api::GameWorld*>(2)};
    bool allowed = true;
    std::function<void()> during_current;
    ActorVoicePlayer player(actor, [&] {
        auto callback = std::move(during_current);
        during_current = {};
        if (callback)
            callback();
        return allowed;
    });
    Check(!player.Play(hksr::api::VoiceCue::LightHit, "Hit") && player.Drained());
    std::vector<ActorVoiceCue> cues;
    player.Configure(1217, "base", [&](const auto& cue) {
        cues.push_back(cue);
        return Request();
    });
    Check(player.Configured() && player.Play(hksr::api::VoiceCue::UltraReady, "Ready"));
    Check(player.Play(hksr::api::VoiceCue::LightHit, "Hit", "variant"));
    Check(cues.size() == 2 && cues[0].avatar_id == 1217 && cues[0].actor.token.generation == 7 &&
          cues[0].actor.token.entity == actor.token.entity && cues[0].actor.world == actor.world &&
          cues[0].source_config == "base" && cues[0].state == "Ready" && cues[1].source_config == "variant");
    Check(player.Count() == 2 && !player.Drained() && !player.Service(true));
    Check(player.Count() == 2); // ordinary action updates do not cut requests
    player.Cancel();
    Check(!player.Service(true) && player.Drained() && player.Count() == 0);
    Check(player.Play(hksr::api::VoiceCue::LightHit, "Hit"));
    player.Service(false);
    Check(player.Drained()); // leaving the owner frame cancels this pool
    allowed = false;
    Check(!player.Play(hksr::api::VoiceCue::LightHit, "Hit") && player.Drained());
    allowed = true;

    player.Configure(1217, "base", [](const auto&) -> std::unique_ptr<hksr::api::OwnedSoundRequest> {
        throw std::runtime_error("voice factory failure");
    });
    Check(player.Play(hksr::api::VoiceCue::LightHit, "Hit") && player.Error() == "voice factory failure" &&
          player.Drained());
    Check(!player.Service(true)); // do not republish old errors over policy diagnostics
    player.Configure(1217, "base", [](const auto&) -> std::unique_ptr<hksr::api::OwnedSoundRequest> { return {}; });
    Check(player.Play(hksr::api::VoiceCue::LightHit, "Hit") && player.Error().empty() && player.Drained());
    // Null is the resolver's native-event deduplication outcome, not a failure.

    player.Configure(1217, "base", [&](const auto&) {
        Check(!player.Drained());
        player.Cancel();
        player.Service(true); // the in-flight ownership slot is not an active request
        Check(!player.Drained() && player.Count() == 0);
        return Request();
    });
    Check(player.Play(hksr::api::VoiceCue::LightHit, "Hit") && player.Count() == 1);
    player.Service(true);
    Check(player.Drained()); // cancellation before factory return includes the new request

    unsigned nested = 0;
    player.Configure(1217, "base", [&](const auto&) {
        Check(!player.Drained());
        if (!nested++) {
            Check(player.Play(hksr::api::VoiceCue::LightHit, "Nested"));
            Check(!player.Drained());
        }
        return Request();
    });
    Check(player.Play(hksr::api::VoiceCue::LightHit, "Outer") && nested == 2 && player.Count() == 2);
    player.Cancel();
    player.Service(true);
    Check(player.Drained());

    unsigned submitted = 0;
    player.Configure(1217, "base", [&](const auto&) { return Request(++submitted == 2); });
    player.Play(hksr::api::VoiceCue::LightHit, "First");
    during_current = [&] {
        Check(player.Play(hksr::api::VoiceCue::LightHit, "Added during service"));
        player.Service(true); // nested service must not erase the current iteration
    };
    player.Service(true);
    Check(submitted == 2 && player.Count() == 2); // new requests start in the next batch
    player.Service(true);
    Check(player.Count() == 1);
    player.Cancel();
    player.Service(true);
    Check(player.Drained() && native_calls == 0);
}
}
int main() try {
    Test();
    std::cout << "Actor voice ownership, source identity, native dedup result, cancellation and reentry passed.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
