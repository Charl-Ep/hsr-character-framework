#include "../support/LegacyScriptFixture.hpp"
#include <hksr/framework/tasks/TaskController.hpp>
#include <hksr/framework/presentation/scene/VisualProgramPlayer.hpp>
#include <hksr/framework/tasks/TaskContextRegistry.hpp>
#include <hksr/framework/presentation/reactions/ReactionPolicy.hpp>
#include <hksr/framework/application/CharacterTasks.hpp>
#include <hksr/framework/layout/SceneLayout.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <limits>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace hksr;
using namespace hksr::framework;
using Json = nlohmann::json;
void CheckAt(bool value, int line) {
    if (!value)
        throw std::runtime_error("task catalog line " + std::to_string(line));
}
#define Check(...) CheckAt((__VA_ARGS__), __LINE__)
template <class F> void RejectAt(F function, int line) {
    bool rejected = false;
    try {
        function();
    } catch (const std::exception&) {
        rejected = true;
    }
    if (!rejected)
        throw std::runtime_error("task catalog expected rejection at line " + std::to_string(line));
}
#define Reject(...) RejectAt((__VA_ARGS__), __LINE__)
Json Document() {
    return Json::parse(R"({
      "format":"hksr.task-catalog","id":"test","domain":"battle",
      "visual":{"id":"scene","source":"fixture","scope":"owned","effects":{},
        "contexts":{"enhanced":{"enter":[],"exit":[]}}},
      "contexts":{"enhanced":{"values":{"charged":true},"phase":"enhanced"}},
      "sequences":{
        "stand":{"entry":{"op":"animation","request":"pose","participant":"body","state":"StandBy"}},
        "attack":{"entry":{"op":"serial","children":[
          {"op":"animation","request":"body","participant":"body","state":"Attack"},
          {"op":"animation","request":"face","participant":"body","state":"Emo_Attack","layer":3},
          {"op":"parallel","children":[
            {"op":"wait_request","request":"body"},
            {"op":"wait_request","request":"face"}]}]},
          "interruption":{"op":"serial","children":[{"op":"wait_seconds","seconds":0.2},
            {"op":"animation","request":"exit","participant":"body","state":"Exit"}]}},
        "hold":{"entry":{"op":"serial","children":[
          {"op":"visual","request":"enter","instruction":{"op":"context_enter","name":"enhanced","source":"modifier/entry"}},
          {"op":"wait_request","request":"enter","signal":"accepted"},
          {"op":"repeat","until_finish":true,"child":{"op":"wait_frame"}},
          {"op":"visual","request":"exit","instruction":{"op":"context_exit","name":"enhanced","source":"modifier/exit"}},
          {"op":"wait_request","request":"exit","signal":"accepted"}]}}
      },
      "tasks":{
        "idle":{"entry":"stand","tags":["normal"]},
        "attack":{"entry":"attack","next":{"task":"idle"},
          "inputs":{"power":{"type":"integer","default":1}}},
        "hold":{"entry":"hold","sources":[{"document":"fixture","field":"Modifier"}]},
        "unmapped":{"requires":[{"op":"unresolved","source":"fixture/unknown","detail":"not translated"}]},
        "conditioned":{"entry":"stand","requires":[{"op":"value","name":"charged","is":true}]}
      }
    })");
}
struct Fixture {
    struct EffectReceipt {
        unsigned id;
        bool released = false, remove_requested = false, auto_release = true;
    };
    inline static std::map<int, void*> effect_roots;
    inline static int next_effect_root = 0, effect_method_info = 0;
    inline static EffectReceipt* allocating_effect = nullptr;
    static int __fastcall KeepEffect(void* object, int, int, void*) {
        effect_roots[++next_effect_root] = object;
        return next_effect_root;
    }
    static void* __fastcall GetEffect(int id, void*) {
        return effect_roots.at(id);
    }
    static void __fastcall FreeEffect(int id, void*) {
        effect_roots.erase(id);
    }
    static unsigned __fastcall EffectId(EffectReceipt* p, void*) {
        return p->id;
    }
    static bool __fastcall EffectReleased(EffectReceipt* p, void*) {
        return p->released;
    }
    static int __fastcall EffectState(EffectReceipt*, void*) {
        return 3;
    }
    static void* __fastcall EffectInstance(EffectReceipt* p, void*) {
        return p;
    }
    static void* __fastcall MapEffect(void*, void* path, void*) {
        return path;
    }
    static bool __fastcall EffectEmpty(void*, void*) {
        return false;
    }
    static api::EffectReference __fastcall AcquireEffect(void*, void*) {
        return {allocating_effect->id, 0, allocating_effect};
    }
    static void __fastcall SubmitEffect(void*, EffectReceipt*, void*) {}
    static void __fastcall ReleaseEffect(EffectReceipt* p, void*) {
        p->released = true;
    }
    static bool __fastcall RemoveEffect(void*, api::EffectReference ref, bool, void*) {
        auto* receipt = static_cast<EffectReceipt*>(ref.parameters);
        receipt->remove_requested = true;
        if (receipt->auto_release)
            receipt->released = true;
        return true;
    }
    template <class F> static api::Method EffectMethod(F entry) {
        return {reinterpret_cast<void*>(entry), &effect_method_info};
    }
    std::shared_ptr<const TaskCatalog> catalog;
    std::shared_ptr<VisualProgramPlayer> scene;
    std::unique_ptr<TaskController> controller;
    std::vector<std::pair<AnimationCommand, std::shared_ptr<AnimationRequestStatus>>> plays;
    std::vector<std::pair<PoseSelection, std::shared_ptr<AnimationRequestStatus>>> poses;
    std::vector<std::shared_ptr<AnimationRequestStatus>> expressions;
    std::vector<VisualFace> faces;
    std::vector<VisualAttachmentVisibility> attachments;
    std::vector<VisualAttachmentScale> scales;
    std::vector<VisualLayerWeight> weights;
    std::vector<VisualRemoveEventEffect> event_removals;
    std::deque<EffectReceipt> effect_receipts;
    std::vector<std::unique_ptr<std::array<unsigned char, 128>>> effect_delegates;
    std::vector<std::pair<std::string, VisualEffectInput>> effect_inputs;
    int effect_manager = 0;
    bool facing_ready = true;
    bool defer_effect_release = false;
    bool participant_ready = true;
    bool target_ready = true;
    bool accept = true, ready = true;
    bool parameter_ready = true;
    std::string parameter_error;
    std::vector<api::AnimationParameter> parameters;
    std::vector<std::string> submissions;
    std::function<void()> on_attach;
    std::function<void()> on_context_start;
    VisualProgramParameters defaults, captured;
    VisualProgramParameters restoring;
    std::map<const VisualEffectContext*, VisualProgramParameters> scoped_inputs;
    std::vector<VisualProgramParameters> face_inputs;
    std::vector<bool> origin_capture;
    std::uint64_t frame = 0;
    std::map<std::uint64_t, std::uint64_t> participant_generations;
    explicit Fixture(Json document = Document(), bool record_effects = false, std::uint64_t owner_generation = 7,
                     std::shared_ptr<TaskContextRegistry> registry = {}, std::uint64_t character = 7,
                     bool ordinary_program = false, bool forbid_state_native = false) {
        TaskCatalogInput parsed(ParseLegacyTaskCatalog(document.dump()));
        if (ordinary_program) {
            auto visual = std::make_shared<VisualProgram>(*parsed.visual);
            visual->actions["legacy"].state = "StandBy";
            visual->actions["legacy"].restart = true;
            parsed.visual = std::move(visual);
        }
        catalog = std::make_shared<const TaskCatalog>(std::move(parsed));
        VisualProgramServices visual;
        visual.character = character;
        visual.task_contexts = std::move(registry);
        visual.bind_participants = [&](const VisualContextBinding& binding) {
            if (!participant_ready)
                throw std::runtime_error("participant still preparing");
            const auto receiver = participant_generations[binding.receiver],
                       caster = participant_generations[binding.caster];
            return [&, binding, receiver, caster] {
                return participant_generations[binding.receiver] == receiver &&
                       participant_generations[binding.caster] == caster;
            };
        };
        visual.effects = std::make_shared<api::PresentationEffects>(
            api::EffectRequestServices{{}, api::EffectAccess({}), {}, {}, {}});
        visual.remove_event_effect = [&](const VisualRemoveEventEffect& command) {
            event_removals.push_back(command);
            return VisualStepResult{VisualStepState::Done, {}};
        };
        visual.create = [](auto&, const auto&, const auto&) -> VisualEffectId {
            throw std::runtime_error("unexpected effect");
        };
        if (record_effects) {
            // Submit through the real request owner backed by local native
            // functions. This tests sequencing and cleanup, not rendering.
            api::EffectRequestServices requests{
                {EffectMethod(KeepEffect), EffectMethod(GetEffect), EffectMethod(FreeEffect), {}, {}},
                api::EffectAccess({EffectMethod(MapEffect),
                                   EffectMethod(AcquireEffect),
                                   EffectMethod(SubmitEffect),
                                   EffectMethod(RemoveEffect),
                                   EffectMethod(ReleaseEffect),
                                   EffectMethod(EffectId),
                                   EffectMethod(EffectReleased),
                                   EffectMethod(EffectState),
                                   EffectMethod(EffectInstance),
                                   {},
                                   EffectMethod(EffectEmpty)}),
                {[&] {
                     auto delegate = std::make_unique<std::array<unsigned char, 128>>();
                     delegate->fill(0);
                     auto* raw = delegate->data();
                     effect_delegates.push_back(std::move(delegate));
                     return api::ObjectResult{{true, 0}, raw};
                 },
                 {24, 40, 64}},
                [&] { return api::ObjectResult{{true, 0}, &effect_manager}; },
                [](void*, void*, void*) { return api::EffectInputResult{{true, 0}, {true, 0}}; }};
            api::EffectActionServices actions;
            actions.remove = [](void* manager, api::EffectReference ref, const api::EffectRemovalOptions& options) {
                api::EffectRemovalResult result;
                result.removal.read = {true, 0};
                result.removal.value = RemoveEffect(manager, ref, options.fade, nullptr);
                result.input_release = {true, 0};
                return result;
            };
            visual.effects = std::make_shared<api::PresentationEffects>(
                std::move(requests), api::EffectControlServices{}, api::ConfigureEntityEffect{}, std::move(actions));
            visual.create = [&](auto& context, const auto& key, const auto& options) {
                effect_inputs.emplace_back(key, options);
                effect_receipts.push_back(
                    {static_cast<unsigned>(effect_receipts.size() + 1), false, false, !defer_effect_release});
                allocating_effect = &effect_receipts.back();
                return context.Start(key, &effect_manager, &effect_manager);
            };
        }
        visual.attach = [&](auto) {
            if (on_attach)
                on_attach();
        };
        visual.context_started = [&](const auto&, auto, auto) {
            if (on_context_start)
                on_context_start();
        };
        visual.select_inputs = [&] { return defaults; };
        visual.available_targets = [&](const auto& inputs) {
            if (!target_ready)
                return std::vector<std::string>{};
            std::vector<std::string> result{"Caster"};
            for (const auto& [name, id] : inputs.targets) {
                if (id == 777)
                    throw std::runtime_error("unrelated unresolved target");
                if (id == 99)
                    result.push_back(name);
            }
            return result;
        };
        visual.prepare_inputs = [&](auto context, const auto& inputs, bool, bool needs_origin) {
            captured = inputs;
            scoped_inputs[context.get()] = inputs;
            origin_capture.push_back(needs_origin);
        };
        visual.scope_inputs = [&](const auto& context) { return scoped_inputs.at(&context); };
        visual.capture_inputs = visual.scope_inputs;
        visual.restore_inputs_ready = [&](const auto& inputs) {
            restoring = inputs;
            return VisualStepResult{participant_ready ? VisualStepState::Done : VisualStepState::Pending,
                                    "participant preparing"};
        };
        visual.face = [&](auto& context, const VisualFace& command) {
            if (!facing_ready)
                return VisualStepResult{VisualStepState::Pending, "waiting for model"};
            face_inputs.push_back(scoped_inputs.at(&context));
            faces.push_back(command);
            return VisualStepResult{VisualStepState::Done, {}};
        };
        visual.attachment_visibility = [&](auto, const VisualAttachmentVisibility& command) {
            attachments.push_back(command);
            return VisualStepResult{VisualStepState::Done, {}};
        };
        visual.attachment_scale = [&](auto, const VisualAttachmentScale& command) {
            scales.push_back(command);
            return VisualStepResult{VisualStepState::Done, {}};
        };
        visual.layer_weight = [&](auto, const VisualLayerWeight& command) {
            weights.push_back(command);
            return VisualStepResult{VisualStepState::Done, {}};
        };
        if (forbid_state_native) {
            visual.select_inputs = []() -> VisualProgramParameters {
                throw std::runtime_error("state requested native defaults");
            };
            visual.prepare_inputs = [](auto, const auto&, bool, bool) {
                throw std::runtime_error("state prepared native inputs");
            };
            visual.capture_inputs = [](const auto&) -> VisualProgramParameters {
                throw std::runtime_error("state captured native inputs");
            };
            visual.scope_inputs = visual.capture_inputs;
            visual.attach = [](auto) { throw std::runtime_error("state attached native producers"); };
            visual.context_started = [](const auto&, auto, auto) {
                throw std::runtime_error("state entered native context");
            };
            visual.context_stopped = [](const auto&, auto) { throw std::runtime_error("state left native context"); };
            visual.timeline = [](auto, const auto&) -> VisualStepResult {
                throw std::runtime_error("state submitted Timeline");
            };
            visual.cancel_timelines = [](auto scope) {
                if (scope)
                    throw std::runtime_error("state cancelled native Timeline scope");
            };
            visual.timelines_drained = [] { return true; };
            visual.timeline_scope_drained = [](auto scope) {
                if (scope)
                    throw std::runtime_error("state polled native Timeline scope");
                return true;
            };
            visual.partner = [](auto, const auto&) -> VisualStepResult {
                throw std::runtime_error("state submitted partner");
            };
            visual.end_partners = [](auto scope) {
                if (scope)
                    throw std::runtime_error("state ended native partner scope");
            };
            visual.partners_drained = [] { return true; };
            visual.partner_scope_drained = [](auto) -> bool {
                throw std::runtime_error("state polled native partner scope");
            };
            visual.read_phase = [](const auto&) -> std::shared_ptr<const api::PhaseVisualConfigSources> {
                throw std::runtime_error("state read native phase");
            };
        }
        scene = std::make_shared<VisualProgramPlayer>(catalog->visual, std::move(visual));
        TaskPresentationServices ports;
        ports.parameter = [&](const TaskParticipant& participant, const api::AnimationParameter& parameter,
                              api::AnimationParameterPropagation) -> std::optional<api::AnimationParameterResult> {
            Check(participant.name == "body");
            if (!parameter_ready)
                return {};
            if (!parameter_error.empty())
                throw std::runtime_error(parameter_error);
            parameters.push_back(parameter);
            submissions.push_back("parameter:" + parameter.name);
            api::AnimationParameterResult result;
            result.state = api::AnimationParameterState::Applied;
            result.body_attempted = result.body.returned = result.owner_current = true;
            result.model_identity = 101;
            return result;
        };
        ports.pose =
            PoseRequestEndpoint{7,
                                [&](const PoseSelection& selection) -> std::shared_ptr<const AnimationRequestStatus> {
                                    if (!ready)
                                        return {};
                                    auto value = std::make_shared<AnimationRequestStatus>();
                                    value->entity_generation = 7;
                                    value->serial = poses.size() + 1;
                                    value->model_generation = 8;
                                    value->accepted = accept;
                                    if (accept)
                                        value->accepted_effect = std::visit([](const auto& pose) -> AcceptedPlaybackEffect {
                                            using T = std::decay_t<decltype(pose)>;
                                            if constexpr (std::is_same_v<T, ReadyPoseSelection>)
                                                return NormalBodyAccepted{};
                                            else if (pose.recover)
                                                return ReactionRecovered{pose.source, pose.flag};
                                            else
                                                return ReactionEntered{pose.source, pose.flag, pose.occurrence};
                                        }, selection);
                                    poses.emplace_back(selection, value);
                                    return value;
                                },
                                [&](auto serial, bool cancel) {
                                    auto& state = *poses.at(serial - 1).second;
                                    state.released = true;
                                    state.cancelled = cancel;
                                }};
        ports.animation = [&](const auto& participant) -> std::optional<TaskAnimationEndpoint> {
            if (participant.name != "body")
                throw std::invalid_argument("unknown participant");
            if (!ready)
                return {};
            return TaskAnimationEndpoint{7,
                                         [&](const auto& command) {
                                             submissions.push_back("animation:" + command.state);
                                             auto value = std::make_shared<AnimationRequestStatus>();
                                             value->entity_generation = 7;
                                             value->serial = plays.size() + 1;
                                             value->model_generation = 8;
                                             value->accepted = accept;
                                             if (accept && command.layer == 0)
                                                 value->accepted_effect = NormalBodyAccepted{};
                                             plays.emplace_back(command, value);
                                             return value;
                                         },
                                         [&](auto serial) { plays.at(serial - 1).second->released = true; },
                                         [&](const std::string&, float) {
                                             auto value = std::make_shared<AnimationRequestStatus>();
                                             value->entity_generation = 7;
                                             value->serial = expressions.size() + 1;
                                             value->model_generation = 8;
                                             value->accepted = value->complete = true;
                                             expressions.push_back(value);
                                             return value;
                                         },
                                         [&](auto serial) { expressions.at(serial - 1)->released = true; }};
        };
        controller = std::make_unique<TaskController>(owner_generation, catalog, scene, std::move(ports));
    }
    TaskSelectionResult Select(const char* name) {
        return controller->Select({{"test", name}});
    }
    void Tick(double seconds = 0) {
        controller->Service(seconds, ++frame);
    }
    ~Fixture() {
        controller->Close(TaskExitReason::Deleted);
        Tick();
        Tick();
    }
};
void Codec() {
    const auto input = Document();
    const auto parsed = ParseLegacyTaskCatalog(input.dump());
    auto wait = input;
    wait["sequences"]["stand"]["entry"] = {{"op", "wait_seconds"}, {"seconds", 0.5}};
    Check(std::get<TaskWaitSeconds>(ParseLegacyTaskCatalog(wait.dump()).sequences.at("stand")->nodes.front().operation)
              .seconds == 0.5);
    wait["sequences"]["stand"]["entry"]["realtime"] = true;
    Reject([&] { ParseLegacyTaskCatalog(wait.dump()); });
    auto hit = input;
    hit["sequences"]["stand"]["entry"] = {
        {"op", "hit"}, {"request", "impact"}, {"target", "Victim"}, {"animation", "DamageHit_H"}};
    auto hit_catalog = ParseLegacyTaskCatalog(hit.dump());
    const auto& impact = std::get<TaskHitCommand>(
        std::get<TaskSubmit>(hit_catalog.sequences.at("stand")->nodes.front().operation).operation);
    Check(impact.target == "Victim" && impact.hit.animation == "DamageHit_H");
    hit["sequences"]["stand"]["entry"]["direction"] = {{"horizontal", 30}, {"vertical", 10}};
    auto directed = ParseLegacyTaskCatalog(hit.dump());
    const auto& directed_hit =
        std::get<TaskHitCommand>(
            std::get<TaskSubmit>(directed.sequences.at("stand")->nodes.front().operation).operation)
            .hit;
    Check(directed_hit.direction && directed_hit.direction->horizontal == 30 &&
          directed_hit.direction->vertical == 10 && !directed_hit.origin);
    hit["sequences"]["stand"]["entry"]["effect"] = {{"path", ""},
                                                    {"height", -1},
                                                    {"angle", 0},
                                                    {"delay", 0},
                                                    {"minimum_interval", .05},
                                                    {"maximum_count", 1},
                                                    {"offset_by_scale", false}};
    const auto effect_catalog = ParseLegacyTaskCatalog(hit.dump());
    const auto& effect_hit =
        std::get<TaskHitCommand>(
            std::get<TaskSubmit>(effect_catalog.sequences.at("stand")->nodes.front().operation).operation)
            .hit;
    Check(effect_hit.effect && effect_hit.effect->path.empty() && effect_hit.effect->height == -1 &&
          effect_hit.effect->maximum_count == 1);
    hit["sequences"]["stand"]["entry"]["origin"] = {{"x", 0}, {"y", 0}, {"z", 0}};
    Reject([&] { ParseLegacyTaskCatalog(hit.dump()); });
    hit["sequences"]["stand"]["entry"].erase("origin");
    hit["sequences"]["stand"]["entry"]["state"] = "ForceAnyAnimator";
    Reject([&] { ParseLegacyTaskCatalog(hit.dump()); });
    hit["sequences"]["stand"]["entry"].erase("state");
    hit["sequences"]["stand"]["entry"]["animation"] = "";
    ParseLegacyTaskCatalog(hit.dump());
    Check(parsed.tasks.size() == 5 && parsed.visual->actions.empty() && parsed.sequences.size() == 3);
    Check(parsed.Resolve({"test", "hold"}).definition.sources.at(0).field == "Modifier");
    Check(parsed.contexts.at("enhanced").values.size() == 1);
    auto ready_binding = input;
    ready_binding["tasks"]["idle"]["presentation"] = {
        {"ready", {{"config", "Battle/A"}, {"skill", 2}, {"variant", 3}}}};
    const auto ready_catalog = ParseLegacyTaskCatalog(ready_binding.dump());
    const auto& binding = *ready_catalog.tasks.at("idle").definition.presentation->ready;
    Check(binding.source == "Battle/A" && binding.skill == 2 && binding.variant == 3);
    ready_binding["tasks"]["idle"]["presentation"]["ready"]["variant"] = -2;
    Reject([&] { ParseLegacyTaskCatalog(ready_binding.dump()); });
    ready_binding["tasks"]["idle"]["presentation"]["ready"]["variant"] = -1;
    ready_binding["tasks"]["idle"]["presentation"]["ready"]["skill"] = -1;
    Reject([&] { ParseLegacyTaskCatalog(ready_binding.dump()); });
    auto shown = input;
    shown["tasks"]["attack"]["inputs"] = {
        {"event",
         {{"type", "integer"},
          {"default", 0},
          {"label", "Callback"},
          {"description", "Select a visual callback"},
          {"choices",
           Json::array({{{"value", 0}, {"label", "Hit"}}, {{"value", 1}, {"label", "Exit"}, {"diagnostic", true}}})}}}};
    shown["tasks"]["attack"]["presentation"] = {{"label", "Skill script"},
                                                {"omissions", Json::array({"Extra effects not included"})}};
    shown["tasks"]["idle"]["presentation"] = {{"listed", false}};
    auto published = ParseLegacyTaskCatalog(shown.dump());
    const auto& callback = published.tasks.at("attack").definition.inputs.front();
    Check(callback.label == "Callback" && callback.description == "Select a visual callback" &&
          callback.choices.size() == 2);
    Check(std::get<std::int64_t>(callback.choices[1].value) == 1 && callback.choices[1].label == "Exit" &&
          callback.choices[1].diagnostic);
    Check(!callback.choices[0].diagnostic);
    auto malformed_diagnostic = shown;
    malformed_diagnostic["tasks"]["attack"]["inputs"]["event"]["choices"][1]["diagnostic"] = "yes";
    Reject([&] { ParseLegacyTaskCatalog(malformed_diagnostic.dump()); });
    auto diagnostic_selection = TaskQuery{};
    diagnostic_selection.inputs["event"] = std::int64_t(1);
    Check(TaskContext{}.Query(published.tasks.at("attack").definition, diagnostic_selection).availability ==
          TaskAvailability::Available);
    auto bad_choice = shown;
    bad_choice["tasks"]["attack"]["inputs"]["event"]["choices"][1]["value"] = 0;
    Reject([&] { ParseLegacyTaskCatalog(bad_choice.dump()); });
    Check(published.tasks.at("attack").definition.presentation->listed);
    auto view = std::make_shared<CharacterTaskStatus>();
    view->domain = ActionDomain::Battle;
    for (const auto& [name, plan] : published.tasks) {
        view->definitions[name] = plan.definition;
        view->control.catalog[name] = {};
    }
    ApplicationSnapshot snapshot;
    SceneCharacterStatus actor;
    actor.id = 7;
    actor.active = actor.enabled = true;
    actor.tasks = view;
    actor.task_catalog = published.id;
    snapshot.characters.push_back(actor);
    const auto directory = CharacterTasks(snapshot, 7);
    const auto script =
        std::find_if(directory.begin(), directory.end(), [](const auto& row) { return row.label == "Skill script"; });
    Check(script != directory.end() && !script->diagnostic && script->detail == "Extra effects not included");
    const auto hidden =
        std::find_if(directory.begin(), directory.end(), [](const auto& row) { return row.label == "idle"; });
    Check(hidden != directory.end() && hidden->diagnostic);
    {
        auto settings = input;
        settings["visual"]["contexts"]["response"] = {{"restore", Json::array()}};
        settings["contexts"]["response"] = {{"resilience", 0}, {"callbacks", Json::object()}};
        settings["sequences"]["setting"]["entry"] = {
            {"op", "serial"},
            {"children", Json::array({{{"op", "branch"},
                                       {"condition", {{"op", "value"}, {"name", "enabled"}, {"is", true}}},
                                       {"yes", {{"op", "context_enter"}, {"name", "response"}, {"request", "setting"}}},
                                       {"no", {{"op", "context_exit"}, {"name", "response"}, {"request", "setting"}}}},
                                      {{"op", "wait_request"}, {"request", "setting"}, {"signal", "performance"}}})}};
        settings["tasks"]["setting"] = {
            {"entry", "setting"},
            {"inputs", {{"enabled", {{"type", "bool"}, {"default", true}}}}},
            {"mode", "input"},
            {"presentation", {{"kind", "hit_response"}, {"contexts", Json::array({"response"})}}}};
        Fixture receiver(settings);
        receiver.Select("attack");
        receiver.Tick();
        const auto plays = receiver.plays.size();
        receiver.Select("setting");
        for (int i = 0; i < 6; ++i)
            receiver.Tick();
        Check(receiver.controller->Status().current->name == "attack" && receiver.plays.size() == plays &&
              receiver.controller->ReactionState().resilience == 0);
        receiver.Select("idle");
        for (int i = 0; i < 6; ++i)
            receiver.Tick();
        Check(receiver.controller->ReactionState().resilience == 0);
        TaskSelection off{{"test", "setting"}};
        off.query.inputs["enabled"] = false;
        receiver.controller->Select(off);
        for (int i = 0; i < 6; ++i)
            receiver.Tick();
        Check(!receiver.controller->ReactionState().resilience);
    }
    shown["tasks"]["attack"]["presentation"]["omissions"] = true;
    Reject([&] { ParseLegacyTaskCatalog(shown.dump()); });
    auto sound = input;
    sound["sequences"]["stand"]["entry"] = {
        {"op", "serial"},
        {"children",
         Json::array({{{"op", "sound"}, {"request", "voice"}, {"participant", "body"}, {"event", "voice_event"}},
                      {{"op", "wait_request"}, {"request", "voice"}, {"signal", "started"}},
                      {{"op", "wait_request"}, {"request", "voice"}, {"signal", "performance"}}})}};
    const auto with_sound = ParseLegacyTaskCatalog(sound.dump());
    const auto& sound_nodes = with_sound.sequences.at("stand")->nodes;
    Check(std::get<TaskSoundCommand>(std::get<TaskSubmit>(sound_nodes.at(0).operation).operation).event ==
          "voice_event");
    Check(std::get<TaskWaitRequest>(sound_nodes.at(1).operation).signal == TaskWaitSignal::Started);
    sound["sequences"]["stand"]["entry"]["children"][0]["event"] = "";
    Reject([&] { ParseLegacyTaskCatalog(sound.dump()); });
    auto movement = input;
    movement["sequences"]["stand"]["entry"] = {
        {"op", "serial"},
        {"children",
         Json::array({{{"op", "animation"}, {"request", "body"}, {"participant", "body"}, {"state", "Attack"}},
                      {{"op", "movement"},
                       {"request", "move"},
                       {"animation", "body"},
                       {"clock", "animation"},
                       {"segments", Json::array({{{"destination", "target"},
                                                  {"name", "Target"},
                                                  {"stop_distance", 1.5},
                                                  {"start", 0},
                                                  {"end", 0.4}},
                                                 {{"destination", "anchor"},
                                                  {"name", "Return"},
                                                  {"offset", {0, 0, 1}},
                                                  {"start", 0.8},
                                                  {"end", 1.2}}})}}})}};
    const auto with_move = ParseLegacyTaskCatalog(movement.dump());
    const auto& move = std::get<TaskMovementCommand>(
        std::get<TaskSubmit>(with_move.sequences.at("stand")->nodes[1].operation).operation);
    Check(move.animation == "body" && move.movement.state.empty() &&
          move.movement.segments[1].destination == MovementDestination::Anchor);
    auto missing = movement;
    missing["sequences"]["stand"]["entry"]["children"][1]["animation"] = "not-declared";
    Reject([&] { ParseLegacyTaskCatalog(missing.dump()); });
    missing = movement;
    missing["sequences"]["stand"]["entry"]["children"][0]["layer"] = 3;
    Reject([&] { ParseLegacyTaskCatalog(missing.dump()); });
    missing = movement;
    missing["sequences"]["stand"]["entry"]["children"][1]["segments"][0]["end"] = 0;
    Reject([&] { ParseLegacyTaskCatalog(missing.dump()); });
    missing = movement;
    missing["sequences"]["stand"]["entry"]["children"][1]["segments"][0]["destination"] = "origin";
    Reject([&] { ParseLegacyTaskCatalog(missing.dump()); });
    auto configured = input;
    configured["tasks"]["attack"]["bindings"] = {
        {"targets", {"Target"}}, {"anchors", {"Destination"}}, {"choices", {{"mode", {"low", "high"}}}}};
    configured["tasks"]["attack"]["requires"] = Json::array({{{"op", "target"}, {"name", "Target"}},
                                                             {{"op", "target"}, {"name", "Other"}},
                                                             {{"op", "target"}, {"name", "Caster"}},
                                                             {{"op", "choice"}, {"name", "branch"}, {"is", "yes"}}});
    const auto binding_catalog = ParseLegacyTaskCatalog(configured.dump());
    const auto& definition = binding_catalog.Resolve({"test", "attack"}).definition;
    const auto bindings = TaskBindings(definition);
    Check(bindings.targets == std::vector<std::string>({"Target", "Other"}) && bindings.anchors.at(0) == "Destination");
    Check(bindings.choices.at("mode").size() == 2 && bindings.choices.at("branch").at(0) == "yes");
    auto collections = input;
    collections["sequences"]["attack"]["entry"] = {{"op", "for_targets"},
                                                   {"collection", "recipients"},
                                                   {"bind", "Member"},
                                                   {"targets", {"Caster"}},
                                                   {"child", {{"op", "wait_frame"}}}};
    const auto collection_catalog = ParseLegacyTaskCatalog(collections.dump());
    Check(TaskBindings(collection_catalog.tasks.at("attack").definition).collections ==
          std::vector<std::string>{"recipients"});
    TaskQuery query;
    query.targets = {"Target", "Other", "Caster"};
    query.choices = {{"mode", "invalid"}, {"branch", "yes"}};
    Check(TaskContext{}.Query(definition, query).availability == TaskAvailability::Unavailable);
    query.choices["mode"] = "low";
    Check(TaskContext{}.Query(definition, query).availability == TaskAvailability::Available);
    auto invalid_bindings = configured;
    invalid_bindings["tasks"]["attack"]["bindings"]["targets"] = {"Caster"};
    Reject([&] { ParseLegacyTaskCatalog(invalid_bindings.dump()); });
    invalid_bindings = configured;
    invalid_bindings["tasks"]["attack"]["bindings"]["choices"]["mode"] = Json::array();
    Reject([&] { ParseLegacyTaskCatalog(invalid_bindings.dump()); });
    auto bad = input;
    bad["sequences"]["stand"]["entry"]["layer"] = -1;
    Reject([&] { ParseLegacyTaskCatalog(bad.dump()); });
    bad = input;
    bad["tasks"]["attack"]["inputs"]["power"]["default"] = true;
    Reject([&] { ParseLegacyTaskCatalog(bad.dump()); });
    bad = input;
    bad["tasks"]["attack"]["next"]["task"] = "missing";
    Reject([&] { ParseLegacyTaskCatalog(bad.dump()); });
    bad = input;
    bad["tasks"]["attack"]["next"] = {{"task", "attack"}, {"inputs", {{"power", "wrong"}}}};
    Reject([&] { ParseLegacyTaskCatalog(bad.dump()); });
    bad = input;
    bad["sequences"]["attack"]["entry"]["children"][2]["children"][0]["request"] = "missing";
    Reject([&] { ParseLegacyTaskCatalog(bad.dump()); });
    bad = input;
    bad["contexts"]["missing"] = Json::object();
    Reject([&] { ParseLegacyTaskCatalog(bad.dump()); });
    auto composed = input;
    composed["visual"]["actions"] = {{"legacy", {{"state", "Legacy"}}}};
    Check(ParseLegacyTaskCatalog(composed.dump()).visual->actions.at("legacy").state == "Legacy");
    bad = input;
    bad["visual"]["actions"] = {{"fake", {{"state", ""}}}};
    Reject([&] { ParseLegacyTaskCatalog(bad.dump()); });
    bad = input;
    bad["sequences"]["stand"]["entry"]["unexpected"] = true;
    Reject([&] { ParseLegacyTaskCatalog(bad.dump()); });
    bad = input;
    bad["sequences"]["stand"]["entry"] = {{"op", "repeat"}, {"child", {{"op", "wait_frame"}}}};
    Reject([&] { ParseLegacyTaskCatalog(bad.dump()); });
    bad = input;
    bad["tasks"]["attack"]["inputs"]["power"]["default"] = std::numeric_limits<std::uint64_t>::max();
    Reject([&] { ParseLegacyTaskCatalog(bad.dump()); });
    // The same resource parser still serves the original action-clock format.
    auto legacy = input["visual"];
    legacy["format"] = "hksr.visual-program";
    legacy["actions"] = {{"idle", {{"state", "StandBy"}}}};
    Check(ParseLegacyVisualProgram(legacy.dump()).actions.at("idle").state == "StandBy");
}
void SelectionAndInterrupt() {
    Fixture f;
    Check(f.Select("attack").intent == 1);
    f.Tick();
    Check(f.plays.size() == 2 && f.controller->Status().current->name == "attack");
    const auto denied = f.Select("unmapped");
    Check(!denied.intent && denied.query.availability == TaskAvailability::Unknown);
    Check(f.controller->Status().requested == 1 && f.controller->Status().current->name == "attack");
    Check(f.Select("idle").intent == 2);
    f.Tick(0.05);
    f.Tick(0.05);
    Check(f.plays.size() == 2 && f.controller->Status().execution->phase == TaskExecutionPhase::Interrupting);
    // Re-selection does not abort the already-running interruption branch.
    Check(f.Select("idle").intent == 3);
    f.Tick(0.05);
    Check(f.plays.size() == 2);
    f.Tick(0.06);
    Check(f.plays.size() == 4);
    Check(f.plays[2].first.state == "Exit" && f.plays[3].first.state == "StandBy");
    Check(f.controller->Status().started == 3);
    f.Tick();
    Check(f.controller->Status().retiring == 0);
}
void AnimationOffsets() {
    auto document = Document();
    auto& command = document["sequences"]["stand"]["entry"];
    command["start"] = 0.375;
    command["transition"] = 0.2;
    Fixture f(document);
    f.Select("idle");
    f.Tick();
    Check(f.plays.size() == 1 && f.plays[0].first.normalized_start == 0.375f && f.plays[0].first.transition == 0.2f);
    Check(!f.plays[0].second->entered && !f.plays[0].second->progress);
    command["start"] = "0.375";
    Reject([&] { ParseLegacyTaskCatalog(document.dump()); });
    command["start"] = 1e100;
    Reject([&] { ParseLegacyTaskCatalog(document.dump()); });
}
void NaturalAndPending() {
    Fixture f;
    f.defaults.targets["Target"] = 99;
    f.defaults.choices["mode"] = "base";
    TaskSelection override_input{{"test", "attack"}};
    override_input.visual.choices["mode"] = "explicit";
    f.controller->Select(override_input);
    f.Tick();
    Check(f.captured.targets.at("Target") == 99 && f.captured.choices.at("mode") == "explicit");
    f.plays[0].second->complete = true;
    f.Tick();
    Check(f.plays.size() == 2);
    f.plays[1].second->complete = true;
    f.Tick();
    Check(f.plays.size() == 3 && f.plays.back().first.state == "StandBy");
    Check(f.controller->Status().current->name == "idle");
    Check(f.controller->Selection()->task.name == "idle" &&
          f.controller->Selection()->visual.targets.at("Target") == 99 &&
          f.controller->Selection()->visual.choices.at("mode") == "explicit");
    const auto saved = *f.controller->Selection();
    Fixture restored;
    restored.defaults.targets["Target"] = 100;
    restored.defaults.choices["mode"] = "new-default";
    restored.controller->Select(saved);
    restored.Tick();
    Check(saved.inputs_captured && restored.captured.targets.at("Target") == 99 &&
          restored.captured.choices.at("mode") == "explicit");
    f.controller->Cancel();
    Check(!f.controller->Selection());
    Fixture delayed;
    delayed.accept = false;
    delayed.Select("idle");
    delayed.Tick();
    delayed.Tick();
    Check(delayed.plays.size() == 1 && !delayed.controller->Drained());
    delayed.plays[0].second->accepted = true;
    delayed.Tick();
    delayed.Tick();
    Check(delayed.controller->Drained());
    Check(delayed.controller->Status().execution &&
          delayed.controller->Status().execution->phase == TaskExecutionPhase::Finished &&
          delayed.controller->Status().execution->retained_requests == 0 && delayed.controller->Requests().empty());
}
void ContextsAndFinish() {
    Fixture f;
    auto base = f.controller->Context().Enter("user");
    f.controller->Context().SetValue(base, "charged", false);
    Check(f.controller->Query({{"test", "conditioned"}}).query.availability == TaskAvailability::Unavailable);
    f.Select("hold");
    f.Tick();
    f.Tick();
    Check(f.controller->Context().Phase() == "enhanced");
    Check(f.controller->Query({{"test", "conditioned"}}).query.availability == TaskAvailability::Available);
    f.controller->RequestFinish();
    f.Tick();
    f.Tick();
    Check(!f.controller->Context().Phase());
    Check(std::get<bool>(f.controller->Context().Values().at("charged").value) == false);
    Check(f.controller->Drained());
    Fixture immediate;
    immediate.Select("hold");
    immediate.controller->RequestFinish();
    immediate.Tick();
    immediate.Tick();
    immediate.Tick();
    Check(immediate.controller->Drained() && !immediate.controller->Context().Phase());
}
void CloseAndReentrant() {
    Fixture f;
    f.ready = false;
    f.Select("attack");
    f.Tick();
    Check(f.plays.empty());
    f.controller->Close(TaskExitReason::WorldLeft);
    f.ready = true;
    f.Tick();
    f.Tick();
    Check(f.plays.empty() && f.controller->Drained());
    Reject([&] { f.Select("idle"); });
    Fixture nested;
    nested.on_attach = [&] { nested.controller->Close(TaskExitReason::Deleted); };
    nested.Select("attack");
    nested.Tick();
    nested.Tick();
    Check(nested.plays.empty() && nested.controller->Drained());
    Fixture broken;
    broken.on_attach = [] { throw std::runtime_error("scope preparation failed"); };
    broken.Select("attack");
    broken.Tick();
    broken.Tick();
    Check(broken.plays.empty() && broken.controller->Status().execution->reason == TaskExitReason::Failed);
    Check(broken.controller->Status().execution->issues.at(0).find("scope preparation failed") != std::string::npos);
    Check(broken.controller->Drained());
}
void InputRequirementsAndPreparation() {
    auto document = Document();
    document["tasks"]["idle"]["requires"] = {{{"op", "target"}, {"name", "Target"}},
                                             {{"op", "anchor"}, {"name", "Stage"}},
                                             {{"op", "choice"}, {"name", "variant"}, {"is", "a"}}};
    Fixture f(document);
    const auto missing = f.controller->Query({{"test", "idle"}});
    Check(missing.query.availability == TaskAvailability::Unavailable && missing.query.reasons.size() == 3);
    f.defaults.targets["Unused"] = 777;
    f.defaults.targets["Target"] = 99;
    f.defaults.anchors["Stage"] = {};
    f.defaults.choices["variant"] = "a";
    Check(f.controller->Query({{"test", "idle"}}).query.availability == TaskAvailability::Available);
    TaskSelection selected{{"test", "idle"}};
    selected.visual.targets["Target"] = 100;
    selected.query.targets = {"Target"}; // caller cannot manufacture owned target availability
    Check(f.controller->Select(selected).query.availability == TaskAvailability::Unavailable);
    selected.visual.targets["Target"] = 99;
    Check(f.controller->Select(selected).intent == 1);
    f.controller->Service(3, ++f.frame, false);
    Check(f.plays.empty() && f.controller->Status().pending);
    f.controller->Service(3, ++f.frame, false);
    Check(f.plays.empty());
    f.Tick();
    Check(f.plays.size() == 1 && f.captured.targets.at("Target") == 99);
    Fixture changed(document);
    changed.defaults = f.defaults;
    changed.Select("idle");
    changed.defaults.targets["Target"] = 0;
    changed.Tick();
    Check(changed.plays.empty() && changed.controller->Status().blocked->availability == TaskAvailability::Unavailable);
    auto branch_document = Document();
    branch_document["sequences"]["stand"]["entry"] = {
        {"op", "serial"},
        {"children",
         Json::array(
             {{{"op", "wait_frame"}},
              {{"op", "branch"},
               {"condition", {{"op", "choice"}, {"name", "mode"}, {"is", "a"}}},
               {"yes", {{"op", "animation"}, {"request", "branch"}, {"participant", "body"}, {"state", "A"}}},
               {"no", {{"op", "animation"}, {"request", "branch"}, {"participant", "body"}, {"state", "B"}}}}})}};
    Fixture frozen(branch_document);
    frozen.defaults.choices["mode"] = "a";
    frozen.Select("idle");
    frozen.Tick();
    frozen.defaults.choices["mode"] = "b";
    frozen.Tick();
    Check(frozen.plays.size() == 1 && frozen.plays[0].first.state == "A" && frozen.captured.choices.at("mode") == "a");
    auto grouped = Document();
    grouped["sequences"]["stand"]["entry"] = {{"op", "for_targets"},
                                              {"bind", "Member"},
                                              {"collection", "Neighbours"},
                                              {"targets", {"Fallback"}},
                                              {"child", {{"op", "wait_frame"}}}};
    Fixture restore(grouped);
    TaskSelection saved{{"test", "idle"}};
    saved.visual.targets = {{"Target", 99}, {"Fallback", 98}, {"Unused", 777}};
    saved.visual.target_aliases["Alias"] = "Target";
    saved.visual.target_collections["Neighbours"] = {"Alias"};
    restore.controller->RestoreInputsReady(saved);
    Check(restore.restoring.targets == std::map<std::string, std::uint64_t>{{"Target", 99}});
    saved.visual.target_collections["Neighbours"] = {};
    restore.controller->RestoreInputsReady(saved);
    Check(restore.restoring.targets.empty());
    saved.visual.target_collections.clear();
    restore.controller->RestoreInputsReady(saved);
    Check(restore.restoring.targets == std::map<std::string, std::uint64_t>{{"Fallback", 98}});
}
}
void TestTaskScriptFlow(const char* file) {
    std::ifstream input(file, std::ios::binary);
    if (!input)
        throw std::runtime_error("script catalog absent");
    const auto document = Json::parse(input);
    const auto catalog = document.value("format", std::string{}) == "hksr.script"
        ? *ReadScriptFile(file)->tasks : ParseLegacyTaskCatalog(document.dump());
    Json results = Json::array();
    std::size_t cases = 0, settled = 0, empty = 0;
    std::set<std::tuple<std::string, std::string, int>> return_animations;
    for (const auto& [key, sequence] : catalog.sequences)
        if (key.compare(0, 8, "@return.") == 0)
            for (const auto& node : sequence->nodes)
                if (const auto* submit = std::get_if<TaskSubmit>(&node.operation))
                    if (const auto* animation = std::get_if<TaskAnimationCommand>(&submit->operation))
                        return_animations.emplace(submit->request, animation->state, animation->layer);
    for (const auto& [name, plan] : catalog.tasks) {
        const auto& definition = plan.definition;
        if (std::none_of(definition.tags.begin(), definition.tags.end(), [](const auto& tag) {
                return tag == "official-script" || tag == "manual-visual-callback" || tag == "manual-effect-cue";
            }))
            continue;
        std::vector<std::pair<std::string, TaskValues>> variants = {{"default", {}}};
        for (const auto& field : definition.inputs)
            for (const auto& choice : field.choices) {
                if (field.default_value && choice.value == *field.default_value)
                    continue;
                variants.push_back({field.name + "=" + choice.label, {{field.name, choice.value}}});
            }
        const auto base_variants = variants.size();
        for (std::size_t variant_index = 0; variant_index < variants.size(); ++variant_index) {
            const auto [variant, values] = variants[variant_index];
            ++cases;
            TaskContext context;
            TaskQuery query;
            query.inputs = values;
            for (const auto& field : definition.inputs)
                if (!query.inputs.count(field.name) && field.default_value)
                    query.inputs.emplace(field.name, *field.default_value);
            std::size_t presentations = 0;
            std::uint64_t token = cases;
            TaskSequenceServices services;
            std::set<std::string> read_booleans;
            services.condition = [&](const TaskRequirement& requirement) {
                if (const auto* test = std::get_if<TaskValueEquals>(&requirement))
                    if (const auto found = query.inputs.find(test->name);
                        found != query.inputs.end() && std::holds_alternative<bool>(found->second))
                        read_booleans.insert(test->name);
                return context.Evaluate(requirement, query).availability;
            };
            services.targets = [](const std::string&) -> std::optional<std::vector<std::string>> { return {}; };
            services.release = [](const auto&, auto) {};
            services.submit = [&](TaskExecutionToken execution, std::uint64_t request, const TaskSubmit& command) {
                TaskSubmission out;
                out.receipt = {execution, request, 1, {"body"}, 1};
                auto& state = out.state;
                state.submitted = state.request_complete = state.performance_complete = state.drained = true;
                state.started = state.animation_end = state.state_left = state.animation_finished = true;
                state.progress = 1;
                std::visit(
                    [&](const auto& op) {
                        using T = std::decay_t<decltype(op)>;
                        if constexpr (std::is_same_v<T, TaskAnimationCommand>) {
                            // End is a track handoff threshold, not clip duration.
                            // With no successor the observer can continue to 1.
                            if (!return_animations.count({command.request, op.state, op.layer}))
                                ++presentations;
                            state.progress = op.normalized_end ? std::max(1.0, double(*op.normalized_end)) : 1.0;
                        } else if constexpr (std::is_same_v<T, TaskMovementCommand>) {
                            if (!op.movement.instant)
                                ++presentations;
                        } else if constexpr (std::is_same_v<T, VisualInstruction>) {
                            if (!std::holds_alternative<VisualRemove>(op.operation))
                                ++presentations;
                        } else if constexpr (std::is_same_v<T, TaskLifetimeCommand>) {
                            if (op.operation != TaskLifetimeOperation::Exit &&
                                !catalog.contexts.at(op.context).state_only)
                                ++presentations;
                        } else if constexpr (!std::is_same_v<T, TaskExpressionCommand>)
                            ++presentations;
                    },
                    command.operation);
                return out;
            };
            TaskSequenceExecution execution({token}, catalog.sequences.at(plan.definition.entry), std::move(services));
            for (std::uint64_t frame = 0; frame < 600 && execution.Status().phase != TaskExecutionPhase::Finished;
                 ++frame) {
                if (frame == 8)
                    execution.RequestFinish();
                execution.Advance(.1, frame + 1);
            }
            const auto state = execution.Status();
            const bool done = state.phase == TaskExecutionPhase::Finished && state.issues.empty() && state.reason &&
                              *state.reason == TaskExitReason::Completed;
            if (done)
                ++settled;
            if (!presentations && variant == "default")
                ++empty;
            if (!done || !presentations)
                results.push_back({{"task", name},
                                   {"variant", variant},
                                   {"settled", done},
                                   {"presentation_submissions", presentations},
                                   {"issues", state.issues},
                                   {"waiting", state.waiting}});
            // Exercise each selected callback's consumed boolean inputs, not
            // only the default callback. Do not imply exhaustive combinations.
            if (variant_index < base_variants)
                for (const auto& boolean : read_booleans) {
                    auto changed = values;
                    changed[boolean] = !std::get<bool>(query.inputs.at(boolean));
                    variants.push_back({variant + ";" + boolean + "=toggle", std::move(changed)});
                }
            execution.Stop(TaskExitReason::Deleted);
        }
    }
    std::cout << Json({{"catalog", catalog.id},
                       {"cases", cases},
                       {"settled", settled},
                       {"empty", empty},
                       {"findings", results}})
                     .dump()
              << "\n";
}

