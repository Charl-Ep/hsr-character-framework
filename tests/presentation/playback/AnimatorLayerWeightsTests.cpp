#include <hksr/framework/presentation/playback/AnimatorLayerWeights.hpp>
#include <hksr/framework/presentation/scene/VisualProgramPlayer.hpp>
#include <hksr/framework/layout/SceneLayout.hpp>
#include <algorithm>
#include <iterator>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
using namespace hksr;
using namespace hksr::framework;
void CheckAt(bool ok, int line) {
    if (!ok)
        throw std::runtime_error("layer weights line " + std::to_string(line));
}
#define Check(...) CheckAt((__VA_ARGS__), __LINE__)
template <class F> void Reject(F call) {
    bool rejected = false;
    try {
        call();
    } catch (const std::exception&) {
        rejected = true;
    }
    Check(rejected);
}
struct Fixture {
    int animator = 0, controller = 0, other_controller = 0;
    bool live = true, fail = false, partial = false;
    float value = .25f;
    unsigned writes = 0;
    std::function<void()> on_read;
    LayerWeightModel model{&animator, &controller};
    LayerWeightServices Services() {
        return {[this]() -> std::optional<LayerWeightModel> {
                    if (!live)
                        return {};
                    return model;
                },
                [](void*, const std::string& name) { return name == "Additive" ? 1 : -1; },
                [this](void* object, int index) {
                    Check(object == model.animator && index == 1);
                    if (on_read) {
                        auto call = std::move(on_read);
                        on_read = {};
                        call();
                    }
                    return value;
                },
                [this](void* object, int index, float weight) {
                    Check(object == model.animator && index == 1);
                    ++writes;
                    if (partial)
                        value = weight;
                    if (fail) {
                        fail = false;
                        throw std::runtime_error("simulated write failure");
                    }
                    value = weight;
                }};
    }
};
struct ProgramFixture {
    struct Scope {
        std::shared_ptr<VisualEffectContext> context;
        std::uint64_t token;
        std::unique_ptr<VisualContextWork> work;
    };
    AnimatorLayerWeights weights;
    std::vector<Scope> scopes;
    std::unique_ptr<VisualProgramPlayer> player;
    std::uint64_t next = 0;
    ProgramFixture(Fixture& native, std::shared_ptr<const VisualProgram> program) : weights(native.Services()) {
        VisualProgramServices services;
        services.character = 7;
        services.effects = std::make_shared<api::PresentationEffects>(
            api::EffectRequestServices{{}, api::EffectAccess({}), {}, {}, {}});
        services.create = [](auto&, const auto&, const auto&) -> VisualEffectAdmission {
            throw std::runtime_error("unexpected effect creation");
        };
        services.layer_weight = [this](auto context, const VisualLayerWeight& command) {
            auto scope =
                std::find_if(scopes.begin(), scopes.end(), [&](const auto& s) { return s.context == context; });
            if (scope == scopes.end()) {
                scopes.push_back({context, ++next, std::make_unique<VisualContextWork>(context)});
                scope = std::prev(scopes.end());
            }
            weights.Set(scope->token, command.layer, command.weight);
            return VisualStepResult{VisualStepState::Done, {}};
        };
        player = std::make_unique<VisualProgramPlayer>(std::move(program), std::move(services));
    }
    void Tick() {
        player->Service();
        for (auto scope = scopes.begin(); scope != scopes.end();) {
            if (scope->context->Closing() || scope->context->Finishing()) {
                Check(weights.Release(scope->token));
                scope = scopes.erase(scope);
            } else
                ++scope;
        }
        player->Service(false);
    }
};
}
void TestAnimatorLayerWeights() {
    {
        Fixture f;
        AnimatorLayerWeights weights(f.Services());
        weights.Set(1, "Additive", .5f);
        weights.Set(2, "Additive", 1);
        Check(f.value == 1);
        Check(weights.Release(1) && f.value == 1);
        Check(weights.Release(2) && f.value == .25f);
        const auto writes = f.writes;
        Check(weights.Close() && f.writes == writes);
        weights.Set(3, "Additive", .5f);
        weights.Set(4, "Additive", 1);
        Check(weights.Release(4) && f.value == .5f);
        Check(weights.Close() && f.value == .25f);
    }
    {
        Fixture f;
        AnimatorLayerWeights weights(f.Services());
        weights.Set(1, "Additive", .5f);
        f.value = .7f;
        Check(weights.Release(1) && f.value == .7f && f.writes == 1);
        weights.Set(2, "Additive", .5f);
        f.value = .9f;
        weights.Set(3, "Additive", 1);
        Check(weights.Release(2) && f.value == 1);
        Check(weights.Release(3) && f.value == .9f); // new baseline, no stale .7
    }
    {
        Fixture f;
        AnimatorLayerWeights weights(f.Services());
        weights.Set(1, "Additive", 1);
        f.model.controller = &f.other_controller;
        f.value = .3f;
        Check(weights.Close() && f.value == .3f && f.writes == 1);
        weights.Set(2, "Additive", .8f);
        Check(weights.Close() && f.value == .3f);
        weights.Set(3, "Additive", 1);
        f.live = false;
        const auto writes = f.writes;
        Check(weights.Close() && f.writes == writes); // ended owner: no native restoration
    }
    for (bool partial : {false, true}) {
        Fixture f;
        AnimatorLayerWeights weights(f.Services());
        f.fail = true;
        f.partial = partial;
        Reject([&] { weights.Set(1, "Additive", 1); });
        Check(weights.Close() && f.value == .25f);
        weights.Set(2, "Additive", .5f);
        weights.Set(3, "Additive", 1);
        f.fail = true;
        Check(!weights.Release(3) && !weights.Error().empty());
        Check(weights.Release(3) && f.value == .5f);
        Check(weights.Close() && f.value == .25f);
    }
    {
        Fixture f;
        AnimatorLayerWeights weights(f.Services());
        Reject([&] { weights.Set(1, "missing", 1); });
        Reject([&] { weights.Set(0, "Additive", 1); });
        Reject([&] { weights.Set(1, "Additive", std::numeric_limits<float>::quiet_NaN()); });
        f.on_read = [&] { f.model.controller = &f.other_controller; };
        Reject([&] { weights.Set(1, "Additive", 1); });
        Check(f.writes == 0);
    }
    {
        // Actual instruction dispatch and context drain contract, not a direct
        // policy-only Set: the native work keeps a closing scope alive.
        Fixture f;
        AnimatorLayerWeights weights(f.Services());
        auto program = std::make_shared<VisualProgram>();
        program->id = "weight-test";
        VisualProgramServices services;
        services.effects = std::make_shared<api::PresentationEffects>(
            api::EffectRequestServices{{}, api::EffectAccess({}), {}, {}, {}});
        services.create = [](auto&, const auto&, const auto&) -> VisualEffectAdmission {
            throw std::runtime_error("unexpected effect creation");
        };
        std::shared_ptr<VisualEffectContext> context;
        std::unique_ptr<VisualContextWork> work;
        services.layer_weight = [&](auto owner, const VisualLayerWeight& command) {
            context = owner;
            if (!work)
                work = std::make_unique<VisualContextWork>(owner);
            weights.Set(1, command.layer, command.weight);
            return VisualStepResult{VisualStepState::Done, {}};
        };
        VisualProgramPlayer player(program, services);
        const auto scope = player.BeginSequence({});
        const auto request = player.SubmitInstruction(scope, {VisualLayerWeight{"Additive", 1}, "fixture"});
        player.Service();
        Check(request->complete && f.value == 1 && context);
        player.EndSequence(scope, false);
        player.Service();
        Check(!player.SequenceDrained(scope));
        Check(context->Closing() || context->Finishing());
        Check(weights.Close());
        work.reset();
        player.Service();
        Check(player.SequenceDrained(scope) && f.value == .25f);
        player.ForgetSequence(scope);
        player.End();
        player.Service();
        Check(player.Drained());
    }
    {
        // Layout serialization preserves the recipe/selection, never the old
        // native baseline. Restore executes on the replacement model's baseline.
        Fixture native;
        auto program = std::make_shared<VisualProgram>();
        program->id = "persistent-weight";
        const VisualInstruction write{VisualLayerWeight{"Additive", .37f}, "fixture"};
        program->contexts["phase"].enter = {write};
        program->contexts["phase"].restore = std::vector<VisualInstruction>{write};
        ProgramFixture first(native, program);
        const auto sequence = first.player->BeginSequence({});
        const auto enter =
            first.player->SubmitInstruction(sequence, {VisualContextOperation{"phase", true, true}, "fixture"});
        for (int i = 0; i < 3; ++i)
            first.Tick();
        Check(enter->complete && native.value == .37f);
        SceneLayout layout;
        layout.scene = "weight-map";
        SavedCharacter actor;
        actor.key = 7;
        actor.source = BrowsePlayer{{1217, 0, 0}};
        actor.restore.visual_state = first.player->Capture();
        actor.task_catalog = "weight-catalog";
        TaskSelection selection{{"weight-catalog", "upper"}};
        selection.inputs_captured = true;
        selection.query.inputs["phase"] = std::int64_t{2};
        selection.visual.choices["variant"] = "enhanced";
        actor.restore.task_selection = selection;
        layout.characters.push_back(std::move(actor));
        const auto decoded = DecodeSceneLayout(EncodeSceneLayout(layout));
        Check(SameLayout(layout, decoded) && decoded.characters[0].restore.visual_state->contexts.size() == 1);
        Check(decoded.characters[0].restore.task_selection->task.name == "upper" && decoded.characters[0].restore.task_selection->inputs_captured &&
              std::get<std::int64_t>(decoded.characters[0].restore.task_selection->query.inputs.at("phase")) == 2 &&
              decoded.characters[0].restore.task_selection->visual.choices.at("variant") == "enhanced");
        first.player->End();
        first.Tick();
        first.Tick();
        Check(first.player->Drained() && native.value == .25f);
        native.model.controller = &native.other_controller;
        native.value = .6f;
        ProgramFixture restored(native, program);
        restored.player->Restore(*decoded.characters[0].restore.visual_state);
        for (int i = 0; i < 3; ++i)
            restored.Tick();
        Check(!restored.player->Restoring() && restored.player->Status().issues.empty() && native.value == .37f);
        Check(SameVisualSnapshot(*decoded.characters[0].restore.visual_state, restored.player->Capture()));
        restored.player->End();
        restored.Tick();
        restored.Tick();
        Check(restored.player->Drained() && native.value == .6f);
    }
}