void TestTaskControlScript(const char* file) {
    std::ifstream input(file, std::ios::binary);
    if (!input)
        throw std::runtime_error("task control script absent");
    const auto script = Json::parse(input);
    Fixture fixture(script.at("catalog"), true, 7, std::make_shared<TaskContextRegistry>());
    fixture.controller->PrepareState();
    const auto id = fixture.catalog->id;
    std::size_t step = 0;
    for (const auto& row : script.at("steps")) {
        ++step;
        const auto effects_before = fixture.effect_inputs.size();
        const auto task = row.at("task").get<std::string>();
        TaskSelection selection{{id, task}};
        const auto query = fixture.controller->Query(selection);
        const auto expected = row.value("available", true);
        if ((query.query.availability == TaskAvailability::Available) != expected)
            throw std::runtime_error("control script availability step " + std::to_string(step) + " " + task);
        if (!expected)
            continue;
        if (!fixture.controller->Select(selection).intent)
            throw std::runtime_error("control script selection rejected");
        // This harness tests sourced logical operations and callback effects.
        // Its input explicitly removes body/geometry clocks; no rendering claim.
        for (int frame = 0; frame < 48; ++frame) {
            if (row.value("finish", false) && frame == 8)
                fixture.controller->RequestFinish();
            fixture.Tick(.1);
        }
        if (row.value("effects_released", false))
            for (const auto& receipt : fixture.effect_receipts)
                if (!receipt.released)
                    throw std::runtime_error("control script retained an effect after finish");
        const auto state = fixture.controller->Status();
        if (!state.error.empty())
            throw std::runtime_error("control script step " + std::to_string(step) + ": " + state.error);
        if (state.execution && !state.execution->issues.empty())
            throw std::runtime_error("control script step " + std::to_string(step) + ": " +
                                     state.execution->issues.front());
        if (state.input) {
            if (!state.input->error.empty())
                throw std::runtime_error(state.input->error);
            if (state.input->execution && !state.input->execution->issues.empty())
                throw std::runtime_error(state.input->execution->issues.front());
        }
        if (row.contains("restore_selection")) {
            const auto saved = fixture.controller->Selection();
            if (!saved || saved->task.name != row.at("restore_selection").get<std::string>())
                throw std::runtime_error("completed task did not capture its stable successor");
            Fixture restored(script.at("catalog"), true, 8, std::make_shared<TaskContextRegistry>());
            restored.scene->Restore(fixture.scene->Capture());
            for (int frame = 0; frame < 12; ++frame)
                restored.Tick(.1);
            if (!restored.controller->RestoreSelection(*saved).intent)
                throw std::runtime_error("stable successor cannot be restored without prior skill selection");
            for (int frame = 0; frame < 48; ++frame)
                restored.Tick(.1);
            if (!restored.controller->Selection() ||
                restored.controller->Selection()->task.catalog != saved->task.catalog ||
                restored.controller->Selection()->task.name != saved->task.name ||
                !restored.controller->Status().error.empty())
                throw std::runtime_error("restored successor differs or failed");
            restored.controller->Close(TaskExitReason::Deleted);
            for (int frame = 0; frame < 12; ++frame)
                restored.Tick();
            Check(restored.controller->Drained());
        }
        if (row.contains("numbers"))
            for (const auto& number : row.at("numbers")) {
                const auto token = fixture.scene->ContextToken(number.at("context").get<std::string>());
                const auto values = fixture.scene->TaskContextNumbers(token);
                const auto found = values.find(number.at("name").get<std::string>());
                if (found == values.end() || !std::holds_alternative<std::int64_t>(found->second) ||
                    std::get<std::int64_t>(found->second) != number.at("value").get<std::int64_t>())
                    throw std::runtime_error("control script number step " + std::to_string(step));
            }
        if (row.contains("absent"))
            for (const auto& name : row.at("absent"))
                if (fixture.scene->ContextToken(name.get<std::string>()))
                    throw std::runtime_error("control script context was not removed");
        if (row.contains("present"))
            for (const auto& name : row.at("present"))
                if (!fixture.scene->ContextToken(name.get<std::string>()))
                    throw std::runtime_error("control script context was not retained");
        if (row.contains("effects_created") &&
            fixture.effect_inputs.size() - effects_before != row.at("effects_created").get<std::size_t>())
            throw std::runtime_error("control script unexpected effects at step " + std::to_string(step));
        if (row.contains("restore_available")) {
            Fixture restored(script.at("catalog"), true, 8, std::make_shared<TaskContextRegistry>());
            restored.scene->Restore(fixture.scene->Capture());
            for (int frame = 0; frame < 12; ++frame)
                restored.Tick(.1);
            Check(restored.controller->Status().error.empty());
            for (auto it = row.at("restore_available").begin(); it != row.at("restore_available").end(); ++it)
                if ((restored.controller->Query({{id, it.key()}}).query.availability == TaskAvailability::Available) !=
                    it.value().get<bool>())
                    throw std::runtime_error("restored input availability differs: " + it.key());
            if (row.contains("numbers"))
                for (const auto& number : row.at("numbers")) {
                    const auto token = restored.scene->ContextToken(number.at("context").get<std::string>());
                    Check(std::get<std::int64_t>(restored.scene->TaskContextNumbers(token).at(
                              number.at("name").get<std::string>())) == number.at("value").get<std::int64_t>());
                }
        }
    }
    fixture.controller->Close(TaskExitReason::Deleted);
    for (int frame = 0; frame < 12; ++frame)
        fixture.Tick();
    Check(fixture.scene->ActiveContexts().empty() && Fixture::effect_roots.empty());
}
namespace {
Json CallbackDocument() {
    return Json::parse(R"({
      "format":"hksr.task-catalog","id":"test","domain":"battle",
      "visual":{"id":"scene","source":"fixture","scope":"owned",
        "effects":{"glow":{"path":"glow.prefab","target":"Target"}},
        "contexts":{"mode":{"enter":[],"exit":[],"restore":[]}}},
      "contexts":{"mode":{"values":{"charged":true},"phase":"enhanced",
        "callbacks":{"enter":"created","exit":"destroyed","signals":{"refresh":"refresh"}}}},
      "sequences":{
        "created":{"entry":{"op":"serial","children":[
          {"op":"visual","request":"effect","instruction":{"op":"create","effects":["glow"],"source":"OnCreate"}},
          {"op":"wait_seconds","seconds":0.25},
          {"op":"branch","condition":{"op":"choice","name":"mode","is":"charged"},
           "yes":{"op":"visual","request":"value","instruction":{"op":"assign","variable":"entered","value":1,"source":"OnCreate"}},
           "no":{"op":"visual","request":"value","instruction":{"op":"assign","variable":"entered","value":2,"source":"OnCreate"}}}]}},
        "refresh":{"entry":{"op":"serial","children":[
          {"op":"wait_seconds","seconds":0.1},
          {"op":"visual","request":"face","instruction":{"op":"face_target","target":"Target","source":"OnChange"}}]}},
        "destroyed":{"entry":{"op":"serial","children":[
          {"op":"wait_seconds","seconds":0.2},
          {"op":"visual","request":"remove","instruction":{"op":"remove","effects":["glow"],"optional":["glow"],"source":"OnDestroy"}}]}},
        "enter":{"entry":{"op":"serial","children":[
          {"op":"context_enter","request":"context","name":"mode"},
          {"op":"wait_request","request":"context","signal":"performance"}]}},
        "leave":{"entry":{"op":"serial","children":[
          {"op":"context_exit","request":"context","name":"mode"},
          {"op":"wait_request","request":"context","signal":"performance"}]}},
        "notify":{"entry":{"op":"context_signal","request":"notify","name":"mode","event":"refresh"}},
        "idle":{"entry":{"op":"wait_frame"}}
      },
      "tasks":{"enter":{"entry":"enter"},"leave":{"entry":"leave"},"notify":{"entry":"notify"},
        "idle":{"entry":"idle"},"charged":{"entry":"idle","requires":[{"op":"value","name":"charged","is":true}]}}
    })");
}
void CallbackLifetimes() {
    Fixture f(CallbackDocument(), true);
    f.defaults.targets["Target"] = 99;
    f.defaults.choices["mode"] = "charged";
    f.defaults.position_targets["Point"] = {{1, 2, 3}, {0, 0, 0, 1}};
    f.defaults.target_collections["Neighbours"] = {"Target", "Point"};
    f.Select("enter");
    f.Tick();
    Check(f.effect_receipts.size() == 1 && !f.effect_receipts.front().released);
    Check(f.controller->Query({{"test", "charged"}}).query.availability != TaskAvailability::Available);
    f.defaults.targets["Target"] = 5;
    f.defaults.choices["mode"] = "ordinary";
    f.defaults.position_targets["Point"].position.x = 99;
    f.defaults.target_collections["Neighbours"] = {};
    f.Select("idle"); // caller replacement must not cancel the admitted context
    for (int i = 0; i < 5; ++i)
        f.Tick(.1);
    Check(f.controller->Query({{"test", "charged"}}).query.availability == TaskAvailability::Available);
    const auto first = f.scene->ContextToken("mode");
    Check(first && !f.effect_receipts.front().released);
    const auto branch = f.scene->BranchState();
    Check(std::any_of(branch.values.begin(), branch.values.end(), [](const auto& value) {
        return value.scope == "mode" && value.name == "entered" && std::get<float>(value.value) == 1;
    }));
    f.Select("notify");
    for (int i = 0; i < 4; ++i)
        f.Tick(.1);
    Check(f.faces.size() == 1 && f.face_inputs.back().targets.at("Target") == 99 &&
          f.face_inputs.back().position_targets.at("Point").position.x == 1);
    Check(f.face_inputs.back().target_collections.at("Neighbours") == std::vector<std::string>({"Target", "Point"}));
    f.Select("leave");
    f.Tick();
    Check(f.controller->Query({{"test", "charged"}}).query.availability != TaskAvailability::Available);
    Check(!f.effect_receipts.front().released); // exit wait still owns its resources
    f.Select("enter");
    for (int i = 0; i < 10; ++i)
        f.Tick(.1);
    if (f.effect_receipts.size() != 2 || !f.effect_receipts.front().released || f.effect_receipts.back().released) {
        std::string detail = "callback recreation: effects=" + std::to_string(f.effect_receipts.size()) +
                             " error=" + f.controller->Status().error;
        for (const auto& issue : f.scene->Status().issues)
            detail += " scene=" + issue;
        for (const auto& request : f.controller->Requests())
            detail += " request=" + request.name + ":" + request.state.error;
        throw std::runtime_error(detail);
    }
    Check(f.scene->ContextToken("mode") > first && f.controller->Status().error.empty());
    // A callback removed before its wait ends must never fire on the new owner.
    const auto faces = f.faces.size();
    f.Select("notify");
    f.Tick();
    f.Select("leave");
    f.Tick();
    f.Select("enter");
    for (int i = 0; i < 10; ++i)
        f.Tick(.1);
    Check(f.faces.size() == faces && f.controller->Status().error.empty());
    f.controller->Close(TaskExitReason::Deleted);
    for (int i = 0; i < 5; ++i)
        f.Tick(.1);
    Check(f.controller->Drained());
    for (const auto& effect : f.effect_receipts)
        Check(effect.released);
}
void CallbackPlaybackHandoff() {
    auto document = CallbackDocument();
    document["contexts"]["mode"]["numbers"] = {{"charge", {{"initial", 4}}}};
    document["sequences"]["refresh"]["entry"] = {
        {"op", "serial"},
        {"children",
         Json::array({{{"op", "wait_seconds"}, {"seconds", .5}},
                      {{"op", "animation"}, {"request", "late"}, {"participant", "body"}, {"state", "LateBody"}}})}};
    Fixture f(document, true);
    f.defaults.targets["Target"] = 99;
    f.defaults.choices["mode"] = "charged";
    auto drain = [&] {
        for (int i = 0; i < 8; ++i)
            f.controller->Service(.1, ++f.frame, false);
    };
    // Already-admitted initialization is completed, not discarded with its caller.
    f.Select("enter");
    f.Tick();
    f.controller->ReleasePlayback();
    drain();
    const auto token = f.scene->ContextToken("mode");
    Check(token && f.controller->Drained() && !f.controller->Selection());
    Check(std::get<std::int64_t>(f.scene->TaskContextNumbers(token).at("charge")) == 4);
    Check(!f.effect_receipts.front().released);
    f.Select("notify");
    f.Tick();
    f.controller->ReleasePlayback();
    drain();
    Check(f.plays.empty() && f.controller->Drained() && f.scene->ContextToken("mode") == token);
    Check(!f.effect_receipts.front().released); // persistent visual resource is not an old callback
    // A later explicit signal remains usable; it is not an old delayed callback.
    f.Select("notify");
    for (int i = 0; i < 8; ++i)
        f.Tick(.1);
    Check(f.plays.size() == 1 && f.plays[0].first.state == "LateBody");
    f.Select("leave");
    f.Tick();
    f.controller->ReleasePlayback();
    drain();
    Check(!f.scene->ContextToken("mode") && f.effect_receipts.front().released && f.controller->Drained());
}
void PersistentVisualReaction() {
    Fixture f(CallbackDocument(), true);
    f.controller->RestoreManualReaction(TaskVisualReaction{"fixture", 115, DeathOccurrence::Fly});
    Check(f.controller->ReactionState().visual_flag == 115 &&
          f.controller->ReactionState().visual_source == "fixture" &&
          f.controller->ReactionState().visual_occurrence == DeathOccurrence::Fly);
    f.Select("idle");
    for (int i = 0; i < 4; ++i)
        f.Tick();
    f.controller->ReleasePlayback();
    for (int i = 0; i < 4; ++i)
        f.Tick();
    Check(f.controller->ReactionState().visual_flag == 115);
    Check(std::any_of(
        f.controller->Context().Owners().begin(), f.controller->Context().Owners().end(), [](const auto& context) {
            return context.visual_reaction && context.visual_reaction->source == "fixture" &&
                   context.visual_reaction->flag == 115 && context.visual_reaction->occurrence == DeathOccurrence::Fly;
        }));
    Reject([&] { f.controller->RestoreManualReaction(TaskVisualReaction{"other-character", 133}); });
    Check(f.controller->ReactionState().visual_flag == 115);
    f.controller->RestoreManualReaction(TaskVisualReaction{"fixture", 133});
    Check(f.controller->ReactionState().visual_flag == 133);
    f.controller->RestoreManualReaction({});
    Check(!f.controller->ReactionState().visual_flag);
    f.controller->RestoreManualReaction(TaskVisualReaction{"fixture", 115});
    f.controller->Reset(TaskExitReason::WorldLeft);
    for (int i = 0; i < 4; ++i)
        f.Tick();
    Check(!f.controller->ReactionState().visual_flag);
    f.controller->RestoreManualReaction(TaskVisualReaction{"fixture", 28});
    Check(f.controller->ReactionState().visual_flag == 28); // reset cannot retain a stale token
    f.controller->Close(TaskExitReason::Deleted);
    for (int i = 0; i < 4; ++i)
        f.Tick();
    Check(!f.controller->ReactionState().visual_flag && f.controller->Drained());
}
void NormalBodyClearsOnlyManualReaction() {
    auto document = CallbackDocument();
    document["sequences"]["normal"] = {
        {"entry", {{"op", "animation"}, {"request", "attack"}, {"participant", "body"}, {"state", "Attack"}}}};
    document["sequences"]["return"] = {{"entry",
                                        {{"op", "animation"},
                                         {"request", "return"},
                                         {"participant", "body"},
                                         {"state", "StandBy"},
                                         {"current_idle", true}}}};
    document["sequences"]["face"] = {
        {"entry", {{"op", "animation"}, {"request", "face"}, {"participant", "body"}, {"state", "Emo"}, {"layer", 3}}}};
    for (const auto* name : {"normal", "return", "face"})
        document["tasks"][name] = {{"entry", name}};
    Fixture f(document, true);
    f.Select("enter");
    for (int i = 0; i < 5; ++i)
        f.Tick(.1);
    const auto mode = f.scene->ContextToken("mode");
    Check(mode && !f.effect_receipts.empty() && !f.effect_receipts.front().released);
    f.controller->RestoreManualReaction(TaskVisualReaction{"fixture", 115});
    f.Select("face");
    for (int i = 0; i < 3; ++i)
        f.Tick();
    Check(f.controller->ReactionState().visual_flag == 115);
    f.Select("return");
    for (int i = 0; i < 3; ++i)
        f.Tick();
    Check(f.controller->ReactionState().visual_flag == 115);
    f.accept = false;
    f.Select("normal");
    for (int i = 0; i < 3; ++i)
        f.Tick();
    Check(f.controller->ReactionState().visual_flag == 115); // a queued body is not yet accepted
    Check(f.plays.back().first.state == "Attack");
    f.plays.back().second->accepted = true;
    f.plays.back().second->accepted_effect = NormalBodyAccepted{};
    f.Tick();
    Check(!f.controller->ReactionState().visual_flag);
    Check(f.scene->ContextToken("mode") == mode && f.controller->Context().Phase() == "enhanced");
    Check(!f.effect_receipts.front().released && !f.effect_receipts.front().remove_requested);
    Check(std::get<bool>(f.controller->Context().Values().at("charged").value));
}
void ManualReactionOwnership() {
    auto document = CallbackDocument();
    document["sequences"]["reaction"] = {{"entry", {{"op", "reaction"}, {"request", "pose"},
                                                    {"config", "fixture"}, {"flag", 115}}}};
    document["sequences"]["ready"] = {{"entry", {{"op", "ready_pose"}, {"request", "pose"},
                                                 {"config", "fixture"}, {"skill", 0}}}};
    for (const auto* name : {"reaction", "ready"})
        document["tasks"][name] = {{"entry", name}};
    Fixture f(document, true);
    f.controller->ApplyPlaybackEffect(ReactionEntered{"fixture", 115, DeathOccurrence::Fly});
    Check(f.controller->ManualVisualReaction()->flag == 115);
    const auto persistent = f.controller->Context().Enter("persistent", "test");
    f.controller->Context().SetVisualReaction(persistent, TaskVisualReaction{"fixture", 133});
    Check(f.controller->ReactionState().visual_flag == 133);
    f.controller->ApplyPlaybackEffect(ReactionRecovered{"fixture", 133});
    Check(f.controller->ManualVisualReaction()->flag == 115); // another owner's policy is not manual state
    f.controller->ApplyPlaybackEffect(ReactionRecovered{"fixture", 115});
    Check(!f.controller->ManualVisualReaction() && f.controller->ReactionState().visual_flag == 133);
    f.controller->ApplyPlaybackEffect(ReactionEntered{"fixture", 28});
    f.controller->ApplyPlaybackEffect(NormalBodyAccepted{});
    Check(!f.controller->ManualVisualReaction() && f.controller->ReactionState().visual_flag == 133);
    Check(f.controller->Context().Exit(persistent) && !f.controller->ReactionState().visual_flag);
    f.Select("reaction");
    for (int frame = 0; frame < 4; ++frame)
        f.Tick();
    Check(f.controller->ManualVisualReaction()->flag == 115 && f.poses.size() == 1);
    f.Select("ready");
    for (int frame = 0; frame < 4; ++frame)
        f.Tick();
    Check(!f.controller->ManualVisualReaction() && f.poses.size() == 2);
}
void ExplicitContextRestore() {
    auto document = CallbackDocument();
    document["contexts"]["mode"]["callbacks"]["restore"] = "rehydrate";
    document["sequences"]["rehydrate"]["entry"] = {
        {"op", "serial"},
        {"children", Json::array({{{"op", "animation_parameter"},
                                   {"request", "mode"},
                                   {"participant", "body"},
                                   {"parameter", {{"name", "Mode"}, {"type", "int"}, {"value", 7}}}},
                                  {{"op", "wait_request"}, {"request", "mode"}, {"signal", "accepted"}}})}};
    document["sequences"]["body"]["entry"] = {
        {"op", "animation"}, {"request", "body"}, {"participant", "body"}, {"state", "StandBy"}};
    document["tasks"]["body"] = {{"entry", "body"}};
    Fixture original(document, true);
    original.defaults.targets["Target"] = 99;
    original.defaults.choices["mode"] = "charged";
    original.Select("enter");
    for (int i = 0; i < 10; ++i)
        original.Tick(.1);
    Check(original.scene->ReadyContexts().size() == 1 && original.effect_receipts.size() == 1 &&
          original.parameters.empty());
    const auto saved = original.scene->Capture();
    Fixture restored(document, true);
    restored.parameter_ready = false;
    restored.scene->Restore(saved);
    restored.Select("body");
    for (int i = 0; i < 8; ++i)
        restored.Tick();
    Check(restored.plays.empty() && restored.parameters.empty() && restored.effect_receipts.empty());
    Check(restored.controller->RestoreInputsReady({{"test", "body"}}).state == VisualStepState::Pending);
    restored.controller->ReleasePlayback();
    restored.Select("body");
    for (int i = 0; i < 3; ++i)
        restored.Tick();
    Check(restored.plays.empty()); // body replacement cannot cancel the explicit restore setter
    restored.parameter_ready = true;
    for (int i = 0; i < 10; ++i)
        restored.Tick();
    Check(restored.parameters.size() == 1 && restored.plays.size() == 1 && restored.effect_receipts.empty());
    Check(restored.submissions == std::vector<std::string>({"parameter:Mode", "animation:StandBy"}));
    Check(std::get<int>(restored.parameters.front().value) == 7 && restored.controller->Status().error.empty());
    Check(restored.controller->RestoreInputsReady({{"test", "body"}}).state == VisualStepState::Done);
    for (int i = 0; i < 5; ++i)
        restored.Tick();
    Check(restored.parameters.size() == 1);
    auto invalid = document;
    invalid["contexts"]["mode"]["callbacks"]["restore"] = "absent";
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });
    invalid = document;
    invalid["contexts"]["mode"]["state_only"] = true;
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });
    // A regular persistent signal is not a restore dependency and must not
    // prevent an unrelated body task from starting.
    auto ordinary = document;
    ordinary["contexts"]["mode"]["callbacks"].erase("restore");
    ordinary["sequences"]["refresh"]["entry"] = {
        {"op", "repeat"}, {"until_finish", true}, {"child", {{"op", "wait_frame"}}}};
    Fixture running(ordinary);
    running.scene->Restore(saved);
    for (int i = 0; i < 5; ++i)
        running.Tick();
    running.Select("notify");
    for (int i = 0; i < 3; ++i)
        running.Tick();
    Check(running.controller->RestoreInputsReady({{"test", "body"}}).state == VisualStepState::Done);
    running.Select("body");
    for (int i = 0; i < 4; ++i)
        running.Tick();
    Check(running.plays.size() == 1);
    Fixture failed(document, true);
    failed.parameter_error = "fixture restore refused";
    failed.scene->Restore(saved);
    for (int i = 0; i < 10; ++i)
        failed.Tick();
    if (!failed.plays.empty() || !failed.effect_receipts.empty() ||
        failed.controller->Status().error.find("mode restore rehydrate") == std::string::npos)
        std::cerr << "restore fixture: plays=" << failed.plays.size() << " effects=" << failed.effect_receipts.size()
                  << " error=" << failed.controller->Status().error << '\n';
    Check(failed.plays.empty() && failed.effect_receipts.empty() &&
          failed.controller->Status().error.find("mode restore rehydrate") != std::string::npos);
    const auto failure = failed.controller->RestoreInputsReady({{"test", "body"}});
    Check(failure.state == VisualStepState::Failed &&
          failure.detail.find("fixture restore refused") != std::string::npos);
    Reject([&] { failed.controller->RestoreSelection({{"test", "body"}}); });
    Check(failed.scene->ReadyContexts().empty() && !failed.controller->Context().Phase());
    failed.parameter_error.clear();
    failed.Select("body");
    for (int i = 0; i < 4; ++i)
        failed.Tick();
    Check(failed.plays.size() == 1); // failure withdraws the owner, not all later actor controls
}
void IndependentInputs() {
    auto document = CallbackDocument();
    document["contexts"]["mode"]["numbers"] = {{"charge", {{"initial", 0}}}};
    document["visual"]["restore"] = Json::array();
    document["sequences"]["play"]["entry"] = {
        {"op", "serial"},
        {"children",
         Json::array({{{"op", "animation"}, {"request", "body"}, {"participant", "body"}, {"state", "Attack"}},
                      {{"op", "wait_request"}, {"request", "body"}}})}};
    document["tasks"]["play"] = {{"entry", "play"}};
    for (const auto* name : {"enter", "notify", "leave"})
        document["tasks"][name]["mode"] = "input";
    for (int amount : {4, 8}) {
        const auto name = "set" + std::to_string(amount);
        document["sequences"][name]["entry"] = {{"op", "serial"},
                                                {"children", Json::array({{{"op", "wait_seconds"}, {"seconds", .2}},
                                                                          {{"op", "context_value"},
                                                                           {"request", "write"},
                                                                           {"name", "mode"},
                                                                           {"variable", "charge"},
                                                                           {"operation", "set"},
                                                                           {"value", amount}}})}};
        document["tasks"][name] = {{"entry", name}, {"mode", "input"}};
    }
    for (const bool legacy : {false, true}) {
        Fixture f(document, true, 7, {}, 7, legacy);
        f.defaults.targets["Target"] = 99;
        f.defaults.choices["mode"] = "charged";
        if (legacy) {
            const auto selected = f.scene->Select("legacy");
            f.scene->Service();
            Check(f.scene->Played(selected, 1, 8));
        } else {
            f.Select("play");
            f.Tick();
            Check(f.plays.size() == 1);
        }
        const auto selected = f.controller->Selection();
        auto check_playback = [&] {
            if (legacy)
                Check(f.scene->Status().playing && f.scene->Status().action == "legacy" && f.plays.empty());
            else
                Check(f.plays.size() == 1 && !f.plays.front().second->released);
            const auto current = f.controller->Selection();
            Check(bool(selected) == bool(current) && (!selected || SameTaskSelection(*selected, *current)));
        };
        f.Select("enter");
        f.Tick(.1);
        check_playback();
        for (int i = 0; i < 5; ++i)
            f.Tick(.1);
        const auto token = f.scene->ContextToken("mode");
        Check(token);
        f.Select("set4");
        f.Select("set8");
        Check(f.controller->Status().pending_inputs == 2);
        for (int i = 0; i < 12; ++i) {
            f.Tick(.1);
            check_playback();
        }
        Check(f.controller->Status().pending_inputs == 0 && f.controller->Status().input->task.name == "set8");
        Check(f.controller->Status().input->execution->phase == TaskExecutionPhase::Finished);
        Check(f.controller->Status().input->execution->retained_requests == 0);
        const auto snapshot = f.scene->Capture();
        Check(std::get<std::int64_t>(snapshot.contexts[0].numbers.at("charge")) == 8);
        f.Select("notify");
        f.Tick();
        // Another ordinary selection must not cancel the already accepted input's callback.
        if (legacy)
            f.scene->Select("legacy");
        else
            f.controller->ReleasePlayback();
        for (int i = 0; i < 6; ++i)
            f.controller->Service(.1, ++f.frame, false, true);
        Check(f.faces.size() == 1 && f.scene->ContextToken("mode") == token && !f.effect_receipts.front().released);
        f.Select("set4");
        f.Tick();
        f.controller->Reset(TaskExitReason::ActorLost);
        for (int i = 0; i < 5; ++i)
            f.Tick(.1);
        Check(f.controller->Drained() && f.scene->ActiveContexts().empty());
        Check(f.effect_receipts.front().released && f.controller->Status().pending_inputs == 0);
    }
}
void CallbackEvents() {
    auto document = CallbackDocument();
    document["contexts"]["mode"]["instances"] = {{"name", "defence"}, {"receiver", "Target"}};
    document["contexts"]["mode"]["callbacks"]["enter"] = "idle";
    document["contexts"]["mode"]["callbacks"]["events"] = {{"after_hit_all", "on-hit"},
                                                           {"after_attack", "after-attack"}};
    document["visual"]["effects"]["hit"] = {{"path", "received-hit.prefab"}, {"target", "Target"}};
    document["sequences"]["on-hit"]["entry"] = {
        {"op", "serial"},
        {"children",
         Json::array({{{"op", "wait_seconds"}, {"seconds", 0.1}},
                      {{"op", "visual"},
                       {"request", "hit"},
                       {"instruction", {{"op", "create"}, {"effects", {"hit"}}, {"source", "OnAfterBeingHitAll"}}}},
                      {{"op", "visual"},
                       {"request", "face"},
                       {"instruction", {{"op", "face_target"}, {"target", "Target"}, {"source", "owner-target"}}}}})}};
    document["sequences"]["after-attack"]["entry"] = {
        {"op", "visual"},
        {"request", "end"},
        {"instruction", {{"op", "assign"}, {"variable", "attack_end"}, {"value", 1}, {"source", "OnAfterAttack"}}}};
    document["sequences"]["emit"]["entry"] = {
        {"op", "serial"},
        {"children", Json::array({{{"op", "visual_event"},
                                   {"request", "notify"},
                                   {"event", "after_hit_all"},
                                   {"recipient", "Target"},
                                   {"allow_position", true}},
                                  {{"op", "wait_request"}, {"request", "notify"}, {"signal", "performance"}}})}};
    document["tasks"]["emit"] = {{"entry", "emit"}};
    auto registry = std::make_shared<TaskContextRegistry>();
    Fixture receiver(document, true, 99, registry, 99), a(document, true, 7, registry, 7),
        b(document, true, 8, registry, 8), attacker(document, true, 9, registry, 9);
    auto enter = [&](Fixture& f) {
        f.defaults.targets["Target"] = 99;
        f.Select("enter");
        for (int i = 0; i < 4; ++i)
            f.Tick(.1);
        Check(f.controller->Status().error.empty());
    };
    enter(a);
    enter(b);
    Check(a.scene->ReadyContexts().size() == 1 && b.scene->ReadyContexts().size() == 1);
    // A different event is not a hit-all alias. No recipient is synthesized.
    Check(registry->Dispatch(TaskEventKind::AfterHitMiss, 99, 9)->callbacks.empty());
    Reject([&] { registry->Dispatch(TaskEventKind::AfterHitAll, 400, 9); });
    Reject([&] { registry->Dispatch(TaskEventKind::AfterHitAll, 99, 400); });
    attacker.defaults.targets["Target"] = 99;
    attacker.Select("emit");
    attacker.Tick();
    Check(a.effect_inputs.empty() && b.effect_inputs.empty()); // enqueue only, no recursive execution
    a.defaults.targets["Target"] = 8; // do not replace original receiver with attacker's or new defaults
    attacker.Select("idle");          // accepted receiver callbacks survive the sender's task replacement
    for (int i = 0; i < 7; ++i) {
        a.Tick(.1);
        b.Tick(.1);
        attacker.Tick(.1);
    }
    Check(a.effect_inputs.size() == 1 && b.effect_inputs.size() == 1 && receiver.effect_inputs.empty());
    Check(a.face_inputs.back().targets.at("Target") == 99 && b.face_inputs.back().targets.at("Target") == 99);
    Check(a.scene->ReadyContexts().size() == 1 && b.scene->ReadyContexts().size() == 1); // hit does not retire defence
    const auto before = a.effect_inputs.size();
    auto stale = registry->Dispatch(TaskEventKind::AfterHitAll, 99, 9);
    attacker.controller->Reset(TaskExitReason::ActorLost);
    for (int i = 0; i < 5; ++i) {
        a.Tick(.1);
        b.Tick(.1);
        attacker.Tick(.1);
    }
    Check(a.effect_inputs.size() == before && stale->State().superseded && stale->State().drained);
    // End-of-attack callbacks are a separate explicit producer.
    auto end = registry->Dispatch(TaskEventKind::AfterAttack, 99, 9);
    for (int i = 0; i < 4; ++i) {
        a.Tick();
        b.Tick();
    }
    Check(end->State().performance_complete && a.effect_inputs.size() == before);
    const auto state = a.scene->BranchState();
    Check(std::any_of(state.values.begin(), state.values.end(),
                      [](const auto& value) { return value.name == "attack_end"; }));
    // Domain reset invalidates notifications already queued in a foreign owner.
    stale = registry->Dispatch(TaskEventKind::AfterHitAll, 99, 9);
    receiver.controller->Reset(TaskExitReason::ActorLost);
    for (int i = 0; i < 6; ++i) {
        a.Tick(.1);
        b.Tick(.1);
        receiver.Tick(.1);
    }
    Check(stale->State().superseded && stale->State().drained && a.effect_inputs.size() == before);
    Check(registry->Dispatch(TaskEventKind::AfterHitAll, 99, 9)->callbacks.empty());
    a.defaults.targets["Target"] = 99;
    enter(a);
    auto pending = registry->Dispatch(TaskEventKind::AfterHitAll, 99, 9);
    a.Select("leave");
    a.Tick();
    for (int i = 0; i < 5; ++i)
        a.Tick(.1);
    Check(pending->State().superseded && a.effect_inputs.size() == before);
    // Point previews have no victim even with subscriptions on the caster.
    attacker.defaults.targets.clear();
    attacker.defaults.position_targets["Target"] = {{1, 2, 3}, {0, 0, 0, 1}};
    attacker.Select("emit");
    for (int i = 0; i < 3; ++i)
        attacker.Tick();
    Check(attacker.controller->Status().error.empty());
    // Every public spelling round-trips; native event numbers are not accepted.
    for (const auto kind :
         {TaskEventKind::AfterHit, TaskEventKind::AfterHitAll, TaskEventKind::AfterHitMiss,
          TaskEventKind::AfterAttackedAdv, TaskEventKind::AfterAttack, TaskEventKind::AfterAttacked,
          TaskEventKind::AfterAttackEnd, TaskEventKind::AfterAttackedEnd, TaskEventKind::AfterDamageChunk,
          TaskEventKind::AfterBeingDamageChunk, TaskEventKind::BeingLimbo, TaskEventKind::BeforeDying})
        Check(ParseTaskEvent(TaskEventName(kind)) == kind);
    Reject([] { ParseTaskEvent("40"); });
    auto invalid = document;
    invalid["contexts"]["mode"]["callbacks"]["events"]["after_hit_all"] = "absent";
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });
    invalid = document;
    invalid["sequences"]["emit"]["entry"]["children"][1]["signal"] = "progress";
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });

    // Capture recipients at task admission, not after a wait when a new domain
    // or replacement might already have reused the same stable ID.
    auto delayed = document;
    delayed["sequences"]["emit"]["entry"]["children"].insert(delayed["sequences"]["emit"]["entry"]["children"].begin(),
                                                             Json{{"op", "wait_seconds"}, {"seconds", 0.5}});
    Fixture late(delayed, true, 30, registry, 30);
    late.defaults.targets["Target"] = 99;
    late.Select("emit");
    late.Tick();
    receiver.controller->Reset(TaskExitReason::ActorLost);
    for (int i = 0; i < 8; ++i)
        late.Tick(.1);
    Check(late.controller->Status().execution && !late.controller->Status().execution->issues.empty());

    // A restored active owner resumes subscriptions without replaying enter.
    auto standalone = document;
    standalone["contexts"]["mode"].erase("instances");
    auto restore_registry = std::make_shared<TaskContextRegistry>();
    Fixture initial(standalone, true, 50, restore_registry, 50);
    initial.defaults.targets["Target"] = 50;
    initial.Select("enter");
    for (int i = 0; i < 4; ++i)
        initial.Tick(.1);
    const auto snapshot = initial.scene->Capture();
    Fixture restored(standalone, true, 51, restore_registry, 50);
    restored.scene->Restore(snapshot);
    for (int i = 0; i < 4; ++i)
        restored.Tick();
    auto restored_event = restore_registry->Dispatch(TaskEventKind::AfterHitAll, 50, 50);
    for (int i = 0; i < 5; ++i)
        restored.Tick(.1);
    Check(restored_event->callbacks.size() == 1 && restored_event->State().performance_complete &&
          restored.effect_inputs.size() == 1);
    Check(initial.effect_inputs.empty());

    // Parse and run history boundaries through the production registry port.
    // Two aliases resolve to one actor; no hit/animation is needed to register.
    auto attack_document = standalone;
    attack_document["contexts"]["mode"]["callbacks"]["events"] = {{"after_attacked", "on-hit"}};
    attack_document["sequences"]["attack"]["entry"] = {
        {"op", "serial"},
        {"children", Json::array({{{"op", "attack_record"}, {"request", "record"}, {"target", "Target"}},
                                  {{"op", "attack_record"}, {"request", "alias"}, {"target", "SameTarget"}},
                                  {{"op", "wait_seconds"}, {"seconds", .2}},
                                  {{"op", "attack_finish"}, {"request", "finish"}},
                                  {{"op", "wait_request"}, {"request", "finish"}, {"signal", "accepted"}},
                                  {{"op", "attack_complete"}, {"request", "post"}}})}};
    attack_document["tasks"]["attack"] = {{"entry", "attack"}};
    auto attack_registry = std::make_shared<TaskContextRegistry>();
    Fixture defender(attack_document, true, 70, attack_registry, 70),
        source(attack_document, true, 71, attack_registry, 71);
    defender.defaults.targets["Target"] = 70;
    defender.Select("enter");
    for (int i = 0; i < 4; ++i)
        defender.Tick(.1);
    source.defaults.targets = {{"Target", 70}, {"SameTarget", 70}};
    source.Select("attack");
    for (int i = 0; i < 10; ++i) {
        source.Tick(.1);
        defender.Tick(.1);
    }
    Check(source.controller->Status().error.empty() && defender.effect_inputs.size() == 1);
    source.Select("attack");
    source.Tick();
    defender.controller->Reset(TaskExitReason::ActorLost);
    for (int i = 0; i < 8; ++i) {
        source.Tick(.1);
        defender.Tick(.1);
    }
    Check(source.controller->Status().execution && !source.controller->Status().execution->issues.empty() &&
          defender.effect_inputs.size() == 1);
    invalid = attack_document;
    invalid["sequences"]["attack"]["entry"]["children"][4]["signal"] = "progress";
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });
}
void CallbackPresence() {
    auto document = CallbackDocument();
    document["contexts"]["mode"].erase("values");
    document["contexts"]["mode"].erase("phase");
    document["tasks"]["charged"]["requires"] = Json::array({{{"op", "context"}, {"name", "mode"}}});
    document["tasks"]["empty"] = {
        {"entry", "idle"}, {"requires", Json::array({{{"op", "context"}, {"name", "mode"}, {"present", false}}})}};
    document["sequences"]["check"]["entry"] = {
        {"op", "branch"},
        {"condition", {{"op", "context"}, {"name", "mode"}}},
        {"yes", {{"op", "animation"}, {"request", "pose"}, {"participant", "body"}, {"state", "Present"}}},
        {"no", {{"op", "animation"}, {"request", "pose"}, {"participant", "body"}, {"state", "Absent"}}}};
    document["tasks"]["check"] = {{"entry", "check"}};
    auto invalid = document;
    invalid["tasks"]["empty"]["requires"][0]["name"] = "typo";
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });
    invalid = document;
    invalid["sequences"]["check"]["entry"]["condition"]["name"] = "typo";
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });
    Fixture f(document, true);
    auto present = [&] {
        return f.controller->Query({{"test", "charged"}}).query.availability == TaskAvailability::Available;
    };
    Check(!present() && f.controller->Query({{"test", "empty"}}).query.availability == TaskAvailability::Available);
    f.Select("enter");
    f.Tick();
    Check(!present()); // entering is not active
    for (int i = 0; i < 5; ++i)
        f.Tick(.1);
    Check(present());
    f.Select("check");
    f.Tick();
    Check(f.plays.back().first.state == "Present");
    f.Select("leave");
    f.Tick();
    Check(!present() && !f.effect_receipts.front().released); // exiting is already absent
    for (int i = 0; i < 5; ++i)
        f.Tick(.1);
    f.Select("check");
    f.Tick();
    Check(f.plays.back().first.state == "Absent");
    f.Select("enter");
    for (int i = 0; i < 6; ++i)
        f.Tick(.1);
    const auto snapshot = f.scene->Capture();
    Fixture restored(document, true);
    restored.scene->Restore(snapshot);
    restored.Tick();
    restored.Tick();
    Check(restored.controller->Query({{"test", "charged"}}).query.availability == TaskAvailability::Available);
    Check(restored.effect_receipts.empty()); // active restore did not replay OnCreate
    // A purely visual context contributes presence to its receiver, with no
    // dummy value or phase. Independent owners withdraw independently.
    document["contexts"]["mode"]["instances"] = {{"name", "mark"}, {"receiver", "Target"}};
    auto registry = std::make_shared<TaskContextRegistry>();
    Fixture receiver(document, true, 9009, registry, 99), source(document, true, 7007, registry, 7),
        other(document, true, 8008, registry, 8);
    source.defaults.targets["Target"] = other.defaults.targets["Target"] = 99;
    source.Select("enter");
    other.Select("enter");
    for (int i = 0; i < 6; ++i) {
        source.Tick(.1);
        other.Tick(.1);
    }
    Check(receiver.controller->Query({{"test", "charged"}}).query.availability == TaskAvailability::Available);
    Check(source.controller->Query({{"test", "charged"}}).query.availability == TaskAvailability::Unavailable);
    source.Select("leave");
    source.Tick();
    Check(receiver.controller->Query({{"test", "charged"}}).query.availability == TaskAvailability::Available);
    other.Select("leave");
    other.Tick();
    Check(receiver.controller->Query({{"test", "charged"}}).query.availability == TaskAvailability::Unavailable);
    const auto foreign = registry->Find(99)->Apply("mode", {}, "another-catalog");
    Check(receiver.controller->Query({{"test", "charged"}}).query.availability == TaskAvailability::Unavailable);
    registry->Find(99)->Remove(registry->Find(99)->Epoch(), foreign);
    document["contexts"]["mode"].erase("instances");
    document["contexts"]["mode"]["callbacks"]["activation"] = "before_callbacks";
    document["contexts"]["mode"]["callbacks"]["reapply"] = "refresh";
    Fixture early(document, true);
    early.Select("enter");
    early.Tick();
    Check(early.controller->Query({{"test", "charged"}}).query.availability == TaskAvailability::Available);
    Check(!early.effect_receipts.empty()); // callback is still waiting, but the source is already active
    early.Select("leave");
    early.Tick();
    Check(early.controller->Query({{"test", "charged"}}).query.availability == TaskAvailability::Unavailable);
    for (int i = 0; i < 5; ++i)
        early.Tick(.1);
    Check(early.effect_receipts.front().released && early.controller->Status().error.empty());
    early.Select("enter");
    for (int i = 0; i < 6; ++i)
        early.Tick(.1);
    const auto token = early.scene->ContextToken("mode");
    early.Select("enter");
    for (int i = 0; i < 6; ++i)
        early.Tick(.1);
    Check(early.scene->ContextToken("mode") == token && early.faces.size() == 1 && early.effect_receipts.size() == 2);
}
void CallbackRestorationAndFailures() {
    auto document = CallbackDocument();
    auto invalid = document;
    invalid["contexts"]["mode"]["callbacks"]["enter"] = "missing";
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });
    invalid = document;
    invalid["sequences"]["notify"]["entry"]["event"] = "unbound";
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });
    Fixture restored(document, true);
    VisualProgramSnapshot snapshot;
    snapshot.program = "scene";
    VisualContextSnapshot mode;
    mode.name = "mode";
    mode.inputs.targets["Target"] = 99;
    mode.inputs.position_targets["Point"] = {{4, 0, 0}, {0, 0, 0, 1}};
    snapshot.contexts.push_back(mode);
    restored.scene->Restore(snapshot);
    restored.Tick();
    restored.Tick();
    Check(restored.effect_receipts.empty()); // restore recipe, not transient OnCreate
    restored.Select("notify");
    for (int i = 0; i < 4; ++i)
        restored.Tick(.1);
    Check(restored.faces.size() == 1 && restored.face_inputs.back().position_targets.at("Point").position.x == 4);
    restored.Select("leave");
    for (int i = 0; i < 5; ++i)
        restored.Tick(.1);
    Check(!restored.scene->ContextToken("mode"));
    // Local provider failure releases only this lifetime and reports the cause.
    Fixture failed(document);
    failed.Select("enter");
    for (int i = 0; i < 5; ++i)
        failed.Tick(.1);
    Check(failed.controller->Status().error.find("unexpected effect") != std::string::npos &&
          !failed.scene->ContextToken("mode"));
    Fixture reentrant(document, true);
    reentrant.on_context_start = [&] { reentrant.controller->Close(TaskExitReason::Deleted); };
    reentrant.Select("enter");
    for (int i = 0; i < 4; ++i)
        reentrant.Tick(.1);
    Check(reentrant.controller->Drained() && reentrant.effect_receipts.empty());
}
void CallbackNumbers() {
    auto document = CallbackDocument();
    auto& context = document["contexts"]["mode"];
    context["numbers"] = {{"charge", {{"initial", 0}, {"min", 0}, {"max", 8}}}};
    document["sequences"]["created"]["entry"] = {{"op", "serial"}, {"children", Json::array()}};
    document["sequences"]["refresh"]["entry"] = {{"op", "context_value"}, {"request", "write"}, {"name", "mode"},
                                                 {"variable", "charge"},  {"operation", "add"}, {"value", 4}};
    auto charged = Json{{"op", "number"}, {"name", "charge"}, {"compare", "ge"}, {"value", 4}};
    document["tasks"]["charged"]["requires"] = Json::array({charged});
    document["sequences"]["destroyed"]["entry"] = {
        {"op", "branch"},
        {"condition", charged},
        {"yes", {{"op", "animation"}, {"request", "pose"}, {"participant", "body"}, {"state", "ChargedExit"}}},
        {"no", {{"op", "animation"}, {"request", "pose"}, {"participant", "body"}, {"state", "EmptyExit"}}}};
    document["sequences"]["increase"]["entry"] = {
        {"op", "serial"},
        {"children",
         Json::array(
             {{{"op", "context_value"},
               {"request", "write"},
               {"name", "mode"},
               {"variable", "charge"},
               {"operation", "add"},
               {"value", 4}},
              {{"op", "wait_request"}, {"request", "write"}, {"signal", "performance"}},
              {{"op", "branch"},
               {"condition", charged},
               {"yes", {{"op", "animation"}, {"request", "pose"}, {"participant", "body"}, {"state", "Enhanced"}}},
               {"no", {{"op", "animation"}, {"request", "pose"}, {"participant", "body"}, {"state", "Ordinary"}}}}})}};
    document["tasks"]["increase"] = {{"entry", "increase"}};
    auto invalid = document;
    invalid["contexts"]["mode"]["numbers"]["charge"]["max"] = -1;
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });
    invalid = document;
    invalid["sequences"]["refresh"]["entry"]["variable"] = "typo";
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });
    invalid = document;
    invalid["sequences"]["refresh"]["entry"]["value"] = 4.0;
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });
    Fixture f(document);
    f.Select("enter");
    f.Tick();
    f.Tick();
    const auto token = f.scene->ContextToken("mode");
    Check(std::get<std::int64_t>(f.scene->TaskContextNumbers(token).at("charge")) == 0);
    Check(f.controller->Query({{"test", "charged"}}).query.availability == TaskAvailability::Unavailable);
    f.Select("increase");
    f.Tick();
    f.Tick();
    Check(f.plays.back().first.state == "Enhanced");
    Check(f.controller->Query({{"test", "charged"}}).query.availability == TaskAvailability::Available);
    f.Select("notify");
    for (int i = 0; i < 3; ++i)
        f.Tick();
    f.Select("notify");
    for (int i = 0; i < 3; ++i)
        f.Tick();
    const auto snapshot = f.scene->Capture();
    Check(std::get<std::int64_t>(snapshot.contexts.at(0).numbers.at("charge")) == 8);
    const auto status = f.scene->BranchState();
    Check(std::any_of(status.values.begin(), status.values.end(), [](const auto& value) {
        return value.scope == "mode" && value.name == "charge" && std::get<std::int64_t>(value.value) == 8;
    }));
    Fixture restored(document);
    restored.scene->Restore(snapshot);
    restored.Tick();
    restored.Tick();
    Check(restored.controller->Query({{"test", "charged"}}).query.availability == TaskAvailability::Available);
    Check(std::get<std::int64_t>(restored.scene->Capture().contexts.at(0).numbers.at("charge")) == 8);
    auto invalid_snapshot = snapshot;
    invalid_snapshot.contexts[0].numbers["charge"] = std::int64_t(9);
    Fixture invalid_restore(document);
    invalid_restore.scene->Restore(invalid_snapshot);
    invalid_restore.Tick();
    invalid_restore.Tick();
    Check(!invalid_restore.controller->Status().error.empty() && invalid_restore.scene->ActiveContexts().empty());
    f.Select("leave");
    f.Tick();
    f.Tick();
    Check(f.controller->Query({{"test", "charged"}}).query.availability != TaskAvailability::Available);
    Check(f.plays.back().first.state == "ChargedExit"); // private exit state survived public withdrawal
    Reject([&] { f.scene->SetTaskContextNumber(token, "charge", std::int64_t(7)); });
    // Mutation submitted in the same task as entry waits for the owned value
    // store, not for a later user operation or a global character lookup.
    auto immediate = document;
    immediate["sequences"]["enter"]["entry"]["children"].insert(
        immediate["sequences"]["enter"]["entry"]["children"].begin() + 1, Json{{"op", "context_value"},
                                                                               {"request", "write"},
                                                                               {"name", "mode"},
                                                                               {"variable", "charge"},
                                                                               {"operation", "add"},
                                                                               {"value", 4}});
    Fixture same_frame(immediate);
    same_frame.Select("enter");
    same_frame.Tick();
    same_frame.Tick();
    Check(same_frame.controller->Query({{"test", "charged"}}).query.availability == TaskAvailability::Available);
    // Two sources publish independently to one receiver. Updating the older
    // source cannot overwrite the newer source's contribution or count.
    document["contexts"]["mode"]["instances"] = {{"name", "mark"}, {"receiver", "Target"}};
    auto registry = std::make_shared<TaskContextRegistry>();
    Fixture receiver(document, false, 99, registry, 99), older(document, false, 7, registry, 7),
        newer(document, false, 8, registry, 8);
    older.defaults.targets["Target"] = newer.defaults.targets["Target"] = 99;
    older.Select("enter");
    older.Tick();
    older.Tick();
    older.Select("notify");
    older.Tick();
    older.Tick();
    newer.Select("enter");
    newer.Tick();
    newer.Tick();
    Check(receiver.controller->Query({{"test", "charged"}}).query.availability == TaskAvailability::Unavailable);
    older.Select("notify");
    older.Tick();
    older.Tick();
    Check(receiver.controller->Query({{"test", "charged"}}).query.availability == TaskAvailability::Unavailable);
    newer.Select("leave");
    newer.Tick();
    newer.Tick();
    Check(newer.plays.back().first.state == "EmptyExit"); // not the receiver's exposed older value
    Check(receiver.controller->Query({{"test", "charged"}}).query.availability == TaskAvailability::Available);
    Check(std::get<std::int64_t>(older.scene->Capture().contexts.at(0).numbers.at("charge")) == 8);
}
void CallbackHitEffects() {
    auto document = CallbackDocument();
    document["sequences"]["created"]["entry"] = {{"op", "wait_frame"}};
    document["contexts"]["mode"]["hit_effects"] = Json::array({{{"stacking", "multiple"},
                                                                {"path", "shield.prefab"},
                                                                {"death_path", "break.prefab"},
                                                                {"priority", 0},
                                                                {"offset", Json::array({0, .85, .6})}}});
    document["contexts"]["mode"]["resilience"] = 0;
    auto invalid = document;
    invalid["contexts"]["mode"]["resilience"] = 5;
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });
    auto resilience = [](Fixture& f) { return f.controller->ReactionState().resilience; };
    auto effects = [](Fixture& f) { return f.controller->ReactionState().effects; };
    Fixture f(document);
    f.Select("enter");
    f.Tick();
    Check(effects(f).empty());
    f.Tick();
    f.Tick();
    Check(effects(f).size() == 1);
    Check(resilience(f) == 0);
    const auto version = effects(f).at(0).version;
    Check(version != 0);
    Check(effects(f).at(0).version == version); // reading does not re-register
    auto plan = SelectHitEffects("base.prefab", "Hit", effects(f));
    Check(plan.effects.size() == 1 && plan.effects[0].path == "shield.prefab" && plan.effects[0].offset->y == .85f);
    auto saved = f.scene->Capture();
    Fixture restored(document);
    restored.scene->Restore(saved);
    restored.Tick();
    restored.Tick();
    Check(effects(restored).size() == 1 && effects(restored)[0].path == "shield.prefab");
    Check(resilience(restored) == 0);
    f.Select("leave");
    f.Tick();
    Check(effects(f).empty()); // withdrawn before asynchronous exit completes
    Check(!resilience(f));
    for (int i = 0; i < 5; ++i)
        f.Tick(.1);
    f.Select("enter");
    for (int i = 0; i < 3; ++i)
        f.Tick();
    Check(effects(f).size() == 1 && effects(f).at(0).version > version);
    f.controller->Close(TaskExitReason::Deleted);
    for (int i = 0; i < 6; ++i)
        f.Tick();
    Check(effects(f).empty());

    auto retained = document;
    retained["contexts"]["mode"]["instances"] = {
        {"name", "shield"}, {"receiver", "Caster"}, {"match", "receiver_caster"}};
    retained["tasks"]["clear"] = {
        {"entry", "leave"}, {"mode", "input"}, {"requires", Json::array({{{"op", "context"}, {"name", "mode"}}})}};
    Fixture owner(retained);
    Check(owner.controller->Query({{"test", "clear"}}).query.availability != TaskAvailability::Available);
    owner.Select("enter");
    for (int i = 0; i < 3; ++i)
        owner.Tick();
    const auto stable_owner = effects(owner).front().version;
    owner.Select("idle");
    for (int i = 0; i < 3; ++i)
        owner.Tick();
    Check(effects(owner).size() == 1 && effects(owner).front().version == stable_owner && resilience(owner) == 0);
    owner.Select("enter");
    for (int i = 0; i < 3; ++i)
        owner.Tick();
    Check(effects(owner).size() == 1 && effects(owner).front().version == stable_owner); // Keep, not stacking
    Check(owner.controller->Query({{"test", "clear"}}).query.availability == TaskAvailability::Available);
    owner.Select("clear");
    for (int i = 0; i < 4; ++i)
        owner.Tick();
    Check(effects(owner).empty() && !resilience(owner));
    Check(owner.controller->Query({{"test", "clear"}}).query.availability != TaskAvailability::Available);

    document["contexts"]["mode"]["instances"] = {{"name", "shield"}, {"receiver", "Target"}};
    auto registry = std::make_shared<TaskContextRegistry>();
    Fixture receiver(document, false, 99, registry, 99), a(document, false, 7, registry, 7),
        b(document, false, 8, registry, 8);
    auto baseline = receiver.controller->Context().Enter("baseline");
    receiver.controller->Context().SetResilience(baseline, 2);
    a.defaults.targets["Target"] = b.defaults.targets["Target"] = 99;
    a.Select("enter");
    for (int i = 0; i < 3; ++i)
        a.Tick();
    Check(effects(a).empty() && effects(receiver).size() == 1); // receiver has not ticked
    Check(!resilience(a) && resilience(receiver) == 0);
    const auto first = effects(receiver).front().version;
    b.Select("enter");
    for (int i = 0; i < 3; ++i)
        b.Tick();
    auto combined = effects(receiver);
    Check(combined.size() == 2 && combined.back().version > first);
    a.Select("leave");
    a.Tick();
    Check(effects(receiver).size() == 1 && effects(receiver).front().version == combined.back().version);
    b.Select("leave");
    b.Tick();
    Check(effects(receiver).empty());
    Check(resilience(receiver) == 2);
    receiver.controller->Context().Exit(baseline);
    Check(!resilience(receiver));
}
void CallbackNumberReferences() {
    auto document = CallbackDocument();
    document["visual"]["contexts"]["pool"] = {
        {"enter", Json::array()}, {"exit", Json::array()}, {"restore", Json::array()}};
    document["contexts"]["pool"] = {{"numbers", {{"energy", {{"initial", 4}}}}}, {"callbacks", Json::object()}};
    document["contexts"]["mode"]["numbers"] = {{"count", {{"initial", 0}}}};
    document["sequences"]["created"]["entry"] = {{"op", "serial"}, {"children", Json::array()}};
    document["sequences"]["pool"]["entry"] = {
        {"op", "serial"},
        {"children", Json::array({{{"op", "context_enter"}, {"request", "pool"}, {"name", "pool"}},
                                  {{"op", "wait_request"}, {"request", "pool"}, {"signal", "performance"}}})}};
    document["tasks"]["pool"] = {{"entry", "pool"}};
    document["sequences"]["transfer"]["entry"] = {
        {"op", "serial"},
        {"children", Json::array({{{"op", "context_enter"}, {"request", "mode"}, {"name", "mode"}},
                                  {{"op", "context_value"},
                                   {"request", "copy"},
                                   {"name", "mode"},
                                   {"variable", "count"},
                                   {"operation", "set"},
                                   {"value_from", {{"context", "pool"}, {"name", "energy"}}}},
                                  {{"op", "context_value"},
                                   {"request", "change"},
                                   {"name", "pool"},
                                   {"variable", "energy"},
                                   {"operation", "set"},
                                   {"value", 6}},
                                  {{"op", "wait_request"}, {"request", "copy"}, {"signal", "performance"}},
                                  {{"op", "wait_request"}, {"request", "mode"}, {"signal", "performance"}}})}};
    document["tasks"]["transfer"] = {{"entry", "transfer"}};
    document["sequences"]["refresh"]["entry"] = {
        {"op", "context_value"}, {"request", "self-read"}, {"name", "mode"},
        {"variable", "count"},   {"operation", "add"},     {"value_from", {{"context", "mode"}, {"name", "count"}}}};
    auto invalid = document;
    invalid["sequences"]["transfer"]["entry"]["children"][1]["value_from"]["name"] = "absent";
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });
    invalid = document;
    invalid["sequences"]["transfer"]["entry"]["children"][1]["value"] = 4;
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });
    Fixture f(document);
    f.Select("pool");
    f.Tick();
    f.Tick();
    f.Select("transfer");
    for (int i = 0; i < 3; ++i)
        f.Tick();
    Check(f.controller->Status().error.empty());
    const auto mode = f.scene->ContextToken("mode"), pool = f.scene->ContextToken("pool");
    Check(std::get<std::int64_t>(f.scene->TaskContextNumbers(mode).at("count")) == 4);
    Check(std::get<std::int64_t>(f.scene->TaskContextNumbers(pool).at("energy")) == 6);
    f.Select("notify");
    f.Tick();
    f.Tick();
    Check(std::get<std::int64_t>(f.scene->TaskContextNumbers(mode).at("count")) == 8);
    Check(std::get<std::int64_t>(f.scene->TaskContextNumbers(pool).at("energy")) == 6);
    const auto snapshot = f.scene->Capture();
    Fixture restored(document);
    restored.scene->Restore(snapshot);
    restored.Tick();
    restored.Tick();
    restored.Select("notify");
    restored.Tick();
    restored.Tick();
    Check(std::get<std::int64_t>(
              restored.scene->TaskContextNumbers(restored.scene->ContextToken("mode")).at("count")) == 16);
    // A new actor never borrows the first actor's pool.
    Fixture separate(document);
    separate.Select("transfer");
    separate.Tick();
    separate.Tick();
    const auto& failure = separate.controller->Status().execution;
    Check(failure && failure->reason == TaskExitReason::Failed &&
          std::any_of(failure->issues.begin(), failure->issues.end(), [](const auto& issue) {
              return issue.find("number source is not initialized: pool") != std::string::npos;
          }));
}
void CallbackInstances() {
    auto document = CallbackDocument();
    document["contexts"]["mode"]["instances"] = {
        {"name", "shared-mark"}, {"receiver", "Target"}, {"match", "receiver"}};
    Fixture f(document, true, 7007);
    f.defaults.targets["Target"] = 99;
    f.defaults.choices["mode"] = "charged";
    f.Select("enter");
    for (int i = 0; i < 6; ++i)
        f.Tick(.1);
    Check(f.scene->ReadyContexts().size() == 1 && f.effect_receipts.size() == 1 && !f.scene->ContextToken("mode"));
    const auto original = f.scene->ReadyContexts().begin()->first;
    const auto original_generation = f.scene->InstanceToken(original);
    // An external receiver must not contribute its phase to the caster.
    Check(f.controller->Query({{"test", "charged"}}).query.availability != TaskAvailability::Available);
    f.Select("enter");
    for (int i = 0; i < 6; ++i)
        f.Tick(.1);
    Check(f.effect_receipts.size() == 1 && f.scene->InstanceToken(original) == original_generation);
    f.defaults.targets["Target"] = 7;
    f.Select("enter");
    for (int i = 0; i < 6; ++i)
        f.Tick(.1);
    Check(f.scene->ReadyContexts().size() == 2 && f.effect_receipts.size() == 2);
    Check(f.controller->Query({{"test", "charged"}}).query.availability == TaskAvailability::Available);
    const auto saved = f.scene->Capture();
    Check(saved.contexts.size() == 2 && saved.contexts[0].binding->receiver == 99 &&
          saved.contexts[0].binding->caster == 7);
    f.Select("leave");
    for (int i = 0; i < 6; ++i)
        f.Tick(.1);
    Check(f.scene->ReadyContexts().size() == 1 && !f.effect_receipts[0].released && f.effect_receipts[1].released);
    Check(f.controller->Query({{"test", "charged"}}).query.availability != TaskAvailability::Available);
    // Restore repeated definitions without replaying OnCreate, and keep their
    // saved IDs distinct from freshly allocated resource generation tokens.
    Fixture restored(document, true);
    restored.scene->Restore(saved);
    for (int i = 0; i < 4; ++i)
        restored.Tick();
    Check(restored.scene->ReadyContexts().size() == 2 && restored.effect_receipts.empty());
    Check(SameVisualSnapshot(saved, restored.scene->Capture()));
    restored.defaults.targets["Target"] = 99;
    restored.Select("notify");
    for (int i = 0; i < 5; ++i)
        restored.Tick(.1);
    Check(restored.faces.size() == 1 && restored.face_inputs.back().targets.at("Target") == 99);
    restored.Select("leave");
    for (int i = 0; i < 6; ++i)
        restored.Tick(.1);
    Check(restored.scene->ReadyContexts().size() == 1);
    restored.defaults.targets["Target"] = 98;
    restored.Select("enter");
    for (int i = 0; i < 6; ++i)
        restored.Tick(.1);
    const auto after = restored.scene->Capture();
    Check(after.contexts.size() == 2 && after.contexts.back().instance > saved.contexts.back().instance);
    // An explicit stale generation cannot remove a restored/replacement owner.
    restored.scene->ReleaseTaskContext({after.contexts.back().name, after.contexts.back().instance},
                                       original_generation);
    Check(restored.scene->ReadyContexts().size() == 2);
    const VisualContextKey reused{"mode", 900};
    const auto old_token =
        restored.scene->OpenTaskContext(reused, {}, VisualContextBinding{"shared-mark", "Default", {}, 7, 7});
    restored.scene->ReleaseTaskContext(reused, old_token);
    const auto new_token =
        restored.scene->OpenTaskContext(reused, {}, VisualContextBinding{"shared-mark", "Default", {}, 7, 7});
    restored.scene->ReleaseTaskContext(reused, old_token);
    Check(new_token != old_token && restored.scene->InstanceToken(reused) == new_token);
    restored.scene->ReleaseTaskContext(reused, new_token);
    Check(f.controller->Status().error.empty() && restored.controller->Status().error.empty());
}
void CallbackReceivers() {
    auto document = CallbackDocument();
    document["contexts"]["mode"]["instances"] = {{"name", "mark"}, {"receiver", "Target"}};
    document["tasks"]["notify"]["requires"] = Json::array({{{"op", "context"}, {"name", "mode"}, {"present", false}}});
    auto registry = std::make_shared<TaskContextRegistry>();
    Fixture receiver(document, true, 9009, registry, 99), source(document, true, 7007, registry, 7);
    source.defaults.targets["Target"] = 99;
    source.Select("enter");
    for (int i = 0; i < 6; ++i)
        source.Tick(.1);
    Check(source.controller->Status().error.empty());
    Check(source.controller->Context().Owners().empty());
    Check(receiver.controller->Query({{"test", "charged"}}).query.availability == TaskAvailability::Available);
    Check(receiver.controller->Query({{"test", "notify"}}).query.availability == TaskAvailability::Unavailable);
    // Independent casters contribute independently; removing the later one
    // restores the earlier owner rather than clearing the receiver wholesale.
    Fixture other(document, true, 8008, registry, 8);
    other.defaults.targets["Target"] = 99;
    other.Select("enter");
    for (int i = 0; i < 6; ++i)
        other.Tick(.1);
    Check(receiver.controller->Context().Owners().size() == 2);
    source.Select("leave");
    source.Tick();
    Check(receiver.controller->Context().Owners().size() == 1);
    for (int i = 0; i < 6; ++i)
        source.Tick(.1);
    Check(receiver.controller->Query({{"test", "charged"}}).query.availability == TaskAvailability::Available);
    // Reset may retain the entity ID. Old resource owners cannot affect a
    // reconstructed controller or remove an identically numbered new token.
    receiver.controller->Reset(TaskExitReason::ActorLost);
    const auto fresh = receiver.controller->Context().Enter("fresh");
    receiver.controller->Context().SetValue(fresh, "new", true);
    Check(other.scene->ReadyContexts().empty() && other.scene->Capture().contexts.empty());
    other.Tick();
    other.Tick();
    Check(other.effect_receipts.back().released && other.scene->ActiveContexts().empty());
    Check(receiver.controller->Context().Owners().size() == 1 && receiver.controller->Context().Values().count("new"));
    other.Select("enter");
    for (int i = 0; i < 6; ++i)
        other.Tick(.1);
    Check(receiver.controller->Context().Owners().size() == 2);
    other.controller->Close(TaskExitReason::Deleted);
    other.Tick();
    Check(receiver.controller->Context().Owners().size() == 1);
    // Registry replacement invalidates the old receiver even if its owning
    // C++ object has not yet been collected.
    Fixture replacement(document, true, 9010, registry, 99);
    Check(receiver.controller->Context().Owners().empty());
    source.Select("enter");
    for (int i = 0; i < 6; ++i)
        source.Tick(.1);
    Check(replacement.controller->Context().Owners().size() == 1 && receiver.controller->Context().Owners().empty());
    // A lost native target also invalidates a quiet context (no event needed).
    ++source.participant_generations[99];
    Check(source.scene->ReadyContexts().empty());
    source.Tick();
    source.Tick();
    Check(replacement.controller->Context().Owners().empty() && source.effect_receipts.back().released);
    Check(source.controller->Status().error.empty() && other.controller->Status().error.empty());
    auto endpoint_document = document;
    endpoint_document["id"] = "foreign";
    auto foreign = std::make_shared<TaskCatalog>(ParseLegacyTaskCatalog(endpoint_document.dump()));
    TaskContextEndpoint endpoint(foreign);
    const auto projected = endpoint.Apply("mode", replacement.catalog->contexts.at("mode"), "test");
    Check(endpoint.Context().Value({"mode", "test"}, "charged") &&
          !endpoint.Context().Value({"mode", "foreign"}, "charged"));
    endpoint.Remove(endpoint.Epoch(), projected);
    Check(endpoint.Context().Owners().empty());
    auto foreign_document = document;
    foreign_document["id"] = "foreign";
    Fixture incompatible(foreign_document, true, 10900, registry, 109);
    source.defaults.targets["Target"] = 109;
    source.Select("enter");
    for (int i = 0; i < 7; ++i)
        source.Tick(.1);
    Check(source.controller->Status().error.empty() && incompatible.controller->Context().Owners().size() == 1 &&
          !source.effect_receipts.back().released);
    Check(incompatible.controller->Context().Value({"mode", "test"}, "charged") &&
          !incompatible.controller->Context().Value({"mode", "foreign"}, "charged"));
    source.Select("leave");
    for (int i = 0; i < 6; ++i)
        source.Tick(.1);
    Check(incompatible.controller->Context().Owners().empty() && source.effect_receipts.back().released);
    source.Select("idle");
    source.Tick();
    source.Tick();
    Check(source.controller->Status().error.empty());
    // Saved participant IDs may exist only in the relationship, not in an
    // editable Target input. Wait for both before binding their generations.
    Fixture restored(document, true, 7007, registry, 70);
    restored.participant_ready = false;
    VisualProgramSnapshot saved;
    saved.program = "scene";
    VisualContextSnapshot mark;
    mark.name = "mode";
    mark.instance = 12;
    mark.binding = VisualContextBinding{"mark", "Default", {}, 99, 70};
    saved.contexts.push_back(mark);
    restored.scene->Restore(saved);
    restored.Tick();
    Check(restored.scene->Restoring() && restored.scene->Status().issues.empty());
    Check(restored.restoring.targets.at("@context.receiver") == 99 &&
          restored.restoring.targets.at("@context.caster") == 70);
    restored.participant_ready = true;
    restored.Tick();
    restored.Tick();
    Check(!restored.scene->Restoring() && restored.scene->ReadyContexts().size() == 1);
    Check(replacement.controller->Context().Owners().size() == 1);
}
void CallbackMultipleAndRemoval() {
    auto document = CallbackDocument();
    document["contexts"]["mode"]["instances"] = {{"name", "mark"}, {"repeat", "new"}};
    auto all = document["sequences"]["leave"];
    all["entry"]["children"][0]["all"] = true;
    document["sequences"]["leave_all"] = all;
    document["tasks"]["leave_all"] = {{"entry", "leave_all"}};
    Fixture f(document, true);
    f.defer_effect_release = true;
    for (int j = 0; j < 3; ++j) {
        f.Select("enter");
        for (int i = 0; i < 6; ++i)
            f.Tick(.1);
    }
    Check(f.scene->ReadyContexts().size() == 3 && f.effect_receipts.size() == 3);
    f.Select("leave");
    for (int i = 0; i < 6; ++i)
        f.Tick(.1);
    Check(f.scene->ReadyContexts().size() == 2 && f.effect_receipts[0].remove_requested &&
          !f.effect_receipts[1].remove_requested);
    Check(f.controller->Query({{"test", "charged"}}).query.availability == TaskAvailability::Available);
    f.Select("leave_all");
    for (int i = 0; i < 6; ++i)
        f.Tick(.1);
    Check(f.scene->ActiveContexts().empty());
    for (const auto& receipt : f.effect_receipts)
        Check(receipt.remove_requested && !receipt.released);
    Check(f.controller->Status().execution && f.controller->Status().execution->phase != TaskExecutionPhase::Finished);
    for (auto& receipt : f.effect_receipts)
        receipt.released = true;
    for (int i = 0; i < 6; ++i)
        f.Tick(.1);
    Check(f.controller->Status().error.empty() &&
          f.controller->Status().execution->phase == TaskExecutionPhase::Finished);

    document["contexts"]["mode"]["instances"] = {
        {"name", "mark"}, {"receiver", "Target"}, {"caster", "Origin"}, {"match", "receiver_caster"}};
    document["sequences"]["leave"]["entry"]["children"][0]["only_caster"] = true;
    document["sequences"]["leave_all"]["entry"]["children"][0]["only_caster"] = false;
    Fixture grouped(document, true);
    grouped.defaults.targets = {{"Target", 99}, {"Origin", 7}};
    grouped.Select("enter");
    for (int i = 0; i < 6; ++i)
        grouped.Tick(.1);
    grouped.defaults.targets["Origin"] = 8;
    grouped.Select("enter");
    for (int i = 0; i < 6; ++i)
        grouped.Tick(.1);
    Check(grouped.scene->ReadyContexts().size() == 2);
    grouped.Select("leave");
    for (int i = 0; i < 6; ++i)
        grouped.Tick(.1);
    Check(grouped.scene->ReadyContexts().size() == 1 && !grouped.effect_receipts[0].released &&
          grouped.effect_receipts[1].released);
    grouped.Select("leave_all");
    for (int i = 0; i < 6; ++i)
        grouped.Tick(.1);
    Check(grouped.scene->ActiveContexts().empty() && grouped.effect_receipts[0].released &&
          grouped.effect_receipts[1].released);
}
void CallbackReplacementAndCollections() {
    auto document = CallbackDocument();
    document["contexts"]["mode"]["instances"] = {{"name", "mark"}, {"repeat", "replace_inputs"}};
    document["contexts"]["mode"]["callbacks"]["reapply"] = "refresh";
    Fixture f(document, true);
    f.defaults.targets["Target"] = 99;
    f.Select("enter");
    for (int i = 0; i < 6; ++i)
        f.Tick(.1);
    Check(!f.scene->ReadyContexts().empty() && f.controller->Status().error.empty());
    const auto key = f.scene->ReadyContexts().begin()->first;
    const auto generation = f.scene->InstanceToken(key);
    f.Select("notify");
    f.Tick(); // old callback is waiting with old captured input
    f.defaults.targets["Target"] = 98;
    f.Select("enter");
    f.Tick();
    for (int i = 0; i < 10; ++i)
        f.Tick(.1);
    Check(f.faces.size() == 2 && f.face_inputs[0].targets.count("Target") && f.face_inputs[1].targets.count("Target"));
    Check(f.face_inputs[0].targets.at("Target") == 99 && f.face_inputs[1].targets.at("Target") == 98);
    Check(f.effect_receipts.size() == 1 && !f.effect_receipts[0].released && f.scene->InstanceToken(key) == generation);
    Check(f.scene->Capture().contexts[0].inputs.targets.count("Target"));
    Check(f.scene->Capture().contexts[0].inputs.targets.at("Target") == 98 && f.controller->Status().error.empty());

    document = CallbackDocument();
    document["contexts"]["mode"]["instances"] = {{"name", "mark"}, {"receiver", "ParamEntity"}};
    auto& loop = document["sequences"]["enter"]["entry"];
    loop = {{"op", "for_targets"}, {"bind", "ParamEntity"}, {"targets", Json::array({"A", "B"})}, {"child", loop}};
    Fixture members(document, true);
    members.defaults.targets = {{"A", 99}, {"B", 98}};
    members.Select("enter");
    for (int i = 0; i < 10; ++i)
        members.Tick(.1);
    const auto saved = members.scene->Capture();
    Check(saved.contexts.size() == 2 && members.controller->Status().error.empty());
    Check(saved.contexts[0].binding->receiver == 99 && saved.contexts[1].binding->receiver == 98);
    Check(saved.contexts[0].inputs.targets.at("ParamEntity") == 99 &&
          saved.contexts[1].inputs.targets.at("ParamEntity") == 98);
    // No fallback from a missing identity to the caster.
    Fixture missing(document, true);
    missing.Select("enter");
    for (int i = 0; i < 5; ++i)
        missing.Tick();
    Check(missing.effect_receipts.empty());
    auto invalid = document;
    invalid["visual"]["contexts"]["mode"]["event_scope"] = "native";
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });
    invalid = document;
    invalid["contexts"]["mode"]["instances"]["match"] = "receiver_caster_ability";
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });
}
void CallbackNativeDrainAndNestedOwners() {
    Fixture f(CallbackDocument(), true);
    f.defer_effect_release = true;
    f.Select("enter");
    for (int i = 0; i < 6; ++i)
        f.Tick(.1);
    const auto first = f.scene->ContextToken("mode");
    f.Select("leave");
    for (int i = 0; i < 5; ++i)
        f.Tick(.1);
    Check(!f.scene->ContextToken("mode") && !f.scene->TaskContextDrained(first));
    Check(f.effect_receipts.front().remove_requested && !f.effect_receipts.front().released);
    f.Select("enter");
    for (int i = 0; i < 6; ++i)
        f.Tick(.1);
    const auto second = f.scene->ContextToken("mode");
    Check(second > first && f.effect_receipts.size() == 2);
    f.effect_receipts.front().released = true;
    for (int i = 0; i < 3; ++i)
        f.Tick();
    Check(f.scene->TaskContextDrained(first) && !f.effect_receipts.back().released &&
          f.scene->ContextToken("mode") == second);
    f.controller->Reset(TaskExitReason::ActorLost);
    f.Tick();
    Check(!f.controller->Drained());
    f.effect_receipts.back().released = true;
    for (int i = 0; i < 4; ++i)
        f.Tick();
    Check(f.controller->Drained());

    auto nested = CallbackDocument();
    nested["visual"]["contexts"]["child"] = {{"enter", Json::array()}, {"exit", Json::array()}};
    nested["contexts"]["child"] = {{"values", {{"child", true}}}, {"callbacks", Json::object()}};
    nested["sequences"]["created"]["entry"] = {
        {"op", "serial"},
        {"children", Json::array({{{"op", "context_enter"}, {"request", "child"}, {"name", "child"}},
                                  {{"op", "wait_request"}, {"request", "child"}, {"signal", "performance"}}})}};
    nested["sequences"]["destroyed"]["entry"] = {
        {"op", "serial"},
        {"children", Json::array({{{"op", "context_exit"}, {"request", "child"}, {"name", "child"}},
                                  {{"op", "wait_request"}, {"request", "child"}, {"signal", "performance"}}})}};
    Fixture owners(nested);
    owners.Select("enter");
    for (int i = 0; i < 5; ++i)
        owners.Tick();
    Check(owners.scene->ReadyContexts().size() == 2 && owners.controller->Status().error.empty());
    owners.Select("leave");
    for (int i = 0; i < 6; ++i)
        owners.Tick();
    Check(owners.scene->ActiveContexts().empty() && owners.controller->Status().error.empty());
}
}
namespace {
Json StateDocument() {
    auto doc = Document();
    doc["contexts"] = {
        {"actor",
         {{"state_only", true}, {"values", {{"selected", ""}, {"performance", "ready"}, {"enabled", false}}}}}};
    doc["visual"]["contexts"] = {{"actor", {{"restore", Json::array()}}}};
    doc["visual"]["restore"] = Json::array();
    doc["initial_contexts"] = Json::array({"actor"});
    doc["sequences"] = {
        {"hold",
         {{"entry",
           {{"op", "serial"},
            {"children",
             Json::array({{{"op", "animation"}, {"request", "body"}, {"participant", "body"}, {"state", "Hold"}},
                          {{"op", "repeat"}, {"until_finish", true}, {"child", {{"op", "wait_frame"}}}}})}}}}},
        {"empty", {{"entry", {{"op", "wait_frame"}}}}}};
    const auto write = [](const Json& values) { return Json::array({{{"context", "actor"}, {"values", values}}}); };
    doc["tasks"] = {
        {"a",
         {{"entry", "hold"},
          {"mode", "replace"},
          {"requires", Json::array({{{"op", "one_of"},
                                     {"context", "actor"},
                                     {"name", "performance"},
                                     {"values", Json::array({"ready", "failed"})}}})},
          {"on",
           {{"accepted", write({{"selected", "A"}, {"performance", "accepted"}})},
            {"started", write({{"performance", "working"}})},
            {"completed", write({{"performance", "ready"}})},
            {"replaced", write({{"performance", "old-replaced"}})},
            {"interrupted", write({{"performance", "old-interrupted"}})},
            {"failed", write({{"performance", "failed"}})}}}}},
        {"b",
         {{"entry", "hold"},
          {"mode", "replace"},
          {"on",
           {{"accepted", write({{"selected", "B"}, {"performance", "accepted"}})},
            {"started", write({{"performance", "working"}})},
            {"interrupted", write({{"performance", "old-interrupted"}})}}}}},
        {"input", {{"entry", "empty"}, {"mode", "input"}, {"on", {{"accepted", write({{"performance", "manual"}})}}}}},
        {"internal", {{"entry", "empty"}, {"trigger", "internal"}}}};
    return doc;
}
void ScriptStateContracts() {
    auto doc = StateDocument();
    {
        Fixture cold(doc, false, 7, {}, 7, false, true);
        cold.controller->PrepareState();
        Check(cold.scene->ContextToken("actor") && cold.plays.empty());
        Check(std::get<std::string>(cold.controller->Context().Value({"actor", "test"}, "performance")->value) ==
              "ready");
        cold.controller->Cancel();
    }
    {
        Fixture cold(doc);
        Check(cold.Select("a").intent && cold.plays.empty());
        Check(std::get<std::string>(cold.controller->Context().Value({"actor", "test"}, "performance")->value) ==
              "accepted");
    }
    {
        Fixture model_leave(doc);
        model_leave.controller->PrepareState();
        const auto token = model_leave.scene->ContextToken("actor");
        model_leave.scene->SetTaskContextValue(token, "performance", std::string("failed"));
        model_leave.scene->Leave();
        Check(model_leave.Select("a").intent);
        model_leave.Tick();
        model_leave.Tick();
        Check(model_leave.controller->Status().error.empty() && model_leave.plays.size() == 1);
        Check(model_leave.scene->ContextToken("actor") == token);
        model_leave.controller->Reset(TaskExitReason::ActorLost);
        model_leave.Tick();
        Check(model_leave.controller->Status().error.empty());
    }
    {
        auto resumable = doc;
        resumable["tasks"]["a"]["requires"].push_back(
            {{"op", "value"}, {"context", "actor"}, {"name", "enabled"}, {"is", false}});
        resumable["tasks"]["a"]["on"]["started"][0]["values"]["enabled"] = true;
        for (const bool began : {false, true}) {
            Fixture source(resumable);
            Check(source.Select("a").intent);
            if (began) {
                source.Tick();
                source.plays[0].second->entered = true;
                source.Tick();
            }
            const auto saved = source.scene->Capture();
            const auto selected = *source.controller->Selection();
            Fixture receiver(resumable);
            receiver.scene->Restore(saved);
            for (int i = 0; i < 3; ++i)
                receiver.Tick();
            Check(receiver.controller->Select(selected).query.availability == TaskAvailability::Unavailable);
            Check(receiver.controller->RestoreSelection(selected).intent);
            receiver.Tick();
            Check(receiver.controller->Status().error.empty() && receiver.plays.size() == 1);
            // A different writer's state is not proof of this saved admission.
            Fixture changed(resumable);
            auto edited = saved;
            edited.contexts[0].values["selected"] = std::string("another-entry");
            changed.scene->Restore(edited);
            for (int i = 0; i < 3; ++i)
                changed.Tick();
            Check(changed.controller->RestoreSelection(selected).query.availability == TaskAvailability::Unavailable &&
                  changed.plays.empty());
        }
    }
    auto invalid = doc;
    invalid["contexts"]["actor"]["callbacks"] = {{"enter", "empty"}};
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });
    invalid = doc;
    invalid["tasks"]["a"]["on"]["accepted"][0]["values"]["performance"] = false;
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });
    invalid = doc;
    invalid["tasks"]["a"]["requires"][0]["values"] = Json::array({"ready", true});
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });
    Fixture f(doc);
    Check(!f.scene->ContextToken("actor"));
    f.Tick();
    const auto read = [&](const char* name) {
        return std::get<std::string>(f.controller->Context().Value({"actor", "test"}, name)->value);
    };
    Check(read("performance") == "ready" && f.plays.empty());
    Check(f.Select("internal").query.availability == TaskAvailability::Unavailable);
    Check(f.Select("a").intent && read("selected") == "A" && read("performance") == "accepted");
    f.Tick();
    Check(f.controller->Status().error.empty() && f.plays.size() == 1);
    f.plays[0].second->entered = true;
    f.Tick();
    Check(read("performance") == "working");
    Check(f.Select("b").intent && read("selected") == "B" && read("performance") == "accepted");
    f.Tick();
    f.Tick();
    Check(f.controller->Status().error.empty() && read("performance") != "old-replaced");
    Check(f.plays.size() == 2);
    f.plays[1].second->entered = true;
    f.Tick();
    Check(read("performance") == "working");
    const auto current = f.controller->Status().execution->token.value;
    Check(f.Select("input").intent);
    for (int i = 0; i < 4; ++i)
        f.Tick();
    Check(read("performance") == "manual" && f.controller->Status().execution->token.value == current);
    f.controller->Cancel();
    for (int i = 0; i < 4; ++i)
        f.Tick();
    Check(read("performance") == "manual");
    // A delayed explicit state write in an old interruption sequence also
    // keeps its original ticket; it cannot reclaim a newer input's variable.
    auto delayed = doc;
    delayed["sequences"]["hold"]["interruption"] = {
        {"op", "serial"},
        {"children", Json::array({{{"op", "wait_seconds"}, {"seconds", .2}},
                                  {{"op", "context_value"},
                                   {"request", "old-tail"},
                                   {"name", "actor"},
                                   {"variable", "performance"},
                                   {"operation", "set"},
                                   {"value", "old-tail"}},
                                  {{"op", "wait_request"}, {"request", "old-tail"}, {"signal", "performance"}}})}};
    Fixture overlap(delayed);
    overlap.Tick();
    overlap.Select("a");
    overlap.Tick();
    overlap.Select("input");
    for (int i = 0; i < 4; ++i)
        overlap.Tick();
    overlap.controller->Cancel();
    for (int i = 0; i < 5; ++i)
        overlap.Tick(.3);
    Check(overlap.controller->Status().error.empty());
    Check(std::get<std::string>(overlap.controller->Context().Value({"actor", "test"}, "performance")->value) ==
          "manual");
    auto callback = doc;
    callback["visual"]["contexts"]["modifier"] = {{"restore", Json::array()}};
    callback["contexts"]["modifier"] = {{"callbacks", {{"signals", {{"changed", "external-write"}}}}}};
    callback["sequences"]["external-write"]["entry"] = {{"op", "context_value"}, {"request", "write"},
                                                        {"name", "actor"},       {"variable", "performance"},
                                                        {"operation", "set"},    {"value", "callback"}};
    callback["sequences"]["external"]["entry"] = {
        {"op", "serial"},
        {"children",
         Json::array({{{"op", "context_enter"}, {"request", "owner"}, {"name", "modifier"}},
                      {{"op", "wait_request"}, {"request", "owner"}, {"signal", "performance"}},
                      {{"op", "context_signal"}, {"request", "signal"}, {"name", "modifier"}, {"event", "changed"}},
                      {{"op", "wait_request"}, {"request", "signal"}, {"signal", "performance"}}})}};
    callback["tasks"]["external"] = {{"entry", "external"}, {"mode", "input"}};
    Fixture other_writer(callback);
    other_writer.Tick();
    other_writer.Select("a");
    other_writer.Tick();
    other_writer.Select("external");
    for (int i = 0; i < 8; ++i)
        other_writer.Tick();
    Check(other_writer.controller->Status().error.empty());
    Check(std::get<std::string>(other_writer.controller->Context().Value({"actor", "test"}, "performance")->value) ==
          "callback");
    other_writer.controller->Cancel();
    for (int i = 0; i < 4; ++i)
        other_writer.Tick();
    Check(std::get<std::string>(other_writer.controller->Context().Value({"actor", "test"}, "performance")->value) ==
          "callback");
    Fixture failed(doc);
    failed.Tick();
    failed.Select("a");
    failed.Tick();
    failed.plays.front().second->error = "native animation failed";
    for (int i = 0; i < 4; ++i)
        failed.Tick();
    Check(std::get<std::string>(failed.controller->Context().Value({"actor", "test"}, "performance")->value) ==
          "failed");
    auto bound = doc;
    bound["tasks"]["a"]["requires"].push_back({{"op", "target"}, {"name", "Enemy"}});
    Fixture lost_target(bound);
    lost_target.defaults.targets["Enemy"] = 99;
    lost_target.Tick();
    Check(lost_target.Select("a").intent);
    lost_target.target_ready = false;
    lost_target.Tick();
    Check(lost_target.plays.empty() && lost_target.controller->Status().blocked &&
          lost_target.controller->Status().blocked->availability == TaskAvailability::Unavailable);
    // Explicit hooks are script data; the internal next edge is not a public
    // entry and does not require a second battle acceptance relation.
    auto chained = doc;
    chained["tasks"]["a"]["next"] = {{"task", "internal"}};
    Fixture continuation(chained);
    continuation.Tick();
    continuation.Select("a");
    continuation.Tick();
    continuation.controller->RequestFinish();
    for (int i = 0; i < 7; ++i)
        continuation.Tick();
    Check(continuation.controller->Selection()->task.name == "internal");
    const auto completed = continuation.controller->Context().Value({"actor", "test"}, "performance");
    Check(completed && std::get<std::string>(completed->value) == "ready");
    SceneLayout checkpoint;
    checkpoint.scene = "internal-checkpoint";
    SavedCharacter checkpoint_actor;
    checkpoint_actor.key = 1;
    checkpoint_actor.source = BrowseNpc{1110};
    checkpoint_actor.placement = {{0, 0, 0}, {0, 0, 0, 1}};
    checkpoint_actor.restore.visual_state = continuation.scene->Capture();
    checkpoint_actor.task_catalog = "test";
    checkpoint_actor.restore.task_selection = continuation.controller->Selection();
    checkpoint.characters.push_back(checkpoint_actor);
    const auto restored_checkpoint = DecodeSceneLayout(EncodeSceneLayout(checkpoint));
    Fixture checkpoint_receiver(chained);
    checkpoint_receiver.scene->Restore(*restored_checkpoint.characters[0].restore.visual_state);
    for (int i = 0; i < 4; ++i)
        checkpoint_receiver.Tick();
    const auto selection = *restored_checkpoint.characters[0].restore.task_selection;
    Check(checkpoint_receiver.controller->Select(selection).query.availability == TaskAvailability::Unavailable);
    Check(checkpoint_receiver.controller->RestoreSelection(selection).intent);
    for (int i = 0; i < 4; ++i)
        checkpoint_receiver.Tick();
    Check(checkpoint_receiver.controller->Status().error.empty() &&
          checkpoint_receiver.controller->Selection()->task.name == "internal");
    Check(checkpoint_receiver.controller->Query(selection).query.availability == TaskAvailability::Unavailable);
    // No visual provider may be consulted to create, preserve, restore or
    // retire these plain actor values. Restore occurs after controller creation.
    Fixture pure(doc, false, 7, {}, 7, false, true);
    pure.Tick();
    Check(pure.controller->Status().error.empty());
    const auto token = pure.scene->ContextToken("actor");
    Check(token);
    pure.scene->SetTaskContextValue(token, "performance", std::string("saved"));
    pure.scene->SetTaskContextValue(token, "enabled", true);
    pure.Tick();
    SceneLayout layout;
    layout.scene = "state-contract";
    SavedCharacter row;
    row.key = 1;
    row.source = BrowseNpc{1110};
    row.placement = {{0, 0, 0}, {0, 0, 0, 1}};
    row.restore.visual_state = pure.scene->Capture();
    layout.characters.push_back(row);
    const auto saved = DecodeSceneLayout(EncodeSceneLayout(layout));
    Fixture restored(doc, false, 7, {}, 7, false, true);
    restored.scene->Restore(*saved.characters[0].restore.visual_state);
    for (int i = 0; i < 4; ++i)
        restored.Tick();
    Check(restored.controller->Status().error.empty());
    const auto live = restored.controller->Context().Value({"actor", "test"}, "performance");
    Check(live && std::get<std::string>(live->value) == "saved");
    Check(std::get<bool>(restored.controller->Context().Value({"actor", "test"}, "enabled")->value));
    restored.scene->ReleaseTaskContext(VisualContextKey{"actor"}, restored.scene->ContextToken("actor"));
    restored.Tick();
    Check(!restored.scene->ContextToken("actor") && restored.controller->Status().error.empty());
}
void ScriptConsumerContracts() {
    auto doc = CallbackDocument();
    doc["contexts"]["mode"]["values"] = {{"enabled", false}, {"label", "initial"}};
    for (const auto* name : {"created", "destroyed"})
        doc["sequences"][name]["entry"] = {{"op", "serial"}, {"children", Json::array()}};
    const auto marker = [](const char* request, const char* point, bool visible) {
        return Json{{"op", "visual"},
                    {"request", request},
                    {"instruction",
                     {{"op", "attachment_visibility"},
                      {"source", "script/branch"},
                      {"points", Json::array({point})},
                      {"visible", visible}}}};
    };
    const Json live_flag = {{"op", "value"}, {"context", "mode"}, {"name", "enabled"}, {"is", true}};
    const Json live_text = {{"op", "value"}, {"context", "mode"}, {"name", "label"}, {"is", "updated"}};
    doc["sequences"]["watch"]["entry"] = {
        {"op", "serial"},
        {"children",
         Json::array({{{"op", "context_enter"}, {"request", "owner"}, {"name", "mode"}},
                      {{"op", "wait_request"}, {"request", "owner"}, {"signal", "performance"}},
                      {{"op", "omitted"},
                       {"request", "screen"},
                       {"reason", "screen overlays excluded"},
                       {"unit", "ShowBattleScreenEffect"},
                       {"source", "official/OnStart[2]"},
                       {"dependencies", Json::array({"camera"})}},
                      {{"op", "wait_request"}, {"request", "screen"}, {"signal", "progress"}, {"progress", 0.5}},
                      {{"op", "animation"}, {"request", "body"}, {"participant", "body"}, {"state", "Hold"}},
                      {{"op", "repeat"},
                       {"until_finish", true},
                       {"child",
                        {{"op", "serial"},
                         {"children", Json::array({{{"op", "wait_frame"}},
                                                   {{"op", "branch"},
                                                    {"condition", live_flag},
                                                    {"yes", marker("flag", "LiveFlag", true)},
                                                    {"no", marker("flag", "LiveFlag", false)}},
                                                   {{"op", "branch"},
                                                    {"condition", live_text},
                                                    {"yes", marker("text", "LiveText", true)},
                                                    {"no", marker("text", "LiveText", false)}}})}}}}})}};
    doc["sequences"]["configure"]["entry"] = {
        {"op", "serial"},
        {"children", Json::array({{{"op", "context_value"},
                                   {"request", "flag"},
                                   {"name", "mode"},
                                   {"variable", "enabled"},
                                   {"operation", "set"},
                                   {"value_input", "enabled"}},
                                  {{"op", "context_value"},
                                   {"request", "text"},
                                   {"name", "mode"},
                                   {"variable", "label"},
                                   {"operation", "set"},
                                   {"value_input", "label"}},
                                  {{"op", "wait_request"}, {"request", "flag"}, {"signal", "performance"}},
                                  {{"op", "wait_request"}, {"request", "text"}, {"signal", "performance"}}})}};
    doc["sequences"]["idle"]["entry"] = {
        {"op", "animation"}, {"request", "idle"}, {"participant", "body"}, {"state", "StandBy"}};
    doc["tasks"]["watch"] = {{"entry", "watch"},
                             {"mode", "replace"},
                             {"next", {{"task", "idle"}}},
                             {"inputs",
                              {{"enabled", {{"type", "bool"}, {"default", false}}},
                               {"label", {{"type", "text"}, {"default", "captured"}}}}}};
    doc["tasks"]["configure"] = {
        {"entry", "configure"},
        {"mode", "input"},
        {"inputs",
         {{"enabled", {{"type", "bool"}, {"default", true}}}, {"label", {{"type", "text"}, {"default", "updated"}}}}},
        {"presentation", {{"kind", "setting"}, {"contexts", Json::array({"mode"})}}}};
    doc["tasks"].erase("charged");
    auto parsed = ParseLegacyTaskCatalog(doc.dump());
    Check(parsed.tasks.at("configure").definition.mode == TaskExecutionMode::Input &&
          parsed.tasks.at("configure").definition.presentation->setting);
    auto literal = doc;
    auto& literal_flag = literal["sequences"]["configure"]["entry"]["children"][0];
    literal_flag.erase("value_input");
    literal_flag["value"] = false;
    auto& literal_text = literal["sequences"]["configure"]["entry"]["children"][1];
    literal_text.erase("value_input");
    literal_text["value"] = "literal";
    const auto literals = ParseLegacyTaskCatalog(literal.dump());
    const auto& literal_nodes = literals.sequences.at("configure")->nodes;
    Check(!std::get<bool>(
        std::get<TaskLifetimeCommand>(std::get<TaskSubmit>(literal_nodes[0].operation).operation).set_value->value));
    Check(
        std::get<std::string>(std::get<TaskLifetimeCommand>(std::get<TaskSubmit>(literal_nodes[1].operation).operation)
                                  .set_value->value) == "literal");
    literal_flag["operation"] = "add";
    Reject([&] { ParseLegacyTaskCatalog(literal.dump()); });
    auto invalid = doc;
    invalid["tasks"]["configure"]["mode"] = "guess";
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });
    invalid = doc;
    invalid["sequences"]["configure"]["entry"]["children"][0]["value"] = true;
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });
    invalid = doc;
    invalid["sequences"]["watch"]["entry"]["children"][5]["child"]["children"][1]["condition"]["context"] = "unknown";
    Reject([&] { ParseLegacyTaskCatalog(invalid.dump()); });
    const auto live_markers = [](const Fixture& f) {
        bool flag = false, text = false;
        for (const auto& command : f.attachments) {
            if (command.points == std::vector<std::string>{"LiveFlag"})
                flag = command.visible;
            if (command.points == std::vector<std::string>{"LiveText"})
                text = command.visible;
        }
        return flag && text;
    };
    Fixture f(doc);
    f.Select("watch");
    for (int i = 0; i < 6; ++i)
        f.Tick();
    Check(f.controller->Status().error.empty() && f.plays.size() == 1 && f.plays.front().first.state == "Hold");
    const auto execution = f.controller->Status().execution->token.value;
    Check(f.controller->Status().execution->skipped.size() == 1 && !live_markers(f));
    const auto requests = f.controller->Requests();
    const auto omission =
        std::find_if(requests.begin(), requests.end(), [](const auto& r) { return r.name == "screen"; });
    Check(omission != requests.end() && omission->state.skipped && !omission->state.started &&
          !omission->state.performance_complete && !omission->state.progress);
    TaskSelection configure{{"test", "configure"}};
    configure.query.inputs = {{"enabled", true}, {"label", std::string("updated")}};
    Check(f.controller->Select(configure).query.availability == TaskAvailability::Available);
    for (int i = 0; i < 8; ++i)
        f.Tick();
    Check(f.controller->Status().error.empty() && f.plays.size() == 1 && live_markers(f));
    Check(f.controller->Status().execution->token.value == execution &&
          f.controller->Selection()->task.name == "watch");
    Check(std::get<bool>(f.controller->Selection()->query.inputs.at("enabled")) == false &&
          std::get<std::string>(f.controller->Selection()->query.inputs.at("label")) == "captured");
    Check(std::get<bool>(f.controller->Context().Value({"mode", "test"}, "enabled")->value));
    const auto token = f.scene->ContextToken("mode");
    Check(std::get<std::string>(f.scene->TaskContextValues(token).at("label")) == "updated");
    f.controller->RequestFinish();
    for (int i = 0; i < 6; ++i)
        f.Tick();
    Check(f.controller->Selection()->task.name == "idle" && f.plays.back().first.state == "StandBy");
    SceneLayout layout;
    layout.scene = "script-contract-map";
    SavedCharacter actor;
    actor.key = 1;
    actor.source = BrowseNpc{1110};
    actor.placement = {{0, 0, 0}, {0, 0, 0, 1}};
    actor.restore.visual_state = f.scene->Capture();
    actor.task_catalog = "test";
    actor.restore.task_selection = f.controller->Selection();
    layout.characters.push_back(actor);
    const auto saved = DecodeSceneLayout(EncodeSceneLayout(layout));
    Check(SameLayout(layout, saved) && saved.characters[0].restore.task_selection->task.name == "idle");
    Fixture restored(doc);
    restored.scene->Restore(*saved.characters[0].restore.visual_state);
    for (int i = 0; i < 4; ++i)
        restored.Tick();
    Check(restored.controller->Select(*saved.characters[0].restore.task_selection).query.availability == TaskAvailability::Available);
    for (int i = 0; i < 4; ++i)
        restored.Tick();
    Check(restored.plays.back().first.state == "StandBy");
    restored.Select("watch");
    for (int i = 0; i < 6; ++i)
        restored.Tick();
    Check(restored.controller->Status().error.empty() && live_markers(restored));
    Check(std::get<bool>(restored.scene->TaskContextValues(restored.scene->ContextToken("mode")).at("enabled")));
    // An explicit non-interrupting input may name its next entry. The parser
    // preserves that authored edge instead of imposing a second permission rule.
    auto next_input = doc;
    next_input["tasks"]["configure"]["next"] = {{"task", "idle"}};
    Check(ParseLegacyTaskCatalog(next_input.dump()).tasks.at("configure").next->task.name == "idle");
    Fixture continued(next_input);
    continued.Select("watch");
    for (int i = 0; i < 6; ++i)
        continued.Tick();
    Check(continued.plays.size() == 1 && continued.plays.front().first.state == "Hold");
    continued.controller->Select(configure);
    for (int i = 0; i < 10; ++i)
        continued.Tick();
    Check(continued.controller->Status().error.empty() && continued.controller->Selection()->task.name == "idle" &&
          continued.plays.back().first.state == "StandBy" && continued.plays.front().second->released);
    Check(std::get<bool>(continued.scene->TaskContextValues(continued.scene->ContextToken("mode")).at("enabled")));
}
}
void TestScriptFormat() {
    {
        const auto selected = ParseScript(R"({"format":"hksr.script","id":"selection","applies_to":"fixture",
          "actions":{"pick":{"run":[{"op":"switch","name":"choice","cases":[
            {"is":1,"label":"first","run":[{"op":"sound","event":"first"}]},
            {"is":2,"run":[{"op":"sound","event":"second"}]}],
            "default":[{"op":"sound","event":"fallback"}]}]}}})");
        for (int choice : {1, 2, 3}) {
            std::vector<std::string> events;
            TaskSequenceServices services;
            services.submit = [&](auto execution, auto id, const TaskSubmit& step) {
                events.push_back(std::get<TaskSoundCommand>(step.operation).event);
                TaskRequestState state;
                state.submitted = state.request_complete = state.performance_complete = state.drained = true;
                return TaskSubmission{{execution,id,1,{"body"},1},state};
            };
            services.condition = [choice](const TaskRequirement& condition) {
                TaskContext context;
                TaskQuery query;
                query.inputs["choice"] = std::int64_t(choice);
                return context.Evaluate(condition, query).availability;
            };
            services.release = [](const auto&,auto) {};
            const auto& definition = selected->tasks->tasks.at("pick").definition;
            TaskSequenceExecution run({1}, selected->tasks->sequences.at(definition.entry), services);
            run.Advance(.1, 1);
            Check(events == std::vector<std::string>{choice == 1 ? "first" : choice == 2 ? "second" : "fallback"});
            Check(run.Status().issues.empty());
        }
        Reject([&] { ParseScript(R"({"format":"hksr.script","id":"x","applies_to":"fixture",
          "actions":{"x":{"run":[{"op":"switch","name":"x","cases":[
            {"is":1,"run":[]},{"is":1,"run":[]}]}]}}})"); });
        const auto priority = ParseScript(R"({"format":"hksr.script","id":"priority","applies_to":"fixture",
          "actions":{"pick":{"run":[{"op":"if","condition":{"op":"phase","is":"one"},
            "then":[{"op":"sound","event":"first"}],"elif":[
              {"condition":{"op":"phase","is":"two"},"then":[{"op":"sound","event":"second"}]},
              {"condition":{"op":"phase","is":"two"},"then":[{"op":"sound","event":"never"}]}],
            "else":[{"op":"sound","event":"fallback"}]}]}}})");
        for (const auto& phase : {"one", "two", "other"}) {
            std::vector<std::string> events;
            unsigned reads = 0;
            TaskSequenceServices services;
            services.submit = [&](auto execution, auto id, const TaskSubmit& step) {
                events.push_back(std::get<TaskSoundCommand>(step.operation).event);
                TaskRequestState state;
                state.submitted = state.request_complete = state.performance_complete = state.drained = true;
                return TaskSubmission{{execution,id,1,{"body"},1},state};
            };
            services.condition = [&](const TaskRequirement& condition) {
                ++reads;
                return std::get<TaskPhaseEquals>(condition).phase == phase
                    ? TaskAvailability::Available : TaskAvailability::Unavailable;
            };
            services.release = [](const auto&,auto) {};
            const auto& definition = priority->tasks->tasks.at("pick").definition;
            TaskSequenceExecution run({1}, priority->tasks->sequences.at(definition.entry), services);
            run.Advance(.1, 1);
            const auto expected = std::string(phase) == "one" ? "first"
                                : std::string(phase) == "two" ? "second" : "fallback";
            Check(events == std::vector<std::string>{expected});
            Check(reads == (std::string(phase) == "one" ? 1u : std::string(phase) == "two" ? 2u : 3u));
        }
    }
    const std::string text = R"({
      "format":"hksr.script","id":"direct","applies_to":"fixture","default":true,
      "defaults":{"choices":{"variant":"a"},"targets":{"Target":{"local_position":[0,0,3]}}},
      "resources":{"effects":{}},
      "states":{"charge":{"state_only":true,"values":{"ready":false}}},"initial_states":["charge"],
      "functions":{"mark":[{"op":"sound","event":"marked","as":"marker","wait":"accepted"}]},
      "actions":{
        "idle":{"run":[{"op":"animation","state":"StandBy"}]},
        "attack":{"inputs":{"enhanced":{"type":"bool","default":true}},"run":[
          {"op":"animation","state":"Skill01","as":"body"},
          {"op":"if","condition":{"op":"value","name":"enhanced","is":true},
            "then":[{"op":"call","function":"mark"}],"else":[]},
          {"op":"wait","for":"body","progress":0.35},
          {"op":"start","id":"tail","join":true,"run":[{"op":"wait","frame":true}]},
          {"op":"join"},{"op":"wait","for":"body"}],
          "on_interrupt":[{"op":"sound","event":"cancel"}],"next":"idle"}
      }
    })";
    const auto parsed = ParseScript(text);
    Check(parsed->default_selection == true && parsed->choices.at("variant") == "a" &&
          parsed->target_characters.empty() && parsed->program->target_defaults.count("Target") == 1);
    Check(parsed->tasks->tasks.size() == 2 && parsed->tasks->contexts.count("charge") == 1);
    Check(parsed->program->contexts.at("charge").restore.has_value());
    const auto& attack = parsed->tasks->Resolve({"direct","attack"});
    const auto plan = parsed->tasks->sequences.at(attack.definition.entry);
    Check(plan->interruption && attack.next->task.name == "idle");
    std::vector<std::string> trace;
    TaskSequenceServices services;
    services.submit = [&](auto execution, auto id, const TaskSubmit& step) {
        trace.push_back(step.request);
        TaskRequestState state;
        state.submitted = state.request_complete = state.started = state.performance_complete = state.drained = true;
        state.progress = 1;
        return TaskSubmission{{execution,id,1,{"body"},1},state};
    };
    services.condition = [](const auto&) { return TaskAvailability::Available; };
    services.release = [](const auto&,auto) {};
    TaskSequenceExecution execution({1},plan,services);
    execution.Advance(.1,1);
    execution.Advance(.1,2);
    execution.Advance(.1,3);
    Check(execution.Status().issues.empty() && trace == std::vector<std::string>({"body","marker"}));
    auto invalid = Json::parse(text);
    invalid["domain"] = "battle";
    Reject([&] { ParseScript(invalid.dump()); });
    Reject([&] { ParseTaskCatalog(Document().dump()); });
    Reject([&] { ParseVisualProgram(R"({"format":"hksr.visual-program"})"); });
    invalid = Json::parse(text);
    invalid["states"]["@playback"] = {{"state_only",true}};
    Reject([&] { ParseScript(invalid.dump()); });
    std::cout << "Direct script grammar and typed execution passed.\n";

    auto playback_document = Json::parse(R"({
      "format":"hksr.script","id":"test","applies_to":"fixture",
      "actions":{
        "hold":{"runtime":{"selected":"Hold","policy":"performance"},"run":[
          {"op":"playback.value","variable":"performance","operation":"set","value":"working"},
          {"op":"animation","state":"Hold","as":"body"},{"op":"wait","for":"body"}]},
        "input":{"mode":"input","runtime":{"policy":"setting"},"run":[
          {"op":"playback.value","variable":"selected","operation":"set","value":"manual"}]},
        "resume":{"requires":[{"op":"playback","name":"selected","is":"manual"}],
          "runtime":{"selected":"StandBy","policy":"ready"},
          "run":[{"op":"animation","state":"StandBy"}]}
      }
    })");
    Fixture playback(playback_document);
    playback.Tick();
    Check(playback.Select("hold").query.availability == TaskAvailability::Available);
    for (int i = 0; i < 4; ++i) playback.Tick();
    Check(playback.controller->Status().error.empty());
    Check(std::get<std::string>(playback.controller->Context().Value({TaskPlaybackContext,"test"},"performance")->value) == "working");
    Check(playback.Select("input").query.availability == TaskAvailability::Available);
    for (int i = 0; i < 4; ++i) playback.Tick();
    Check(playback.controller->Selection()->task.name == "hold" && playback.plays.size() == 1);
    Check(std::get<std::string>(playback.controller->Context().Value({TaskPlaybackContext,"test"},"selected")->value) == "manual");
    Check(playback.Select("resume").query.availability == TaskAvailability::Available);
    for (int i = 0; i < 4; ++i) playback.Tick();
    Check(playback.controller->Status().error.empty() && playback.plays.back().first.state == "StandBy");
    Check(!playback.controller->Context().Value({"actor","test"},"performance"));
    auto invalid_playback = playback_document;
    invalid_playback["actions"]["input"]["run"][0]["value"] = 1;
    Reject([&] { ParseScript(invalid_playback.dump()); });
    std::cout << "Playback property writes, predicates and non-interrupting input passed.\n";

    const auto scene = ParseScript(R"({
      "format":"hksr.script","id":"cued","applies_to":"fixture",
      "states":{"persistent":{"restore":[]}},
      "actions":{
        "perform":{"run":[{"op":"animation.cued","state":"Skill02","restart_scope":true,
          "prepare":[{"op":"scope.enter","name":"persistent"},
                     {"op":"timeline.play","key":"preload","path":"prepare.playable"}],
          "tracks":[
            [{"at":0.2,"run":[{"op":"effect.remove_event","path":"blocked"}]},
             {"at":0.8,"run":[{"op":"effect.remove_event","path":"late"}]}],
            [{"at":0.3,"run":[{"op":"effect.remove_event","path":"independent"}]}]
          ],"then":{"state":"StandBy","at":0.25,
                     "run":[{"op":"effect.remove_event","path":"successor-tail"}]}}]},
        "continue":{"run":[{"op":"animation.cued","state":"Hold"}]}
      }
    })");
    Check(scene->tasks->tasks.empty() && scene->program->actions.size() == 2);
    bool prepared = false, released = false;
    unsigned attached = 0, detached = 0;
    std::vector<std::string> effects;
    VisualProgramServices visual;
    visual.effects = std::make_shared<api::PresentationEffects>(
        api::EffectRequestServices{{},api::EffectAccess({}),{},{},{}});
    visual.create = [](auto&, const auto&, const auto&) -> VisualEffectId {
        throw std::runtime_error("unexpected effect submission in cued protocol test");
    };
    visual.attach = [&](auto) { ++attached; };
    visual.detach = [&] { ++detached; };
    visual.timeline = [&](auto,const auto&) {
        return VisualStepResult{prepared ? VisualStepState::Done : VisualStepState::Pending,{}};
    };
    visual.cancel_timelines = [](auto) {};
    visual.timelines_drained = [] { return true; };
    visual.remove_event_effect = [&](const auto& command) {
        if (command.path == "blocked" && !released) return VisualStepResult{VisualStepState::Pending,{}};
        effects.push_back(command.path);
        return VisualStepResult{VisualStepState::Done,{}};
    };
    VisualProgramPlayer player(scene->program,visual);
    const auto intent = player.Select("perform");
    player.Service();
    Check(!player.Status().prepared && !player.Played(intent,1,1));
    const auto persistent = player.ContextToken("persistent");
    Check(persistent != 0);
    prepared = true;
    player.Service();
    Check(player.Status().prepared && player.Played(intent,1,1));
    player.Observe(1,1,"Skill02",.3f);
    player.Service();
    Check(effects == std::vector<std::string>({"independent"}) && !player.Status().finished);
    released = true;
    player.Observe(1,1,"Skill02",.9f);
    player.Service();
    Check(effects == std::vector<std::string>({"independent","blocked","late"}) && player.Status().finished);
    Check(player.SuccessorPlayed(intent,2,1));
    player.Observe(2,1,"StandBy",.2f); player.Service();
    Check(effects.size() == 3 && !player.Status().completed);
    player.Observe(2,1,"StandBy",.25f); player.Service();
    Check(player.Status().completed && effects.back() == "successor-tail");
    Check(player.ContextToken("persistent") == persistent);
    player.Select("perform"); player.Service();
    player.InterruptAction(); player.Service();
    Check(player.ContextToken("persistent") == persistent && detached != 0 && attached == 2);
    player.Leave();
    for (int i = 0; i < 4; ++i) player.Service();
    Check(player.ActiveContexts().empty());
    auto bad_scene = Json::parse(R"({"format":"hksr.script","id":"bad","applies_to":"fixture",
      "actions":{"x":{"run":[{"op":"animation.cued","state":"A"},{"op":"sound","event":"x"}]}}})");
    Reject([&] { ParseScript(bad_scene.dump()); });
    std::cout << "Cued animation preparation, independent tracks, successor clock and scope retirement passed.\n";
}
void TestTaskCatalog() {
    auto run_case = [](const char* name, auto run) {
        try { run(); }
        catch (const std::exception& error) { throw std::runtime_error(std::string(name) + ": " + error.what()); }
    };
    run_case("direct script format", TestScriptFormat);
    {
        auto parse = [](const Json& entry) {
            auto document = Document();
            document["sequences"]["stand"]["entry"] = entry;
            return ParseLegacyTaskCatalog(document.dump());
        };
        auto leaf = [](const TaskCatalog& catalog) -> const TaskLeafOperation& {
            const auto& sequence = *catalog.sequences.at("stand");
            return std::get<TaskSubmit>(sequence.nodes.at(sequence.entry).operation).operation;
        };
        const Json compact = {{"op", "animation"}, {"request", "body"}, {"state", "A"},
                              {"end", 0}, {"track", {{"logic", 3}}}};
        const auto minimal = parse(compact);
        const auto& animation = std::get<TaskAnimationCommand>(leaf(minimal));
        Check(animation.participant.name == "body" && animation.layer == 0 && animation.transition == 0 &&
              animation.normalized_start == 0 && animation.normalized_end == 0 && animation.restart &&
              !animation.current_idle && animation.track && animation.track->extra == 0 &&
              animation.track->release_on.empty() && !animation.track->force);
        auto explicit_defaults = compact;
        explicit_defaults.update({{"participant", "body"}, {"layer", 0}, {"transition", 0}, {"start", 0},
                                  {"restart", true}, {"current_idle", false}});
        explicit_defaults["track"].update({{"extra", 0}, {"release_on", Json::array()}, {"force", false}});
        const auto expanded = parse(explicit_defaults);
        const auto& same = std::get<TaskAnimationCommand>(leaf(expanded));
        Check(same.participant.name == animation.participant.name && same.layer == animation.layer &&
              same.transition == animation.transition && same.normalized_start == animation.normalized_start &&
              same.normalized_end == animation.normalized_end && same.restart == animation.restart &&
              same.track->release_on == animation.track->release_on && same.track->force == animation.track->force);
        explicit_defaults["restart"] = false;
        Check(!std::get<TaskAnimationCommand>(leaf(parse(explicit_defaults))).restart);
        explicit_defaults["participant"] = "";
        Reject([&] { parse(explicit_defaults); });
        explicit_defaults = compact;
        explicit_defaults["track"]["force"] = 0;
        Reject([&] { parse(explicit_defaults); });
        explicit_defaults = compact;
        explicit_defaults["track"]["release_on"] = nullptr;
        Reject([&] { parse(explicit_defaults); });

        const auto empty = parse({{"op", "serial"}});
        const auto expanded_empty = parse({{"op", "serial"}, {"children", Json::array()}});
        Check(empty.sequences.at("stand")->nodes.size() == 1 &&
              std::get<TaskSerial>(empty.sequences.at("stand")->nodes[0].operation).children.empty() &&
              std::get<TaskSerial>(expanded_empty.sequences.at("stand")->nodes[0].operation).children.empty());
        Reject([&] { parse({{"op", "serial"}, {"children", nullptr}}); });
        const auto sound = parse({{"op", "sound"}, {"request", "voice"}, {"event", "test"}});
        Check(std::get<TaskSoundCommand>(leaf(sound)).participant.name == "body");
        const auto expression = parse({{"op", "expression"}, {"request", "face"}, {"state", "Emo_Default"}});
        Check(std::get<TaskExpressionCommand>(leaf(expression)).transition == .2f);
        const auto hit = parse({{"op", "hit"}, {"request", "hit"}, {"target", "Caster"}, {"animation", "Hit"},
                                {"effect", {{"path", "effect"}}}, {"direction", Json::object()}});
        const auto& hit_command = std::get<TaskHitCommand>(leaf(hit));
        Check(hit_command.events.empty() && !hit_command.allow_position && hit_command.hit.direction &&
              hit_command.hit.direction->horizontal == 0 && hit_command.hit.direction->vertical == 0);
        const auto& effect = *hit_command.hit.effect;
        Check(effect.path == "effect" && effect.height == -1 && effect.angle == 0 && effect.delay == 0 &&
              effect.minimum_interval == 0 && effect.maximum_count == 0 && !effect.offset_by_scale);
        auto full_hit = Json{{"op", "hit"}, {"request", "hit"}, {"target", "Caster"}, {"animation", "Hit"},
                             {"effect", {{"path", "effect"}, {"height", -1}, {"angle", 0}, {"delay", 0},
                                         {"minimum_interval", .05}, {"maximum_count", 1}, {"offset_by_scale", false}}}};
        const auto limited_hit = parse(full_hit);
        const auto& limited = *std::get<TaskHitCommand>(leaf(limited_hit)).hit.effect;
        Check(limited.minimum_interval == .05f && limited.maximum_count == 1);
        full_hit["effect"]["maximum_count"] = 1.5;
        Reject([&] { parse(full_hit); });
    }
    std::cout << "Script leaf defaults and invalid input cases passed.\n";
    {
        auto document = Document();
        document["sequences"]["movement"]["entry"] = {
            {"op", "movement"}, {"request", "move"}, {"clock", "instant"},
            {"segments", Json::array({{{"destination", "origin"}}})}};
        document["tasks"]["movement"] = {{"entry", "movement"}};
        document["visual"]["contexts"]["moving"] = {{"restore", Json::array()}};
        document["contexts"]["moving"] = {{"callbacks", {{"enter", "movement"}}}};
        document["sequences"]["enter-moving"]["entry"] = {
            {"op", "context_enter"}, {"request", "enter"}, {"name", "moving"}};
        document["tasks"]["enter-moving"] = {{"entry", "enter-moving"}};
        const auto catalog = ParseLegacyTaskCatalog(document.dump());
        Check(!catalog.Prepared("idle").needs_movement_origin);
        Check(catalog.Prepared("movement").needs_movement_origin);
        Check(catalog.Prepared("enter-moving").needs_movement_origin);
        Check(catalog.ContextNeedsMovementOrigin("moving"));
        Check(!catalog.ContextNeedsMovementOrigin("enhanced"));
        // A moving sibling must not add a position-read dependency to an idle.
        Fixture f(document);
        f.Select("idle");
        f.Tick();
        Check(!f.origin_capture.empty() &&
              std::none_of(f.origin_capture.begin(), f.origin_capture.end(), [](bool needed) { return needed; }));
        // The immutable boundary also detaches nested mutable visual aliases.
        TaskCatalogInput input(catalog);
        auto visual = std::make_shared<VisualProgram>(*catalog.visual);
        input.visual = visual;
        TaskCatalog detached(input);
        visual->contexts.clear();
        input.tasks.clear();
        Check(detached.visual->contexts.count("moving") && detached.tasks.count("idle") &&
              detached.Prepared("enter-moving").needs_movement_origin);
        TaskSequenceInput sequence(*catalog.sequences.at("movement"));
        const TaskSequence prepared(sequence);
        sequence.nodes.clear();
        Check(prepared.nodes.size() == 1 && prepared.NeedsMovementOrigin());
    }
    std::cout << "Script movement preparation and immutable plans passed.\n";
    run_case("persistent visual reaction", PersistentVisualReaction);
    run_case("manual reaction ownership", ManualReactionOwnership);
    run_case("normal body cleanup", NormalBodyClearsOnlyManualReaction);
    run_case("explicit context restore", ExplicitContextRestore);
    run_case("script state contracts", ScriptStateContracts);
    run_case("script consumer contracts", ScriptConsumerContracts);
    {
        auto document = Document();
        document["sequences"]["common"]["entry"] = {
            {"op", "expression"}, {"request", "face"}, {"state", "Emo_Default"}};
        document["sequences"]["stand"]["entry"] = {
            {"op", "serial"},
            {"children", Json::array({{{"op", "call_sequence"}, {"sequence", "common"}},
                                      {{"op", "wait_request"}, {"request", "face"}, {"signal", "accepted"}},
                                      {{"op", "call_sequence"}, {"sequence", "common"}},
                                      {{"op", "wait_request"}, {"request", "face"}, {"signal", "accepted"}}})}};
        const auto parsed = ParseLegacyTaskCatalog(document.dump());
        const auto& plan = *parsed.sequences.at("stand");
        const auto& children = std::get<TaskSerial>(plan.nodes.at(plan.entry).operation).children;
        Check(std::get<TaskSerial>(plan.nodes.at(children[0]).operation).children ==
              std::get<TaskSerial>(plan.nodes.at(children[2]).operation).children);
        Fixture actor(document);
        actor.Select("idle");
        for (int frame = 0; frame < 5; ++frame)
            actor.Tick();
        Check(actor.controller->Drained() && actor.controller->Status().error.empty() && actor.expressions.size() == 2);
        auto bad = document;
        bad["sequences"]["common"]["entry"] = {{"op", "call_sequence"}, {"sequence", "stand"}};
        Reject([&] { ParseLegacyTaskCatalog(bad.dump()); });
        bad = document;
        bad["sequences"]["common"]["entry"] = {{"op", "call_sequence"}, {"sequence", "absent"}};
        Reject([&] { ParseLegacyTaskCatalog(bad.dump()); });
        bad = document;
        bad["sequences"]["common"]["interruption"] = {{"op", "wait_frame"}};
        Reject([&] { ParseLegacyTaskCatalog(bad.dump()); });
        bad = document;
        bad["sequences"]["common"]["entry"]["transition"] = -.1;
        Reject([&] { ParseLegacyTaskCatalog(bad.dump()); });
        for (const char* signal : {"started", "progress", "animation_end", "state_left", "animation_finished"}) {
            bad = document;
            auto& wait = bad["sequences"]["stand"]["entry"]["children"][1];
            wait["signal"] = signal;
            if (std::string(signal) == "progress")
                wait["progress"] = .5;
            Reject([&] { ParseLegacyTaskCatalog(bad.dump()); });
        }
    }
    {
        auto document = Document();
        document["sequences"]["stand"]["entry"] = {
            {"op", "for_targets"},
            {"bind", "ParamEntity"},
            {"targets", Json::array({"TargetA", "TargetB"})},
            {"child",
             {{"op", "visual"},
              {"request", "face"},
              {"instruction", {{"op", "face_target"}, {"target", "ParamEntity"}, {"source", "retarget/body"}}}}},
            {"empty", {{"op", "wait_frame"}}}};
        Fixture scoped(document);
        scoped.Select("idle");
        scoped.Tick();
        scoped.Tick();
        Check(scoped.faces.size() == 2 && scoped.faces[0].target == "TargetA" && scoped.faces[1].target == "TargetB");
        document["sequences"]["stand"]["entry"]["bind"] = "Caster";
        Reject([&] { ParseLegacyTaskCatalog(document.dump()); });
    }
    {
        auto document = Document();
        document["sequences"]["stand"]["entry"] = {
            {"op", "serial"},
            {"children",
             Json::array(
                 {{{"op", "animation"}, {"request", "body"}, {"participant", "body"}, {"state", "Attack"}},
                  {{"op", "movement"},
                   {"request", "move"},
                   {"animation", "body"},
                   {"segments",
                    Json::array({{{"destination", "target"},
                                  {"name", "Target"},
                                  {"sample", {{"percentage", 0.5}, {"distance", -2}, {"behind_target", true}}}}})}}})}};
        const auto catalog = ParseLegacyTaskCatalog(document.dump());
        const auto& move = std::get<TaskMovementCommand>(
                               std::get<TaskSubmit>(catalog.sequences.at("stand")->nodes[1].operation).operation)
                               .movement;
        Check(move.segments[0].sample && move.segments[0].sample->percentage == .5f &&
              move.segments[0].sample->distance == -2);
        document["sequences"]["stand"]["entry"]["children"][1]["segments"][0]["stop_distance"] = 1;
        Reject([&] { ParseLegacyTaskCatalog(document.dump()); });
    }
    auto callback_test = [](const char* name, auto run) {
        try {
            run();
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string(name) + ": " + error.what());
        }
    };
    callback_test("callback lifetime", CallbackLifetimes);
    callback_test("callback restoration", CallbackRestorationAndFailures);
    callback_test("callback events", CallbackEvents);
    callback_test("callback playback handoff", CallbackPlaybackHandoff);
    callback_test("independent inputs", IndependentInputs);
    callback_test("callback numbers", CallbackNumbers);
    callback_test("callback hit effects", CallbackHitEffects);
    callback_test("callback number references", CallbackNumberReferences);
    callback_test("callback drain", CallbackNativeDrainAndNestedOwners);
    callback_test("callback instances", CallbackInstances);
    callback_test("callback multiple", CallbackMultipleAndRemoval);
    callback_test("callback replacement", CallbackReplacementAndCollections);
    callback_test("callback receivers", CallbackReceivers);
    callback_test("callback presence", CallbackPresence);
    run_case("codec", Codec);
    run_case("selection and interruption", SelectionAndInterrupt);
    run_case("animation offsets", AnimationOffsets);
    run_case("natural and pending", NaturalAndPending);
    run_case("contexts and finish", ContextsAndFinish);
    run_case("close and reentrant", CloseAndReentrant);
    run_case("requirements and preparation", InputRequirementsAndPreparation);
    auto input = Document();
    {
        auto queued = Document();
        auto& sequence = queued["sequences"]["stand"]["entry"];
        sequence = {
            {"op", "serial"},
            {"children",
             Json::array(
                 {{{"op", "animation"},
                   {"request", "body"},
                   {"participant", "body"},
                   {"state", "Skill"},
                   {"end", .58},
                   {"track", {{"logic", 3}, {"extra", 0}, {"release_on", Json::array({6})}, {"force", false}}}},
                  {{"op", "wait_request"}, {"request", "body"}, {"signal", "animation_finished"}},
                  {{"op", "expression"}, {"request", "return"}, {"state", "Emo_Default"}, {"transition", .2}}})}};
        const auto parsed = ParseLegacyTaskCatalog(queued.dump());
        const auto& nodes = parsed.sequences.at("stand")->nodes;
        const auto& animation = std::get<TaskAnimationCommand>(std::get<TaskSubmit>(nodes[0].operation).operation);
        Check(animation.track && animation.track->logic == 3 && animation.track->release_on == std::vector<int>{6});
        Check(std::holds_alternative<TaskExpressionCommand>(std::get<TaskSubmit>(nodes[2].operation).operation));
        sequence["children"][0].erase("end");
        Reject([&] { ParseLegacyTaskCatalog(queued.dump()); });
    }
    {
        auto abilities = Document();
        abilities["sequences"]["stand"]["entry"] = {{"op", "serial"},
                                                    {"children", Json::array({{{"op", "call_ability"},
                                                                               {"ability", "phase"},
                                                                               {"wait_for_performance", false},
                                                                               {"child", {{"op", "wait_frame"}}}},
                                                                              {{"op", "wait_abilities"}}})}};
        const auto parsed = ParseLegacyTaskCatalog(abilities.dump());
        const auto& call = std::get<TaskAbilityCall>(parsed.sequences.at("stand")->nodes[1].operation);
        Check(call.ability == "phase" && !call.wait_for_performance && call.child == 0);
        abilities["sequences"]["stand"]["entry"]["children"][0].erase("wait_for_performance");
        const auto default_call = ParseLegacyTaskCatalog(abilities.dump());
        Check(!std::get<TaskAbilityCall>(default_call.sequences.at("stand")->nodes[1].operation).wait_for_performance);
        abilities["sequences"]["stand"]["entry"]["children"][0]["wait_for_performance"] = nullptr;
        Reject([&] { ParseLegacyTaskCatalog(abilities.dump()); });
    }
    {
        auto poses = Document();
        poses["sequences"]["stand"]["entry"] = {
            {"op", "ready_pose"}, {"request", "ready"}, {"config", "Battle/A"}, {"skill", 8}, {"variant", 4}};
        auto parsed = ParseLegacyTaskCatalog(poses.dump());
        const auto& ready = std::get<ReadyPoseSelection>(
            std::get<TaskPoseCommand>(std::get<TaskSubmit>(parsed.sequences.at("stand")->nodes[0].operation).operation)
                .selection);
        Check(ready.source == "Battle/A" && ready.skill == 8 && ready.variant == 4);
        auto progress = poses;
        progress["sequences"]["stand"]["entry"] = {
            {"op", "serial"},
            {"children",
             Json::array({poses["sequences"]["stand"]["entry"],
                          {{"op", "wait_request"}, {"request", "ready"}, {"signal", "progress"}, {"progress", 0.5}}})}};
        Reject([&] { ParseLegacyTaskCatalog(progress.dump()); });
        poses["sequences"]["stand"]["entry"]["skill"] = -1;
        Reject([&] { ParseLegacyTaskCatalog(poses.dump()); });
        poses["sequences"]["stand"]["entry"] = {
            {"op", "reaction"}, {"request", "recover"}, {"config", "Battle/A"}, {"flag", 99}, {"recover", true}};
        const auto reaction_catalog = ParseLegacyTaskCatalog(poses.dump());
        const auto& recovery = std::get<ReactionSelection>(
            std::get<TaskPoseCommand>(std::get<TaskSubmit>(reaction_catalog.sequences.at("stand")->nodes[0].operation).operation)
                .selection);
        Check(recovery.flag == 99 && recovery.recover && recovery.source == "Battle/A");
        poses["sequences"]["stand"]["entry"]["participant"] = "official-player";
        Reject([&] { ParseLegacyTaskCatalog(poses.dump()); });
    }
    input["sequences"]["stand"]["entry"] = {
        {"op", "serial"},
        {"children",
         Json::array({{{"op", "visual"},
                       {"request", "turn"},
                       {"instruction",
                        {{"op", "face_target"},
                         {"source", "LookAt"},
                         {"target", "AbilityTargetEntity"},
                         {"ratio", 0.5},
                         {"angle_offset", 10},
                         {"duration", 1}}}},
                      {{"op", "wait_request"}, {"request", "turn"}, {"signal", "accepted"}},
                      {{"op", "animation"}, {"request", "body"}, {"participant", "body"}, {"state", "Attack"}}})}};
    Fixture f(input);
    f.facing_ready = false;
    f.Select("idle");
    f.Tick();
    Check(f.faces.empty() && f.plays.empty());
    f.facing_ready = true;
    f.Tick();
    Check(f.faces.size() == 1 && f.plays.size() == 1 && f.faces[0].target == "AbilityTargetEntity" &&
          f.faces[0].ratio == 0.5f && f.faces[0].angle_offset == 10 && f.faces[0].duration == 1);
    f.Tick();
    Check(f.faces.size() == 1); // no per-frame rotation write, no artificial one-second wait
    input["sequences"]["stand"]["entry"] = {{"op", "visual"},
                                            {"request", "weapon"},
                                            {"instruction",
                                             {{"op", "attachment_visibility"},
                                              {"source", "SetAttachmentVisibility"},
                                              {"points", Json::array({"Weapon"})},
                                              {"visible", false}}}};
    const auto visibility = ParseLegacyTaskCatalog(input.dump());
    const auto& command = std::get<VisualAttachmentVisibility>(
        std::get<VisualInstruction>(
            std::get<TaskSubmit>(visibility.sequences.at("stand")->nodes[0].operation).operation)
            .operation);
    Check(command.points == std::vector<std::string>{"Weapon"} && !command.visible && command.delay == 0);
    Fixture attached(input);
    attached.Select("idle");
    attached.Tick();
    Check(attached.attachments.size() == 1 && !attached.attachments[0].visible);
    input["sequences"]["stand"]["entry"]["instruction"]["delay"] = -1;
    Reject([&] { ParseLegacyTaskCatalog(input.dump()); });
    auto& instruction = input["sequences"]["stand"]["entry"]["instruction"];
    instruction = {{"op", "attachment_scale"},
                   {"source", "SetAttachmentScale"},
                   {"points", Json::array({"Arrow"})},
                   {"scale", Json::array({0, 0, 0})}};
    Fixture scaled(input);
    scaled.Select("idle");
    scaled.Tick();
    Check(scaled.scales.size() == 1 && scaled.scales[0].scale == std::array<float, 3>{0, 0, 0});
    instruction["scale"] = Json::array({-1, 0, 0});
    Reject([&] { ParseLegacyTaskCatalog(input.dump()); });
    instruction = {{"op", "layer_weight"}, {"source", "SetLayerWeight"}, {"layer", "HitAdditive"}, {"weight", 0.4}};
    Fixture weighted(input);
    weighted.Select("idle");
    weighted.Tick();
    Check(weighted.weights.size() == 1 && weighted.weights[0].layer == "HitAdditive" &&
          weighted.weights[0].weight == 0.4f);
    instruction["duration"] = 0.2;
    ParseLegacyTaskCatalog(input.dump());
    instruction["duration"] = 0;
    instruction["layer"] = "";
    Reject([&] { ParseLegacyTaskCatalog(input.dump()); });
    instruction = {{"op", "remove_event_effect"},
                   {"source", "OnDestroy.RemoveEffect"},
                   {"path", "Effects/Owned.prefab"},
                   {"fade", false},
                   {"immediate", true},
                   {"unbind", true}};
    Fixture event_removed(input);
    event_removed.Select("idle");
    event_removed.Tick();
    Check(event_removed.event_removals.size() == 1 && event_removed.event_removals[0].path == "Effects/Owned.prefab" &&
          !event_removed.event_removals[0].options.fade && event_removed.event_removals[0].options.immediate_fade &&
          event_removed.event_removals[0].options.unbind);
    instruction["path"] = "";
    Reject([&] { ParseLegacyTaskCatalog(input.dump()); });
}
