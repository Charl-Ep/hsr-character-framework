#include "../support/LegacyScriptFixture.hpp"
#include <hksr/framework/tasks/TaskPresentation.hpp>
#include <hksr/model/ModelAccess.hpp>
#include <hksr/framework/presentation/scene/VisualProgramPlayer.hpp>
#include <hksr/framework/presentation/timeline/VisualTimelines.hpp>
#include <hksr/presentation/PresentationBindings.hpp>
#include <hksr/framework/tasks/TaskCatalog.hpp>
#include "../../src/serialization/AnimationParameterCodec.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <limits>

namespace {
using namespace hksr;
using namespace hksr::framework;
void CheckAt(bool ok, int line) {
    if (!ok)
        throw std::runtime_error("task presentation line " + std::to_string(line));
}
#define Check(...) CheckAt((__VA_ARGS__), __LINE__)
int __fastcall UnusedRetain(void*, int, int, void*) {
    throw std::logic_error("queue test must not retain engine objects");
}
void* __fastcall UnusedTarget(int, void*) {
    throw std::logic_error("queue test must not read engine objects");
}
void __fastcall UnusedFree(int, void*) {
    throw std::logic_error("queue test must not free engine objects");
}
void CompletedAnimationReplacement() {
    int entity = 0, world = 0, method = 0;
    const api::EntityToken owner{reinterpret_cast<api::GameEntity*>(&entity), 1};
    api::PresentationBindings bindings;
    bindings.references.retain = {reinterpret_cast<void*>(&UnusedRetain), &method};
    bindings.references.get = {reinterpret_cast<void*>(&UnusedTarget), &method};
    bindings.references.free = {reinterpret_cast<void*>(&UnusedFree), &method};
    api::ActorAnimationSession engine(owner, bindings.references, api::EntityAccess(bindings.entity),
                                      api::ModelAccess(bindings.model), api::AnimationControl(bindings.animation),
                                      api::ProfileInputs(bindings.profile_inputs));
    AnimationRequests requests(owner, reinterpret_cast<api::GameWorld*>(&world), engine);
    const auto direct = requests.Submit({"Clip", 0, 0, 0, 1.f, {}});
    // Simulates the observed non-looping clip boundary, not engine playback.
    auto observed = std::const_pointer_cast<AnimationRequestStatus>(direct);
    observed->accepted = observed->entered = observed->complete = true;
    observed->progress = 1;
    auto track = AnimationCommand{"StandBy", 0, 0, 0, 1.f, AnimationTrack{}};
    requests.Submit(track);
    Check(direct->released && !direct->cancelled && direct->error.empty());
    requests.Cancel();
    const auto bounded = requests.Submit({"Bounded", 0, 0, 0, .4f, {}});
    auto boundary = std::const_pointer_cast<AnimationRequestStatus>(bounded);
    boundary->accepted = boundary->entered = boundary->end_reached = true;
    boundary->progress = .4;
    requests.Submit(track);
    Check(bounded->released && !bounded->cancelled && !bounded->complete && bounded->error.empty());
    requests.Cancel();
    const auto unfinished = requests.Submit({"Unfinished", 0, 0, 0, 1.f, {}});
    requests.Submit(track);
    Check(unfinished->released && unfinished->cancelled && !unfinished->error.empty());
    requests.Cancel();
    Check(engine.Close());
}
TaskNode Visual(const char* name, VisualOperation operation) {
    return {name, TaskSubmit{name, VisualInstruction{std::move(operation), name}}};
}
std::shared_ptr<TaskSequence> Plan(std::initializer_list<TaskNode> nodes, TaskNodeIndex entry = 0) {
    TaskSequenceInput input;
    input.nodes = nodes;
    input.entry = entry;
    return std::make_shared<TaskSequence>(input);
}
struct Fixture {
    unsigned starts = 0, stops = 0, attached = 0, detached = 0;
    std::map<int, api::TimelinePlaybackPhase> timelines;
    std::shared_ptr<VisualProgram> program = std::make_shared<VisualProgram>();
    std::shared_ptr<VisualProgramPlayer> player;
    std::function<void()> on_start, on_attach;
    std::function<std::optional<MovementRequestEndpoint>(VisualEffectContext&, const VisualMovement&)> movement;
    std::function<std::optional<HitDelivery>(VisualEffectContext&, const std::string&, const HitInput&)> hit;
    std::function<std::optional<SpatialHitEndpoint>(const std::string&, const HitInput&)> point_hit;
    std::map<std::string, std::shared_ptr<AnimationRequestStatus>> animations;
    std::map<std::string, std::uint64_t> generations{{"body", 10}, {"partner", 20}};
    std::vector<std::pair<std::string, std::uint64_t>> released;
    std::vector<VisualActorTimeScale> clocks;
    std::vector<std::pair<bool, std::size_t>> preparations;
    bool partner_ready = false;
    VisualProgramParameters defaults;
    Fixture(std::function<VisualStepResult()> face = {}) {
        VisualProgramServices services;
        services.select_inputs = [this] { return defaults; };
        services.prepare_inputs = [this](auto, const auto&, bool, bool needs_origin) {
            preparations.emplace_back(needs_origin, animations.size());
        };
        services.effects = std::make_shared<api::PresentationEffects>(
            api::EffectRequestServices{{}, api::EffectAccess({}), {}, {}, {}});
        services.create = [](auto&, const auto&, const auto&) -> VisualEffectId {
            throw std::runtime_error("unexpected effect create");
        };
        services.attach = [&](auto) {
            ++attached;
            if (on_attach)
                on_attach();
        };
        services.detach = [&] { ++detached; };
        if (face)
            services.face = [face = std::move(face)](auto&, const auto&) { return face(); };
        services.actor_time_scale = [this](auto, const auto& command) {
            clocks.push_back(command);
            return VisualStepResult{VisualStepState::Done, {}};
        };
        services.movement = [&](auto& context, const auto& command) -> std::optional<MovementRequestEndpoint> {
            if (!movement)
                throw std::runtime_error("test movement unavailable");
            return movement(context, command);
        };
        services.hit = [&](auto& context, const auto& target, const auto& command) -> std::optional<HitDelivery> {
            if (!hit)
                throw std::runtime_error("test receiver unavailable");
            return hit(context, target, command);
        };
        services.point_hit = [&](auto, const auto& target, const auto& command) -> std::optional<SpatialHitEndpoint> {
            return point_hit ? point_hit(target, command) : std::nullopt;
        };
        ConnectVisualTimelines(services, std::make_shared<VisualTimelines>([&] {
                                   const int id = static_cast<int>(++starts);
                                   api::TimelineRequestServices native;
                                   native.start = [&, id](const auto&, int, double) {
                                       timelines[id] = api::TimelinePlaybackPhase::Playing;
                                       api::TimelineStartResult result;
                                       result.call.returned = true;
                                       result.playback = id;
                                       result.attributed = true;
                                       result.references.push_back({reinterpret_cast<void*>(1),
                                                                    reinterpret_cast<void*>(2), id,
                                                                    static_cast<std::uint64_t>(id)});
                                       if (on_start)
                                           on_start();
                                       return result;
                                   };
                                   native.observe = [&](auto ref) { return timelines.at(ref.playback); };
                                   native.stop = [&](auto ref) {
                                       ++stops;
                                       timelines[ref.playback] = api::TimelinePlaybackPhase::Stopped;
                                       return api::CallResult{true, 0};
                                   };
                                   return std::make_unique<api::OwnedTimeline>(std::move(native));
                               }));
        player = std::make_shared<VisualProgramPlayer>(program, std::move(services));
    }
    TaskPresentationServices Services() {
        TaskPresentationServices result;
        result.animation = [&](const auto& participant) -> std::optional<TaskAnimationEndpoint> {
            const auto name = participant.name;
            const auto found = generations.find(name);
            if (found == generations.end())
                throw std::runtime_error("unknown participant: " + name);
            if (name == "partner" && !partner_ready)
                return {};
            const auto generation = found->second;
            return TaskAnimationEndpoint{generation,
                                         [&, name, generation](const auto&) {
                                             auto value = std::make_shared<AnimationRequestStatus>();
                                             value->serial = animations.size() + 1;
                                             value->entity_generation = generation;
                                             animations[name] = value;
                                             return value;
                                         },
                                         [&, name](auto serial) {
                                             released.emplace_back(name, serial);
                                             animations.at(name)->released = true;
                                         }};
        };
        return result;
    }
    void Retire() {
        for (auto& [id, phase] : timelines)
            phase = api::TimelinePlaybackPhase::Retired;
        player->Service();
    }
    ~Fixture() {
        player->End();
        player->Service();
        Retire();
    }
};
void AnimationParameterParsing() {
    using Json = nlohmann::json;
    auto row = Json::parse(
        R"({"op":"animation_parameter","as":"setting","participant":"body","parameter":{"name":"Mode","type":"int","value":7},"propagate":true})");
    auto command = animation_parameter_codec::Parse(row);
    Check(command.participant.name == "body" && command.parameter.name == "Mode" &&
          std::get<int>(command.parameter.value) == 7 &&
          command.propagation == api::AnimationParameterPropagation::SyncPropState);
    row["parameter"] = {{"name", "Rate"}, {"type", "float"}, {"value", 1.25}};
    Check(std::get<float>(animation_parameter_codec::Parse(row).parameter.value) == 1.25f);
    row["parameter"] = {{"name", "Visible"}, {"type", "bool"}, {"value", false}};
    Check(!std::get<bool>(animation_parameter_codec::Parse(row).parameter.value));
    row["parameter"] = {{"name", "Start"}, {"type", "trigger"}};
    Check(!std::get<api::AnimationTrigger>(animation_parameter_codec::Parse(row).parameter.value).reset);
    row["parameter"]["type"] = "reset_trigger";
    row["parameter"]["value"] = true;
    Check(std::get<api::AnimationTrigger>(animation_parameter_codec::Parse(row).parameter.value).reset);
    const auto reject = [&](Json value) {
        bool rejected = false;
        try {
            animation_parameter_codec::Parse(value);
        } catch (const std::exception&) {
            rejected = true;
        }
        Check(rejected);
    };
    auto bad = row;
    bad["parameter"]["value"] = false;
    reject(bad);
    bad = row;
    bad["parameter"] = {{"name", "Mode"}, {"type", "int"}, {"value", 2.5}};
    reject(bad);
    bad["parameter"]["value"] = std::numeric_limits<std::uint64_t>::max();
    reject(bad);
    bad["parameter"]["value"] = std::numeric_limits<std::int64_t>::min();
    reject(bad);
    bad["parameter"] = {{"name", "Mode"}, {"type", "bool"}, {"value", 1}};
    reject(bad);
    bad["parameter"] = {{"name", "Rate"}, {"type", "float"}, {"value", std::numeric_limits<double>::infinity()}};
    reject(bad);
    bad = row;
    bad["propagate"] = "true";
    reject(bad);
    bad = row;
    bad["parameter"]["name"] = std::string("a\0b", 3);
    reject(bad);
    bad = row;
    bad["parameter"]["path"] = "reflection.path";
    reject(bad);
    bad = row;
    bad["participant"] = "";
    reject(bad);
    const auto catalog = ParseLegacyTaskCatalog(
        Json{{"format", "hksr.task-catalog"},
             {"id", "parameters"},
             {"domain", "battle"},
             {"visual", {{"id", "scene"}, {"source", "fixture"}, {"scope", "owned"}, {"effects", Json::object()}}},
             {"sequences", {{"apply", {{"entry", row}}}}},
             {"tasks", {{"apply", {{"entry", "apply"}, {"mode", "input"}}}}}}
            .dump());
    Check(catalog.tasks.at("apply").definition.mode == TaskExecutionMode::Input);
    const auto& submit = std::get<TaskSubmit>(catalog.sequences.at("apply")->nodes.at(0).operation);
    Check(std::holds_alternative<TaskAnimationParameterCommand>(submit.operation) &&
          !RequiresBodyPlayback(submit.operation));
}
api::AnimationParameterResult AppliedParameter() {
    api::AnimationParameterResult result;
    result.state = api::AnimationParameterState::Applied;
    result.body_attempted = result.body.returned = result.owner_current = true;
    result.model_identity = 101;
    return result;
}
void AnimationParameters() {
    const TaskAnimationParameterCommand command{
        {"body"}, {"Mode", 7}, api::AnimationParameterPropagation::SyncPropState};
    auto plan = Plan({{"set", TaskSubmit{"set", command}},
                      {"wait", TaskWaitRequest{"set", TaskWaitSignal::RequestComplete}},
                      {"hold", TaskWaitSeconds{10}},
                      {"entry", TaskSerial{{0, 1, 2}}}},
                     3);
    Fixture f;
    auto ports = f.Services();
    bool ready = false;
    unsigned written = 0;
    auto body_plan = Plan({{"body", TaskSubmit{"body", TaskAnimationCommand{{"body"}, "Existing"}}},
                           {"wait", TaskWaitRequest{"body"}},
                           {"entry", TaskSerial{{0, 1}}}},
                          2);
    TaskPresentation body({119}, 10, body_plan, f.player, {}, f.Services());
    body.Service(0, 1);
    const auto existing = f.animations.at("body");
    existing->accepted = existing->entered = true;
    ports.parameter =
        [&](const TaskParticipant& participant, const api::AnimationParameter& parameter,
            api::AnimationParameterPropagation propagation) -> std::optional<api::AnimationParameterResult> {
        Check(participant.name == "body" && parameter.name == "Mode" && std::get<int>(parameter.value) == 7 &&
              propagation == api::AnimationParameterPropagation::SyncPropState);
        if (!ready)
            return {};
        ++written;
        return AppliedParameter();
    };
    TaskPresentation run({120}, 10, plan, f.player, {}, ports, false, 0, false);
    run.Service(0, 1);
    Check(written == 0 && !run.Drained() && f.animations.size() == 1 && !existing->released);
    ready = true;
    run.Service(0, 2);
    run.Service(.1, 3);
    run.Service(.1, 4);
    Check(written == 1 && run.Status().issues.empty() && f.animations.size() == 1 && !existing->released &&
          f.released.empty() && !run.Started());
    const auto seen = run.Requests();
    Check(seen.size() == 1 && seen[0].receipt.model == 101 && seen[0].state.submitted &&
          seen[0].state.request_complete && seen[0].state.performance_complete && !seen[0].state.progress &&
          !seen[0].state.animation_end);
    run.Stop(TaskExitReason::Deleted);
    run.Service(0, 5);
    Check(run.Drained() && written == 1 && !existing->released);
    body.Stop(TaskExitReason::Deleted);
    body.Service(0, 6);
    Check(body.Drained());
    // Cancellation before model readiness never submits a later setter.
    Fixture waiting;
    ready = false;
    TaskPresentation cancelled({121}, 10, plan, waiting.player, {}, ports, false, 0, false);
    cancelled.Service(0, 1);
    cancelled.Stop(TaskExitReason::Deleted);
    ready = true;
    cancelled.Service(0, 2);
    Check(cancelled.Drained() && written == 1);
    // A successful body write with failed propagation is not retried/undone.
    Fixture partial;
    unsigned attempts = 0;
    int body_value = 0;
    auto partial_ports = partial.Services();
    partial_ports.parameter = [&](const auto&, const auto&, auto) -> std::optional<api::AnimationParameterResult> {
        ++attempts;
        body_value = 7;
        auto result = AppliedParameter();
        result.state = api::AnimationParameterState::Partial;
        result.effect_failures = 1;
        result.detail = "effect parameter write failed";
        return result;
    };
    TaskPresentation failed({122}, 10, plan, partial.player, {}, partial_ports, false, 0, false);
    failed.Service(0, 1);
    failed.Service(0, 2);
    failed.Service(0, 3);
    Check(failed.Drained() && failed.Status().reason == TaskExitReason::Failed && attempts == 1 && body_value == 7 &&
          !failed.Status().issues.empty() &&
          failed.Status().issues.front().find("effect parameter write failed") != std::string::npos);
    // A returned unavailable result is terminal, unlike null readiness.
    Fixture unavailable;
    auto unavailable_ports = unavailable.Services();
    attempts = 0;
    unavailable_ports.parameter = [&](const auto&, const auto&, auto) -> std::optional<api::AnimationParameterResult> {
        ++attempts;
        api::AnimationParameterResult result;
        result.detail = "binding unavailable";
        return result;
    };
    TaskPresentation missing({123}, 10, plan, unavailable.player, {}, unavailable_ports, false, 0, false);
    missing.Service(0, 1);
    missing.Service(0, 2);
    Check(missing.Drained() && attempts == 1 && missing.Status().reason == TaskExitReason::Failed);
    // Stop during a synchronous setter does not replay it or invent rollback.
    Fixture reentry;
    auto reentry_ports = reentry.Services();
    TaskPresentation* active = nullptr;
    attempts = 0;
    reentry_ports.parameter = [&](const auto&, const auto&, auto) -> std::optional<api::AnimationParameterResult> {
        ++attempts;
        active->Stop(TaskExitReason::Deleted);
        active->Service(0, 1);
        return AppliedParameter();
    };
    TaskPresentation stopped({124}, 10, plan, reentry.player, {}, reentry_ports, false, 0, false);
    active = &stopped;
    stopped.Service(0, 1);
    stopped.Service(0, 2);
    Check(stopped.Drained() && attempts == 1 && stopped.Status().reason == TaskExitReason::Deleted);
}
void CurrentIdleSelection() {
    Fixture f;
    auto ports = f.Services();
    bool ready = false;
    std::vector<std::string> bodies, faces;
    std::uint64_t serial = 0;
    auto receipt = [&] {
        auto value = std::make_shared<AnimationRequestStatus>();
        value->serial = ++serial;
        value->entity_generation = 10;
        value->accepted = value->entered = value->complete = true;
        return value;
    };
    ports.animation = [&](const auto&) -> std::optional<TaskAnimationEndpoint> {
        return TaskAnimationEndpoint{10,
                                     [&](const auto& command) {
                                         bodies.push_back(command.state);
                                         return receipt();
                                     },
                                     [](auto) {},
                                     [&](const auto& state, float) {
                                         faces.push_back(state);
                                         return receipt();
                                     },
                                     [](auto) {}};
    };
    ports.current_idle = [&]() -> std::optional<CurrentIdleResult> {
        if (!ready)
            return {};
        return CurrentIdleResult{"Stun_Loop", "Emo_Stun", .15f, {}};
    };
    TaskAnimationCommand body{{"body"}, "StandBy"};
    body.current_idle = true;
    auto plan = Plan({{"face", TaskSubmit{"face", TaskExpressionCommand{"Emo_Default", .2f, true}}},
                      {"body", TaskSubmit{"body", body}},
                      {"entry", TaskSerial{{0, 1}}}},
                     2);
    TaskPresentation run({991}, 10, plan, f.player, {}, ports);
    run.Service(0, 1);
    Check(bodies.empty() && faces.empty());
    ready = true;
    for (std::uint64_t frame = 2; frame < 8; ++frame)
        run.Service(.1, frame);
    Check(bodies == std::vector<std::string>{"Stun_Loop"} && faces == std::vector<std::string>{"Emo_Stun"});
    Check(run.Status().issues.empty());
    run.Stop(TaskExitReason::Deleted);
    run.Service(0, 9);
    // No active owner means the authored, source-derived fallback is retained.
    ports.current_idle = []() -> std::optional<CurrentIdleResult> { return CurrentIdleResult{}; };
    TaskPresentation plain({992}, 10, plan, f.player, {}, ports);
    for (std::uint64_t frame = 10; frame < 18; ++frame)
        plain.Service(.1, frame);
    Check(bodies.back() == "StandBy" && faces.back() == "Emo_Default");
    plain.Stop(TaskExitReason::Deleted);
    plain.Service(0, 18);
    // A terminal state delegates to the existing complete reaction pose. The
    // pose owns its expression; accepting it must not wait for a loop/end.
    const auto count = bodies.size(), face_count = faces.size();
    unsigned poses = 0, releases = 0;
    ports.current_idle = []() -> std::optional<CurrentIdleResult> {
        return CurrentIdleResult{{}, {}, 0, ReactionSelection{"fixture", 115, false, DeathOccurrence::Fly}};
    };
    unsigned idle_effects = 0;
    ports.accepted_playback = [&](const auto&) { ++idle_effects; };
    ports.pose = PoseRequestEndpoint{10,
                                     [&](const PoseSelection& value) {
                                         const auto& selection = std::get<ReactionSelection>(value);
                                         Check(selection.source == "fixture" && selection.flag == 115 &&
                                               !selection.recover && selection.occurrence == DeathOccurrence::Fly);
                                         ++poses;
                                         auto status = receipt();
                                         status->complete = false;
                                         status->accepted_effect =
                                             ReactionEntered{"fixture", 115, DeathOccurrence::Fly};
                                         return status;
                                     },
                                     [&](auto, bool cancel) {
                                         Check(!cancel);
                                         ++releases;
                                     }};
    TaskPresentation terminal({993}, 10, plan, f.player, {}, ports);
    for (std::uint64_t frame = 19; frame < 25; ++frame)
        terminal.Service(.1, frame);
    Check(terminal.Status().issues.empty() && bodies.size() == count && faces.size() == face_count && poses == 1 &&
          releases == 1 && terminal.Drained() && idle_effects == 0);
    terminal.Stop(TaskExitReason::Deleted);
    terminal.Service(0, 25);
}
void SkippedLifetimeScope() {
    for (const bool delayed : {false, true}) {
        Fixture f;
        auto ports = f.Services();
        unsigned submissions = 0;
        auto receipt = std::make_shared<TaskLifetimeReceipt>();
        const auto skip = [&] {
            receipt->state.skipped = true;
            receipt->state.skip_reason = "state version was superseded";
            receipt->state.drained = true;
        };
        if (!delayed)
            skip();
        ports.lifetime = [&](const TaskLifetimeCommand& command, const auto&) {
            Check(command.context == "state" && command.operation == TaskLifetimeOperation::ChangeValue);
            ++submissions;
            return receipt;
        };
        TaskLifetimeCommand write{"state", "", TaskLifetimeOperation::ChangeValue};
        write.set_value = TaskContextValueSet{"mode", true};
        auto plan = Plan({{"write", TaskSubmit{"write", write}},
                          {"accepted-or-skipped", TaskWaitRequest{"write", TaskWaitSignal::RequestComplete}},
                          {"entry", TaskSerial{{0, 1}}}},
                         2);
        TaskPresentation run({static_cast<std::uint64_t>(delayed ? 126 : 125)}, 10, plan, f.player, {}, ports, false, 0,
                             false);
        run.Service(0, 1);
        if (delayed) {
            Check(!run.Drained());
            skip();
        }
        for (unsigned frame = 2; frame < 7; ++frame)
            run.Service(0, frame);
        Check(run.Drained() && run.Status().reason == TaskExitReason::Completed && run.Status().issues.empty() &&
              submissions == 1 && !run.Started());
        Check(receipt->state.skipped && receipt->state.drained && !receipt->state.submitted &&
              !receipt->state.request_complete && !receipt->state.performance_complete && !receipt->state.started &&
              !receipt->state.progress);
        for (const auto& request : run.Requests())
            Check(request.state.skipped && request.state.drained && !request.state.submitted &&
                  !request.state.request_complete && !request.state.started && !request.state.progress);
    }
}
void SkippedVisualScope() {
    unsigned attempts = 0;
    Fixture f([&] {
        ++attempts;
        return VisualStepResult{VisualStepState::Skipped, "owned follow has an unsupported group"};
    });
    auto plan = Plan({Visual("optional-face", VisualFace{"Target"}),
                      {"skip-boundary", TaskWaitRequest{"optional-face", TaskWaitSignal::RequestComplete}},
                      {"body", TaskSubmit{"body", TaskAnimationCommand{{"body"}, "Hold"}}},
                      {"keep-body", TaskWaitRequest{"body"}},
                      {"entry", TaskSerial{{0, 1, 2, 3}}}},
                     4);
    TaskPresentation run({127}, 10, plan, f.player, {}, f.Services());
    run.Service(0, 1);
    run.Service(0, 2);
    run.Service(0, 3);
    Check(attempts == 1 && f.animations.count("body") && run.Status().issues.empty());
    Check(run.Status().skipped.size() == 1 && run.Status().skipped[0].request == "optional-face" &&
          run.Status().skipped[0].reason == "owned follow has an unsupported group");
    const auto requests = run.Requests();
    const auto skipped =
        std::find_if(requests.begin(), requests.end(), [](const auto& row) { return row.name == "optional-face"; });
    Check(skipped != requests.end() && skipped->state.skipped && skipped->state.drained && !skipped->state.submitted &&
          !skipped->state.request_complete && !skipped->state.performance_complete && !skipped->state.started &&
          !skipped->state.progress);
    f.animations.at("body")->accepted = f.animations.at("body")->complete = true;
    for (unsigned frame = 4; frame < 9; ++frame)
        run.Service(0, frame);
    Check(run.Drained() && run.Status().reason == TaskExitReason::Completed && run.Status().issues.empty());
}
void Scopes() {
    Fixture f;
    auto plan = Plan({Visual("scene", VisualTimeline{"scene", "scene.playable"})});
    TaskPresentation a({1}, 10, plan, f.player, {}, f.Services());
    a.Service(0, 1);
    a.Service(0, 2);
    Check(f.starts == 1 && f.stops == 0 && !a.Drained()); // bare submission still executes; natural tail remains
    TaskPresentation b({2}, 10, plan, f.player, {}, f.Services());
    b.Service(0, 3);
    b.Service(0, 4);
    Check(f.starts == 2 && f.timelines.at(2) == api::TimelinePlaybackPhase::Playing);
    a.Stop(TaskExitReason::Deleted);
    a.Service(0, 5);
    Check(f.timelines.at(1) == api::TimelinePlaybackPhase::Stopped &&
          f.timelines.at(2) == api::TimelinePlaybackPhase::Playing);
    f.timelines[1] = api::TimelinePlaybackPhase::Retired;
    a.Service(0, 6);
    Check(a.Drained() && !b.Drained()); // another scope cannot delay this drain
    b.Stop(TaskExitReason::Deleted);
    b.Service(0, 7);
    f.Retire();
    b.Service(0, 8);
    Check(b.Drained());
    // A late close of the old scope must not detach the new event producer.
    const auto old = f.player->BeginSequence({});
    f.player->Service();
    const auto current = f.player->BeginSequence({});
    f.player->Service();
    const auto detach = f.detached;
    f.player->EndSequence(old, false);
    f.player->Service();
    Check(f.detached == detach && f.player->SequenceDrained(old));
    f.player->ForgetSequence(old);
    f.player->EndSequence(current, false);
    f.player->Service();
    Check(f.detached == detach + 1);
    f.player->ForgetSequence(current);
}
void IndependentParticipants() {
    Fixture f;
    auto plan = Plan({{"body", TaskSubmit{"body", TaskAnimationCommand{{"body"}, "Body"}}},
                      {"partner", TaskSubmit{"partner", TaskAnimationCommand{{"partner"}, "Partner"}}},
                      {"wait-body", TaskWaitRequest{"body"}},
                      {"wait-partner", TaskWaitRequest{"partner"}},
                      {"wait-both", TaskParallel{{2, 3}}},
                      {"entry", TaskSerial{{0, 1, 4}}}},
                     5);
    TaskPresentation run({3}, 10, plan, f.player, {}, f.Services());
    run.Service(0, 1);
    Check(f.animations.size() == 1 && !f.animations.at("body")->accepted && run.Status().issues.empty());
    auto body = f.animations.at("body");
    body->accepted = true;
    body->model_generation = 101;
    body->progress = 1;
    body->complete = true;
    run.Service(0.1, 2);
    Check(!run.Drained());
    f.partner_ready = true;
    run.Service(0.1, 3);
    Check(f.animations.size() == 2 && !run.Drained());
    auto partner = f.animations.at("partner");
    partner->accepted = true;
    partner->model_generation = 202;
    partner->progress = 0.2;
    run.Service(0.1, 4);
    Check(!run.Drained());
    partner->complete = true;
    partner->progress = 1;
    run.Service(0.1, 5);
    run.Service(0, 6);
    Check(run.Drained() && run.Status().issues.empty() && f.released.size() == 2);
    // A cancelled unresolved participant is never resolved/submitted later.
    Fixture waiting;
    TaskPresentation cancelled({4}, 10, plan, waiting.player, {}, waiting.Services());
    cancelled.Service(0, 1);
    cancelled.Stop(TaskExitReason::Deleted);
    waiting.partner_ready = true;
    cancelled.Service(0, 2);
    Check(cancelled.Drained() && waiting.animations.size() == 1);
}
void ConfiguredPoses() {
    Fixture f;
    auto ports = f.Services();
    bool prepared = false;
    std::shared_ptr<AnimationRequestStatus> pose;
    unsigned submitted = 0;
    bool cancelled = false;
    unsigned accepted_effects = 0;
    ports.accepted_playback = [&](const auto& effect) {
        Check(std::holds_alternative<NormalBodyAccepted>(effect));
        ++accepted_effects;
    };
    ports.pose = PoseRequestEndpoint{10,
                                     [&](const auto& key) -> std::shared_ptr<const AnimationRequestStatus> {
                                         Check(std::get<ReadyPoseSelection>(key).skill == 8);
                                         if (!prepared)
                                             return {};
                                         ++submitted;
                                         pose = std::make_shared<AnimationRequestStatus>();
                                         pose->serial = 41;
                                         pose->entity_generation = 10;
                                         return pose;
                                     },
                                     [&](auto serial, bool cancel) {
                                         Check(serial == 41);
                                         cancelled = cancel;
                                         pose->released = true;
                                     }};
    auto plan = Plan({{"pose", TaskSubmit{"pose", TaskPoseCommand{ReadyPoseSelection{"source", 8, -1}}}},
                      {"wait", TaskWaitRequest{"pose"}},
                      {"entry", TaskSerial{{0, 1}}}},
                     2);
    TaskPresentation run({91}, 10, plan, f.player, {}, ports);
    run.Service(0, 1);
    Check(!submitted && !run.Drained() && !accepted_effects);
    prepared = true;
    run.Service(0, 2);
    pose->accepted = true;
    pose->accepted_effect = NormalBodyAccepted{};
    run.Service(0, 3);
    Check(!run.Drained() && accepted_effects == 1);
    pose->entered = true;
    run.Service(0, 4);
    Check(!run.Drained());
    pose->complete = true;
    run.Service(0, 5);
    run.Service(0, 6);
    Check(run.Drained() && run.Status().issues.empty() && submitted == 1 && !cancelled && accepted_effects == 1);
    TaskPresentation next({92}, 10, plan, f.player, {}, ports);
    next.Service(0, 7);
    pose->accepted = true;
    pose->accepted_effect = NormalBodyAccepted{};
    next.Stop(TaskExitReason::Deleted);
    next.Service(0, 8);
    Check(next.Drained() && cancelled && accepted_effects == 1);
    auto bad = ports;
    bad.pose->entity_generation = 999;
    TaskPresentation foreign({93}, 10, plan, f.player, {}, bad);
    foreign.Service(0, 9);
    foreign.Service(0, 10);
    Check(foreign.Drained() && foreign.Status().reason == TaskExitReason::Failed && submitted == 2);
}
void ErrorsAndReentry() {
    Fixture f;
    auto plan = Plan({Visual("scene", VisualTimeline{"scene", "scene.playable"}),
                      {"keep", TaskWaitSeconds{10}},
                      {"entry", TaskSerial{{0, 1}}}},
                     2);
    TaskPresentation run({5}, 10, plan, f.player, {}, f.Services());
    f.on_start = [&] {
        run.Stop(TaskExitReason::Deleted);
        run.Service(0, 1);
    };
    run.Service(0, 1);
    f.on_start = {};
    run.Service(0, 2);
    Check(f.starts == 1 && f.stops == 1 && !run.Drained());
    f.Retire();
    run.Service(0, 3);
    Check(run.Drained());
    Fixture bad;
    auto bad_plan = Plan({Visual("missing-variable", VisualProgress{"missing", std::string("undefined"), 0}),
                          {"wait", TaskWaitRequest{"missing-variable"}},
                          {"entry", TaskSerial{{0, 1}}}},
                         2);
    TaskPresentation failure({6}, 10, bad_plan, bad.player, {}, bad.Services());
    failure.Service(0, 1);
    failure.Service(0, 2);
    Check(failure.Status().reason == TaskExitReason::Failed && !failure.Status().issues.empty() && failure.Drained());
    // Setup reentry cancels queued instructions before they reach native calls.
    Fixture setup;
    TaskPresentation removed({7}, 10, plan, setup.player, {}, setup.Services());
    setup.on_attach = [&] { removed.Stop(TaskExitReason::Deleted); };
    removed.Service(0, 1);
    setup.on_attach = {};
    removed.Service(0, 2);
    Check(setup.starts == 0 && removed.Drained());
    Fixture wrong;
    auto unknown = Plan({{"unknown", TaskSubmit{"unknown", TaskAnimationCommand{{"official-player"}, "Attack"}}}});
    TaskPresentation missing({8}, 10, unknown, wrong.player, {}, wrong.Services());
    missing.Service(0, 1);
    missing.Service(0, 2);
    Check(missing.Status().reason == TaskExitReason::Failed && wrong.animations.empty() && missing.Drained());
}
void NamedContexts() {
    Fixture f;
    f.program->contexts["phase"].enter = {{VisualTimeline{"phase-scene", "scene.playable"}, "phase.enter"}};
    auto enter = Plan({Visual("phase", VisualContextOperation{"phase", true})});
    TaskPresentation first({9}, 10, enter, f.player, {}, f.Services());
    first.Service(0, 1);
    first.Service(0, 2);
    Check(first.Drained() && f.starts == 1 && f.player->ContextToken("phase"));
    auto exit = Plan({Visual("phase-exit", VisualContextOperation{"phase", false})});
    TaskPresentation second({10}, 10, exit, f.player, {}, f.Services());
    second.Service(0, 3);
    second.Service(0, 4);
    Check(second.Drained() && !f.player->ContextToken("phase") && f.stops == 1);
    f.Retire();
    f.player->End();
    f.player->Service();
    Check(f.player->Drained());
}
void IndependentTail() {
    Fixture f;
    f.program->contexts["phase"].enter = {{VisualTimeline{"phase-scene", "scene.playable"}, "phase.enter"}};
    auto enter = Plan({Visual("phase", VisualContextOperation{"phase", true})});
    TaskPresentation start({11}, 10, enter, f.player, {}, f.Services());
    start.Service(0, 1);
    start.Service(0, 2);
    auto end = Plan({Visual("tail", VisualTimeline{"tail", "tail.playable"}),
                     {"tail-accepted", TaskWaitRequest{"tail", TaskWaitSignal::RequestComplete}},
                     Visual("exit", VisualContextOperation{"phase", false}),
                     {"exit-accepted", TaskWaitRequest{"exit", TaskWaitSignal::RequestComplete}},
                     {"entry", TaskSerial{{0, 1, 2, 3}}}},
                    4);
    TaskPresentation finish({12}, 10, end, f.player, {}, f.Services());
    for (unsigned frame = 3; frame < 8; ++frame)
        finish.Service(0, frame);
    Check(f.stops == 1 && f.timelines.at(1) == api::TimelinePlaybackPhase::Stopped &&
          f.timelines.at(2) == api::TimelinePlaybackPhase::Playing && !finish.Drained());
    f.timelines[1] = api::TimelinePlaybackPhase::Retired;
    f.timelines[2] = api::TimelinePlaybackPhase::Retired;
    finish.Service(0, 8);
    Check(finish.Drained() && finish.Status().issues.empty());
}
void Sounds() {
    Fixture f;
    auto ports = f.Services();
    unsigned stops = 0;
    auto status = std::make_shared<api::SoundRequestStatus>();
    ports.sound = [&](const TaskSoundCommand& command) -> std::optional<TaskSoundEndpoint> {
        Check(command.participant.name == "body" && command.event == "event");
        status->accepted = true;
        return TaskSoundEndpoint{10, [] {},
                                 [&] {
                                     ++stops;
                                     status->cancelled = true;
                                 },
                                 [status] { return *status; }};
    };
    const auto sound = TaskNode{"sound", TaskSubmit{"sound", TaskSoundCommand{{"body"}, "event"}}};
    auto plan = Plan({sound,
                      {"wait-start", TaskWaitRequest{"sound", TaskWaitSignal::Started}},
                      {"body", TaskSubmit{"body", TaskAnimationCommand{{"body"}, "Body"}}},
                      {"wait-end", TaskWaitRequest{"sound"}},
                      {"entry", TaskSerial{{0, 1, 2, 3}}}},
                     4);
    TaskPresentation run({20}, 10, plan, f.player, {}, ports);
    run.Service(0, 1);
    Check(f.animations.empty() && !run.Drained()); // queued isn't started
    status->started = true;
    run.Service(0, 2);
    Check(f.animations.size() == 1 && !run.Drained());
    auto body = f.animations.at("body");
    body->accepted = true;
    body->complete = true;
    status->ended = true;
    status->closed = true;
    run.Service(0, 3);
    run.Service(0, 4);
    Check(run.Drained() && stops == 0 && run.Status().issues.empty());

    // Natural finish retains sound tails; a new execution cannot own/stop them.
    auto tail_status = std::make_shared<api::SoundRequestStatus>();
    tail_status->accepted = true;
    ports.sound = [tail_status, &stops](const auto&) -> std::optional<TaskSoundEndpoint> {
        return TaskSoundEndpoint{10, [] {},
                                 [tail_status, &stops] {
                                     ++stops;
                                     tail_status->cancelled = true;
                                 },
                                 [tail_status] { return *tail_status; }};
    };
    TaskPresentation tail({21}, 10, Plan({sound}), f.player, {}, ports);
    tail.Service(0, 5);
    tail.Service(0, 6);
    Check(!tail.Drained() && stops == 0);
    tail.Stop(TaskExitReason::Replaced);
    tail.Service(0, 7);
    Check(stops == 1 && !tail.Drained()); // stop request is not backend drain
    tail_status->closed = true;
    tail.Service(0, 8);
    Check(tail.Drained());

    // Cancellation inside the submitting factory retains the returned request.
    auto pending = std::make_shared<api::SoundRequestStatus>();
    pending->accepted = true;
    TaskPresentation* active = nullptr;
    ports.sound = [&](const auto&) -> std::optional<TaskSoundEndpoint> {
        active->Stop(TaskExitReason::Deleted);
        return TaskSoundEndpoint{10, [] {},
                                 [&] {
                                     ++stops;
                                     pending->cancelled = true;
                                 },
                                 [pending] { return *pending; }};
    };
    TaskPresentation reentrant({22}, 10, Plan({sound}), f.player, {}, ports);
    active = &reentrant;
    reentrant.Service(0, 9);
    Check(pending->cancelled && !reentrant.Drained());
    pending->closed = true;
    reentrant.Service(0, 10);
    Check(reentrant.Drained());

    // Missing backend and playback failure remain errors, not completed sound.
    TaskPresentation unavailable({23}, 10, Plan({sound}), f.player, {}, f.Services());
    unavailable.Service(0, 11);
    unavailable.Service(0, 12);
    Check(unavailable.Drained() && unavailable.Status().reason == TaskExitReason::Failed);
    auto failed = std::make_shared<api::SoundRequestStatus>();
    failed->error = "bank failed";
    failed->closed = true;
    ports.sound = [failed](const auto&) -> std::optional<TaskSoundEndpoint> {
        return TaskSoundEndpoint{10, [] {}, [] {}, [failed] { return *failed; }};
    };
    TaskPresentation failure({24}, 10, Plan({sound}), f.player, {}, ports);
    failure.Service(0, 13);
    failure.Service(0, 14);
    Check(failure.Drained() && failure.Status().reason == TaskExitReason::Failed);
}
void Movements() {
    VisualMovement movement;
    movement.segments.push_back({});
    const TaskNode body{"body", TaskSubmit{"body", TaskAnimationCommand{{"body"}, "Attack"}}};
    const TaskNode moving{"movement", TaskSubmit{"move", TaskMovementCommand{"body", movement}}};
    auto plan = Plan({body, moving, {"wait", TaskWaitRequest{"move"}}, {"entry", TaskSerial{{0, 1, 2}}}}, 3);
    Fixture f;
    unsigned starts = 0, stops = 0;
    auto status = std::make_shared<api::MovementRequestStatus>();
    status->accepted = true;
    f.movement = [&](auto&, const auto& command) -> std::optional<MovementRequestEndpoint> {
        Check(command.state == "Attack");
        ++starts;
        return MovementRequestEndpoint{10, [] {},
                                       [&] {
                                           ++stops;
                                           status->cancelled = true;
                                       },
                                       [status] { return *status; }};
    };
    TaskPresentation run({30}, 10, plan, f.player, {}, f.Services());
    run.Service(0, 1);
    Check(starts == 0 && f.animations.size() == 1);
    Check(f.preparations.size() == 1 && f.preparations[0].first && f.preparations[0].second == 0);
    auto animation = f.animations.at("body");
    animation->accepted = true;
    run.Service(0, 2);
    Check(starts == 0); // not the old same-name state
    animation->entered = true;
    animation->model_generation = 201;
    run.Service(0, 3);
    Check(starts == 1 && !run.Drained());
    status->started = true;
    status->movement_time = 0.5f;
    run.Service(0, 4);
    bool progress = false;
    for (const auto& request : run.Requests())
        if (request.receipt.participant.name == "body" && request.state.progress == 0.5)
            progress = true;
    Check(progress);
    status->completed = true;
    status->closed = true;
    run.Service(0, 5);
    run.Service(0, 6);
    Check(run.Drained() && stops == 0 && run.Status().issues.empty());

    // A user placement wins over both the active graph and this execution's
    // later return-to-origin. The body is not interrupted by motion withdrawal.
    Fixture repositioned;
    MovementPlacementIntent intent;
    const auto captured = intent.Epoch();
    auto motion = std::make_shared<api::MovementRequestStatus>();
    motion->accepted = motion->started = true;
    unsigned graphs = 0;
    repositioned.movement = [&](auto&, const auto&) -> std::optional<MovementRequestEndpoint> {
        return intent.Bind(captured, 10, [&] {
            ++graphs;
            return MovementRequestEndpoint{10,
                                           [motion] {
                                               if (motion->cancelled)
                                                   motion->closed = true;
                                           },
                                           [motion] { motion->cancelled = true; }, [motion] { return *motion; }};
        });
    };
    VisualMovement restore;
    restore.instant = true;
    restore.segments.push_back({});
    auto place_plan = Plan({body,
                            moving,
                            {"wait", TaskWaitRequest{"move"}},
                            {"restore", TaskSubmit{"restore", TaskMovementCommand{"", restore}}},
                            {"entry", TaskSerial{{0, 1, 2, 3}}}},
                           4);
    TaskPresentation placed({36}, 10, place_plan, repositioned.player, {}, repositioned.Services());
    placed.Service(0, 1);
    repositioned.animations.at("body")->accepted = repositioned.animations.at("body")->entered = true;
    placed.Service(0, 2);
    Check(graphs == 1);
    intent.Reposition();
    placed.Service(0, 3);
    placed.Service(0, 4);
    Check(graphs == 1 && motion->cancelled && placed.Drained() && placed.Status().issues.empty());

    // Bare leaves must still be delivered; finishing the sequence cannot release
    // the animation observation before the current playback actually enters.
    Fixture tail;
    auto tail_status = std::make_shared<api::MovementRequestStatus>();
    tail_status->accepted = true;
    tail.movement = [&](auto&, const auto&) -> std::optional<MovementRequestEndpoint> {
        ++starts;
        return MovementRequestEndpoint{10, [] {},
                                       [&] {
                                           ++stops;
                                           tail_status->cancelled = true;
                                       },
                                       [tail_status] { return *tail_status; }};
    };
    TaskPresentation finishing({31}, 10, Plan({body, moving, {"entry", TaskSerial{{0, 1}}}}, 2), tail.player, {},
                               tail.Services());
    finishing.Service(0, 1);
    tail.animations.at("body")->accepted = true;
    finishing.Service(0, 2);
    Check(starts == 1 && !tail.animations.at("body")->released);
    tail.animations.at("body")->entered = true;
    finishing.Service(0, 3);
    Check(starts == 2 && !finishing.Drained());
    finishing.Stop(TaskExitReason::WorldLeft);
    finishing.Service(0, 4);
    Check(stops == 1 && !finishing.Drained());
    tail_status->closed = true;
    finishing.Service(0, 5);
    Check(finishing.Drained());

    // A same-execution animation change first detaches its old motion request.
    Fixture change;
    auto old = std::make_shared<api::MovementRequestStatus>();
    old->accepted = true;
    change.movement = [old](auto&, const auto&) -> std::optional<MovementRequestEndpoint> {
        return MovementRequestEndpoint{10, [] {}, [old] { old->cancelled = true; }, [old] { return *old; }};
    };
    auto switching = Plan({body,
                           moving,
                           {"accept", TaskWaitRequest{"move", TaskWaitSignal::RequestComplete}},
                           {"return", TaskSubmit{"return", TaskAnimationCommand{{"body"}, "StandBy"}}},
                           {"entry", TaskSerial{{0, 1, 2, 3}}}},
                          4);
    TaskPresentation switcher({32}, 10, switching, change.player, {}, change.Services());
    switcher.Service(0, 1);
    const auto initial = change.animations.at("body");
    initial->accepted = true;
    initial->entered = true;
    switcher.Service(0, 2);
    switcher.Service(0, 3);
    Check(old->cancelled && change.animations.at("body") == initial && !initial->released);
    old->closed = true;
    switcher.Service(0, 4);
    Check(change.animations.at("body") != initial && initial->released);
    change.animations.at("body")->accepted = true;
    switcher.Service(0, 5);
    switcher.Service(0, 6);
    Check(switcher.Drained());

    Fixture nested;
    TaskPresentation* active = nullptr;
    auto pending = std::make_shared<api::MovementRequestStatus>();
    pending->accepted = true;
    nested.movement = [&](auto&, const auto&) -> std::optional<MovementRequestEndpoint> {
        active->Stop(TaskExitReason::Deleted);
        return MovementRequestEndpoint{10, [] {}, [pending] { pending->cancelled = true; },
                                       [pending] { return *pending; }};
    };
    TaskPresentation reentrant({33}, 10, plan, nested.player, {}, nested.Services());
    active = &reentrant;
    reentrant.Service(0, 1);
    nested.animations.at("body")->accepted = true;
    nested.animations.at("body")->entered = true;
    reentrant.Service(0, 2);
    if (!pending->cancelled || reentrant.Drained()) {
        std::string detail = "movement reentry: cancelled=" + std::to_string(pending->cancelled) +
                             " drained=" + std::to_string(reentrant.Drained());
        for (const auto& issue : reentrant.Status().issues)
            detail += "; " + issue;
        throw std::runtime_error(detail);
    }
    pending->closed = true;
    reentrant.Service(0, 3);
    if (!reentrant.Drained()) {
        std::string detail =
            "movement reentry finish phase=" + std::to_string(static_cast<int>(reentrant.Status().phase));
        for (const auto& request : reentrant.Requests())
            detail += "; " + request.name + " drained=" + std::to_string(request.state.drained) +
                      " gen=" + std::to_string(request.receipt.entity_generation) +
                      " model=" + std::to_string(request.receipt.model);
        for (const auto& issue : reentrant.Status().issues)
            detail += "; " + issue;
        throw std::runtime_error(detail);
    }

    // Named request identity is required, not a matching state name elsewhere.
    Fixture order;
    TaskPresentation missing({34}, 10, Plan({moving, body, {"entry", TaskSerial{{0, 1}}}}, 2), order.player, {},
                             order.Services());
    missing.Service(0, 1);
    missing.Service(0, 2);
    Check(missing.Drained() && missing.Status().reason == TaskExitReason::Failed);
}
void MovementInputs() {
    MovementPlacementIntent placement;
    const auto before = placement.Epoch();
    api::MovementRequestStatus owned;
    owned.accepted = owned.started = true;
    unsigned begins = 0, cancels = 0, services = 0;
    auto start = [&] {
        ++begins;
        return MovementRequestEndpoint{10,
                                       [&] {
                                           ++services;
                                           if (owned.cancelled)
                                               owned.closed = true;
                                       },
                                       [&] {
                                           ++cancels;
                                           owned.cancelled = true;
                                       },
                                       [&] { return owned; }};
    };
    auto active = placement.Bind(before, 10, start);
    placement.Reposition();
    Check(begins == 1 && cancels == 1 && services == 1 && active.superseded() && active.status().closed);
    auto old_return = placement.Bind(before, 10, start);
    Check(begins == 1 && old_return.superseded() && old_return.status().closed &&
          !old_return.status().attempted); // no old-origin write
    placement.Reposition();
    Check(cancels == 1); // no repeated native clear
    owned = {};
    owned.accepted = true;
    auto fresh = placement.Bind(placement.Epoch(), 10, start);
    Check(begins == 2 && !fresh.superseded());
    owned.error = "clear failed";
    placement.Reposition();
    Check(fresh.status().error == "clear failed"); // real failures stay visible

    VisualMovement instant;
    instant.instant = true;
    instant.segments.push_back({});
    const auto endpoint = ResolveVisualMovement(instant, {3, 4, 5}, {6, 7, 8}, {}).segments.front().target;
    Check(endpoint.x == 6 && endpoint.y == 4 && endpoint.z == 8);
    Fixture immediate;
    unsigned writes = 0;
    immediate.movement = [&](auto&, const VisualMovement& command) -> std::optional<MovementRequestEndpoint> {
        Check(command.instant && command.state.empty());
        ++writes;
        api::MovementRequestStatus result;
        result.accepted = result.started = result.completed = result.closed = true;
        return MovementRequestEndpoint{10, [] {}, [] {}, [result] { return result; }};
    };
    TaskPresentation direct({35}, 10, Plan({{"position", TaskSubmit{"position", TaskMovementCommand{"", instant}}}}, 0),
                            immediate.player, {}, immediate.Services());
    direct.Service(0, 1);
    direct.Service(0, 2);
    Check(writes == 1 && immediate.animations.empty() && direct.Drained() && direct.Status().issues.empty());
    VisualMovement command;
    command.state = "Attack";
    VisualMovementSegment attack;
    attack.destination = MovementDestination::Target;
    attack.name = "target";
    attack.stop_distance = 2;
    attack.end = 0.4f;
    VisualMovementSegment back;
    back.start = 0.8f;
    back.end = 1.2f;
    back.preserve_height = false;
    command.segments = {attack, back};
    int reads = 0;
    const auto resolve = [&](auto kind, const std::string& name) {
        Check(kind == MovementDestination::Target && name == "target");
        ++reads;
        return VisualSpatialAnchor{{10, 99, 0}, {}};
    };
    const auto plan = ResolveVisualMovement(command, {0, 2, 0}, {1, 3, 1}, resolve);
    Check(reads == 1 && plan.segments[0].target.x == 8 && plan.segments[0].target.y == 2 &&
          plan.segments[1].target.y == 3);
    Check(plan.segments[1].target.x == 1 && plan.segments[1].target.z == 1 && plan.segments[1].end > 1);
    command.segments[0].offset = {0, 1, 0};
    command.segments[0].stop_distance = 0;
    const auto offset = ResolveVisualMovement(command, {0, 2, 0}, {0, 2, 0}, resolve);
    Check(offset.segments[0].target.y == 3);
    command.clock = api::MovementClock::DistanceSpeed;
    command.segments[0].speed = 4;
    command.segments[1].speed = 2;
    Check(api::CompileAnimationMove(ResolveVisualMovement(command, {0, 2, 0}, {0, 2, 0}, resolve)).duration > 0);
    command.segments[0].stop_distance = 100;
    bool invalid = false;
    try {
        ResolveVisualMovement(command, {0, 2, 0}, {0, 2, 0}, resolve);
    } catch (const std::invalid_argument&) {
        invalid = true;
    }
    Check(invalid);
    const auto near = [](float a, float b) { return std::abs(a - b) < 1e-5f; };
    Check(near(ResolveVisualMovementSample({1, 3, {}}, {}, {{2, 0, 0}, {}}).x, -1));
    Check(near(ResolveVisualMovementSample({.5f, -2, {}}, {}, {{2, 0, 0}, {}}).x, 2));
    Check(near(ResolveVisualMovementSample({.5f, 2, {1, 0, 0}}, {}, {{2, 0, 0}, {}}).x, 2));
    Check(near(ResolveVisualMovementSample({1, 2, {}, true}, {}, {{2, 0, 0}, {0, .70710678f, 0, .70710678f}}).x, 0));
    Check(ResolveVisualMovementSample({1, 3, {}}, {1, 2, 3}, {{1, 2, 3}, {}}).x == 1);
    Check(ResolveVisualMovementSample({1, 3, {}}, {}, {{.000001f, 0, 0}, {}}).x == 0);
    VisualMovement geometry_move;
    geometry_move.state = "Skill";
    VisualMovementSegment incoming;
    incoming.destination = MovementDestination::Target;
    incoming.name = "Target";
    incoming.start = .1f;
    incoming.end = .3f;
    incoming.target_geometry = true;
    incoming.sample = VisualMovementSample{1, 1, {}, false, true};
    geometry_move.segments = {incoming, incoming};
    geometry_move.segments[1].start = .5f;
    geometry_move.segments[1].end = .8f;
    int geometry_reads = 0;
    const auto geometry_plan = ResolveVisualMovement(
        geometry_move, {0, 2, 0}, {0, 2, 0},
        [](auto, const auto&) { return VisualSpatialAnchor{{0, 20, 10}, {0, .70710678f, 0, .70710678f}}; },
        [&](const std::string& name) {
            ++geometry_reads;
            return name == "Caster" ? api::MovementGeometry{1, {}} : api::MovementGeometry{2, {0, 9, 1}};
        });
    Check(geometry_reads == 2 && geometry_plan.segments[0].target.y == 2);
    // Target local forward becomes world X, while the vertical offset is
    // removed by the source's preserve-height flag. Radii add to signed offset.
    const float fraction = (std::sqrt(101.f) - 4) / std::sqrt(101.f);
    Check(near(geometry_plan.segments[0].target.x, fraction) &&
          near(geometry_plan.segments[0].target.z, 10 * fraction));
    geometry_move.segments.resize(1);
    const auto point_plan = ResolveVisualMovement(
        geometry_move, {0, 2, 0}, {0, 2, 0}, [](auto, const auto&) { return VisualSpatialAnchor{{0, 2, 6}, {}}; },
        [](const std::string& name) { return api::MovementGeometry{name == "Caster" ? 1.f : 0.f, {}}; });
    Check(near(point_plan.segments[0].target.z, 4));
    command.clock = api::MovementClock::Animation;
    command.segments = {attack, back};
    command.segments[0].stop_distance = 0;
    command.segments[0].sample = VisualMovementSample{.5f, 2, {}};
    const auto sampled = ResolveVisualMovement(command, {0, 2, 0}, {1, 3, 1}, resolve);
    Check(near(sampled.segments[0].target.x, 4) && sampled.segments[0].target.y == 2);
    Check(sampled.segments[1].target.x == 1 && sampled.segments[1].target.y == 3);
    command.segments[0].stop_distance = 1;
    invalid = false;
    try {
        ValidateVisualMovement(command);
    } catch (const std::invalid_argument&) {
        invalid = true;
    }
    Check(invalid);
}
void AbilityTailScope() {
    Fixture f;
    auto plan = Plan({{"delay", TaskWaitSeconds{0.5}},
                      Visual("scene", VisualTimeline{"scene", "scene.playable"}),
                      {"child", TaskSerial{{0, 1}}},
                      {"call", TaskAbilityCall{"visual-tail", 2, false}},
                      {"join", TaskWaitAbilities{}},
                      {"entry", TaskSerial{{3, 4}}}},
                     5);
    TaskPresentation run({90}, 10, plan, f.player, {}, f.Services());
    run.Service(0.1, 1);
    Check(run.YieldedControl() && !run.Drained() && f.starts == 0);
    run.Service(0.4, 2);
    run.Service(0, 3);
    Check(f.starts == 1 && f.stops == 0 && !run.Drained());
    f.Retire();
    run.Service(0, 4);
    Check(run.Drained());
}
void PointHitDispatch() {
    Fixture f;
    auto status = std::make_shared<SpatialHitStatus>();
    unsigned sent = 0, cancelled = 0;
    f.point_hit = [&](const auto& target, const auto&) -> std::optional<SpatialHitEndpoint> {
        Check(target == "Point");
        ++sent;
        return SpatialHitEndpoint{status, [&] {
                                      ++cancelled;
                                      status->cancelled = true;
                                  }};
    };
    auto plan = Plan(
        {{"impact",
          TaskSubmit{"impact",
                     TaskHitCommand{"Point", {"Hit"}, true, {TaskEventKind::AfterHit, TaskEventKind::AfterHitAll}}}}});
    auto ports = f.Services();
    ports.event = [](const auto&, const auto&) -> std::shared_ptr<const TaskEventReceipt> {
        throw std::runtime_error("point must not notify actor");
    };
    TaskPresentation run({101}, 10, plan, f.player, {}, ports);
    run.Service(0, 1);
    run.Service(0, 2);
    Check(sent == 1 && !run.Drained());
    auto requests = run.Requests();
    Check(requests.size() == 1 && requests[0].receipt.participant.name == "target:Point" &&
          requests[0].receipt.entity_generation == 10);
    status->admitted = status->submitted = true;
    run.Service(0, 3);
    Check(cancelled == 0 && !run.Drained()); // admission does not destroy the natural effect tail
    status->closed = true;
    run.Service(0, 4);
    Check(run.Drained() && run.Status().reason == TaskExitReason::Completed);
    // A natural tail must remain cancellable after the sequence body finishes.
    status = std::make_shared<SpatialHitStatus>();
    status->admitted = status->submitted = true;
    TaskPresentation tail({104}, 10, plan, f.player, {}, f.Services());
    tail.Service(0, 5);
    tail.Service(0, 6);
    Check(!tail.Drained() && cancelled == 0);
    tail.Stop(TaskExitReason::Deleted);
    tail.Service(0, 7);
    Check(cancelled == 1 && status->cancelled && !tail.Drained());
    status->closed = true;
    tail.Service(0, 8);
    Check(tail.Drained());
    status = std::make_shared<SpatialHitStatus>();
    TaskPresentation stop({102}, 10, plan, f.player, {}, f.Services());
    stop.Service(0, 5);
    stop.Stop(TaskExitReason::Interrupted);
    stop.Service(0, 6);
    Check(cancelled == 2 && !stop.Drained());
    status->closed = true;
    stop.Service(0, 7);
    Check(stop.Drained());
    // Entity-only consumers do not route through the point endpoint.
    auto entity_only = Plan({{"impact", TaskSubmit{"impact", TaskHitCommand{"Point", {"Hit"}, false}}}});
    TaskPresentation invalid({103}, 10, entity_only, f.player, {}, f.Services());
    invalid.Service(0, 8);
    invalid.Service(0, 9);
    Check(sent == 3 && invalid.Status().reason == TaskExitReason::Failed);
}
void TargetDispatch() {
    Fixture f;
    std::vector<std::string> sent;
    f.point_hit = [&](const std::string& name, const auto&) -> std::optional<SpatialHitEndpoint> {
        sent.push_back(name);
        auto state = std::make_shared<SpatialHitStatus>();
        state->admitted = state->submitted = state->closed = true;
        return SpatialHitEndpoint{state, [] {}};
    };
    auto plan = Plan({{"hit", TaskSubmit{"hit", TaskHitCommand{"Param", {"Hit"}, true}}},
                      {"scope", TaskForTargets{"Param", {"A", "B"}, 0, {}}},
                      {"outside", TaskSubmit{"outside", TaskHitCommand{"Param", {"Hit"}, true}}},
                      {"entry", TaskSerial{{1, 2}}}},
                     3);
    TaskPresentation run({105}, 10, plan, f.player, {}, f.Services());
    run.Service(0, 1);
    run.Service(0, 2);
    Check(sent == std::vector<std::string>({"A", "B", "Param"}) && run.Drained() && run.Status().issues.empty());
    TaskSequenceInput with_collection(*plan);
    std::get<TaskForTargets>(with_collection.nodes[1].operation).collection = "Neighbours";
    plan = std::make_shared<TaskSequence>(with_collection);
    VisualProgramParameters parameters;
    parameters.target_collections["Neighbours"] = {"C", "A"};
    sent.clear();
    TaskPresentation supplied({106}, 10, plan, f.player, parameters, f.Services());
    parameters.target_collections["Neighbours"] = {"changed"};
    supplied.Service(0, 3);
    supplied.Service(0, 4);
    Check(sent == std::vector<std::string>({"C", "A", "Param"}) && supplied.Drained());
    f.defaults.target_collections["Neighbours"] = {"Default"};
    sent.clear();
    TaskPresentation defaulted({107}, 10, plan, f.player, {}, f.Services());
    defaulted.Service(0, 5);
    f.defaults.target_collections["Neighbours"] = {"Changed"};
    defaulted.Service(0, 6);
    Check(sent == std::vector<std::string>({"Default", "Param"}) && defaulted.Drained());
    VisualActorTimeScale clock;
    clock.key = "slow";
    clock.recipient = "Param";
    auto clocks = Plan({Visual("clock", clock), {"targets", TaskForTargets{"Param", {"A", "B"}, 0, {}}}}, 1);
    TaskPresentation clock_run({128}, 10, clocks, f.player, {}, f.Services());
    clock_run.Service(0, 7);
    clock_run.Service(0, 8);
    Check(clock_run.Drained() && f.clocks.size() == 2 && f.clocks[0].recipient == "A" && f.clocks[1].recipient == "B");
}
void HitDispatch() {
    Fixture f;
    auto inbox = std::make_shared<HitInbox>(20);
    unsigned sends = 0, plays = 0;
    bool ready = false;
    std::shared_ptr<HitRequestStatus> playback;
    f.hit = [&](auto&, const std::string& target, const HitInput& input) -> std::optional<HitDelivery> {
        Check(target == "Victim" && input.animation == "DamageHit");
        ++sends;
        return inbox->Post(input);
    };
    auto resolve = [&](const HitInput& input) {
        Check(input.animation == "DamageHit");
        return HitResolution{ready, HitCommand{{{"Add", "Hit", .5f}}}};
    };
    auto submit = [&](HitCommand command) -> std::shared_ptr<const HitRequestStatus> {
        Check(command.layers[0].state == "Hit");
        ++plays;
        playback = std::make_shared<HitRequestStatus>();
        playback->entity_generation = 20;
        return playback;
    };
    std::vector<TaskEventKind> events;
    auto ports = f.Services();
    ports.event = [&](const TaskEventCommand& command, const auto&) -> std::shared_ptr<const TaskEventReceipt> {
        Check(command.recipient == "Victim" && command.source == "Caster");
        events.push_back(command.kind);
        // The receiver's callback can still be running; hit delivery does not
        // wait for its visual tail or take ownership of the receiver's state.
        auto receipt = std::make_shared<TaskEventReceipt>();
        receipt->callbacks.push_back(std::make_shared<TaskLifetimeReceipt>());
        return receipt;
    };
    auto plan =
        Plan({{"impact", TaskSubmit{"impact", TaskHitCommand{"Victim",
                                                             {"DamageHit"},
                                                             false,
                                                             {TaskEventKind::AfterHit, TaskEventKind::AfterHitAll}}}}});
    TaskPresentation run({91}, 10, plan, f.player, {}, ports);
    run.Service(0, 1);
    run.Service(0, 2);
    Check(sends == 1 && plays == 0 && !run.Drained() && events.empty());
    inbox->Service(resolve, submit);
    Check(plays == 0);
    ready = true;
    inbox->Service(resolve, submit);
    run.Service(0, 3);
    Check(plays == 1 && run.Drained() && run.Status().reason == TaskExitReason::Completed && !playback->cancelled);
    Check(events == std::vector<TaskEventKind>({TaskEventKind::AfterHit, TaskEventKind::AfterHitAll}));
    run.Stop(TaskExitReason::Deleted);
    run.Service(0, 4);
    Check(!playback->cancelled);
    // Source cancellation before admission retracts the queued value. It does
    // not reset any animation on the target or retry against another actor.
    ready = false;
    TaskPresentation cancelled({92}, 10, plan, f.player, {}, ports);
    cancelled.Service(0, 5);
    cancelled.Stop(TaskExitReason::Deleted);
    ready = true;
    inbox->Service(resolve, submit);
    cancelled.Service(0, 6);
    Check(cancelled.Drained() && plays == 1 && !playback->cancelled && events.size() == 2);
    // Retiring the bound target reports failure, not a fallback to the caster.
    ready = false;
    TaskPresentation lost({93}, 10, plan, f.player, {}, f.Services());
    lost.Service(0, 7);
    inbox->Close();
    lost.Service(0, 8);
    lost.Service(0, 9);
    Check(lost.Drained() && lost.Status().reason == TaskExitReason::Failed && plays == 1);
    bool closed = false;
    try {
        inbox->Post({"DamageHit"});
    } catch (const std::logic_error&) {
        closed = true;
    }
    Check(closed);
    // Cancellation/owner close during a policy read precedes the commit.
    HitInbox reentrant(30);
    auto pending = reentrant.Post({"DamageHit"});
    reentrant.Service(
        [&](const auto&) {
            pending.cancel();
            return HitResolution{true, HitCommand{{{"Add", "Hit", 1}}}};
        },
        submit);
    Check(pending.status->cancelled && plays == 1);
    // Empty HitAnimation is a delivered no-op, not a missing implementation.
    auto ignored = reentrant.Post({});
    reentrant.Service([](const auto&) { return HitResolution{true, {}}; }, submit);
    Check(ignored.status->delivered && ignored.status->ignored && !ignored.status->playback && plays == 1);
    // Effects and reactions have independent admission. Empty/throttled body
    // does not skip a hit effect; a pending Animator does not resubmit it.
    HitInbox channels(20);
    HitInput with_effect{"DamageHit"};
    with_effect.effect = HitEffectInput{"hit-path"};
    auto separate = channels.Post(with_effect);
    int effect_calls = 0;
    auto receive_effect = [&](const HitInput&) {
        ++effect_calls;
        return HitEffectAdmission{true, false, {}};
    };
    ready = false;
    channels.Service(resolve, submit, receive_effect);
    Check(effect_calls == 1 && separate.status->effects_delivered && !separate.status->animation_delivered);
    separate.cancel();
    Check(!separate.status->cancelled); // receiver already owns the accepted half
    ready = true;
    channels.Service(resolve, submit, receive_effect);
    Check(effect_calls == 1 && separate.status->delivered && plays == 2);
    with_effect.animation.clear();
    auto no_animation = channels.Post(with_effect);
    channels.Service([](const auto&) { return HitResolution{true, {}}; }, submit, receive_effect);
    Check(no_animation.status->delivered && no_animation.status->ignored && !no_animation.status->effects_ignored &&
          effect_calls == 2);
    with_effect.animation = "DamageHit";
    auto failed_effect = channels.Post(with_effect);
    channels.Service(resolve, submit,
                     [](const auto&) -> HitEffectAdmission { throw std::runtime_error("effect unavailable"); });
    Check(failed_effect.status->playback && failed_effect.status->delivered &&
          failed_effect.status->effect_errors.size() == 1 && plays == 3);
    auto close_in_effect = channels.Post(with_effect);
    channels.Service(resolve, submit, [&](const auto&) {
        channels.Close();
        return HitEffectAdmission{true, false, {}};
    });
    Check(close_in_effect.status->cancelled && plays == 3);
}
void HitPerformanceBoundary() {
    Fixture f;
    auto inbox = std::make_shared<HitInbox>(10);
    std::shared_ptr<HitRequestStatus> playback;
    f.hit = [&](auto&, const std::string& target, const HitInput&) -> std::optional<HitDelivery> {
        Check(target == "Caster");
        return inbox->Post({"Hit"});
    };
    auto deliver = [&] {
        inbox->Service([](const auto&) { return HitResolution{true, HitCommand{{{"Add", "Hit", 1.f}}}}; },
                       [&](const auto&) -> std::shared_ptr<const HitRequestStatus> {
                           playback = std::make_shared<HitRequestStatus>();
                           playback->entity_generation = 10;
                           return playback;
                       });
    };
    auto plan = Plan({{"hit", TaskSubmit{"hit", TaskHitCommand{"Caster", {"Hit"}}}},
                      {"wait-hit", TaskWaitRequest{"hit", TaskWaitSignal::PerformanceComplete}},
                      {"idle", TaskSubmit{"idle", TaskAnimationCommand{{"body"}, "Idle"}}},
                      {"entry", TaskSerial{{0, 1, 2}}}},
                     3);
    TaskPresentation run({191}, 10, plan, f.player, {}, f.Services());
    run.Service(0, 1);
    deliver();
    run.Service(0, 2);
    Check(playback && !run.Drained() && f.animations.empty());
    const auto requests = run.Requests();
    Check(requests.size() == 1 && requests[0].state.request_complete && !requests[0].state.performance_complete);
    playback->accepted = playback->entered = true;
    run.Service(0, 3);
    Check(f.animations.empty());
    playback->clip_complete = true;
    run.Service(0, 4);
    Check(f.animations.empty()); // expression still belongs to the receiver
    playback->expression_released = true;
    run.Service(0, 5);
    run.Service(0, 6);
    Check(f.animations.count("body") == 1);
    f.animations.at("body")->accepted = f.animations.at("body")->complete = true;
    run.Service(0, 7);
    run.Service(0, 8);
    Check(run.Drained() && run.Status().reason == TaskExitReason::Completed && !playback->cancelled);
    // A receiver-side failure must reach a waiting script, not be reported as
    // successful delivery followed by an invisible return to idle.
    Fixture failed;
    auto receipt = std::make_shared<HitDeliveryStatus>();
    receipt->entity_generation = 10;
    receipt->delivered = true;
    receipt->playback = std::make_shared<HitRequestStatus>();
    auto rejected = std::const_pointer_cast<HitRequestStatus>(receipt->playback);
    rejected->entity_generation = 10;
    rejected->error = "hit animation unavailable";
    rejected->released = rejected->cancelled = true;
    failed.hit = [&](auto&, const auto&, const auto&) -> std::optional<HitDelivery> {
        return HitDelivery{receipt, [] {}};
    };
    TaskPresentation bad({192}, 10, plan, failed.player, {}, failed.Services());
    bad.Service(0, 1);
    bad.Service(0, 2);
    Check(bad.Drained() && bad.Status().reason == TaskExitReason::Failed && !bad.Status().issues.empty() &&
          failed.animations.empty());
    const auto failure = bad.Status().issues;
    rejected->error.clear();
    bad.Service(0, 3);
    Check(bad.Status().reason == TaskExitReason::Failed &&
          bad.Status().issues == failure); // later drain cannot erase the failure
    // An intentionally empty or throttled reaction is a delivered no-op and
    // cannot leave a script waiting for an animation that will not be created.
    Fixture ignored;
    receipt = std::make_shared<HitDeliveryStatus>();
    receipt->entity_generation = 10;
    receipt->delivered = receipt->ignored = true;
    ignored.hit = [&](auto&, const auto&, const auto&) -> std::optional<HitDelivery> {
        return HitDelivery{receipt, [] {}};
    };
    TaskPresentation skip({193}, 10, plan, ignored.player, {}, ignored.Services());
    skip.Service(0, 1);
    skip.Service(0, 2);
    Check(ignored.animations.count("body") == 1);
    ignored.animations.at("body")->accepted = ignored.animations.at("body")->complete = true;
    skip.Service(0, 3);
    skip.Service(0, 4);
    Check(skip.Drained());
}
}
namespace {
void AttackHistory() {
    using K = TaskEventKind;
    using B = TaskAttackBoundary;
    std::vector<std::pair<unsigned, K>> calls;
    auto emit = [&](unsigned target) -> TaskAttackHistory::Emit {
        return [&, target](K kind) {
            calls.emplace_back(target, kind);
            return std::make_shared<TaskEventReceipt>();
        };
    };
    TaskAttackHistory history;
    history.Boundary(B::Finish, emit(1));
    Check(calls.empty());
    history.Boundary(B::BeginChunk, emit(1));
    history.Record(2, emit(2));
    history.Record(2, emit(222));
    history.Record(3, emit(3));
    history.Boundary(B::EndChunk, emit(1));
    Check(calls == std::vector<std::pair<unsigned, K>>{
                       {1, K::AfterDamageChunk}, {2, K::AfterBeingDamageChunk}, {3, K::AfterBeingDamageChunk}});
    calls.clear();
    history.Boundary(B::BeginChunk, emit(1));
    history.Record(3, emit(3));
    history.Boundary(B::Finish, emit(1));
    history.Boundary(B::Finish, emit(1));
    Check(calls == std::vector<std::pair<unsigned, K>>{{2, K::AfterAttackedAdv},
                                                       {3, K::AfterAttackedAdv},
                                                       {1, K::AfterAttack},
                                                       {2, K::AfterAttacked},
                                                       {3, K::AfterAttacked}});
    calls.clear();
    history.Record(4, emit(4));
    history.Boundary(B::Complete, emit(1));
    Check(calls == std::vector<std::pair<unsigned, K>>{
                       {1, K::AfterAttackEnd}, {2, K::AfterAttackedEnd}, {3, K::AfterAttackedEnd}});
    calls.clear();
    history.Boundary(B::Complete, emit(1));
    Check(calls.empty());
    history.Boundary(B::EndChunk, emit(1)); // separate history, not attack-list clearing
    Check(calls == std::vector<std::pair<unsigned, K>>{
                       {1, K::AfterDamageChunk}, {3, K::AfterBeingDamageChunk}, {4, K::AfterBeingDamageChunk}});
    calls.clear();
    history.Record(0, {});
    history.Boundary(B::Finish, emit(1));
    Check(calls == std::vector<std::pair<unsigned, K>>{{1, K::AfterAttack}}); // forward ground has no receiver
    calls.clear();
    {
        TaskAttackHistory abandoned;
        abandoned.Record(2, emit(2));
    }
    Check(calls.empty()); // destruction/cancel is not a successful settlement
    TaskAttackHistory failed;
    failed.Record(2, [](auto) -> std::shared_ptr<const TaskEventReceipt> { throw std::runtime_error("stale target"); });
    failed.Record(3, emit(3));
    const auto result = failed.Boundary(B::Complete, emit(1));
    Check(result->State().error == "stale target" && calls.size() == 5); // independent participants still dispatch
}
void AttackBoundaries() {
    Fixture f;
    f.defaults.targets = {{"A", 20}, {"B", 20}};
    f.defaults.position_targets["Point"] = {{1, 0, 2}};
    std::vector<std::pair<std::string, TaskEventKind>> calls;
    auto ports = f.Services();
    ports.event = [&](const auto& command, const auto& inputs) {
        if (command.recipient != "Caster")
            Check(inputs.targets.at(command.recipient) == 20);
        calls.emplace_back(command.recipient, command.kind);
        return std::make_shared<TaskEventReceipt>();
    };
    using B = TaskAttackBoundary;
    using K = TaskEventKind;
    auto plan = Plan({{"root", TaskSerial{{1, 2, 3, 4, 5, 6}}},
                      {"first", TaskSubmit{"first", TaskAttackCommand{B::Record, "A"}}},
                      {"alias", TaskSubmit{"alias", TaskAttackCommand{B::Record, "B"}}},
                      {"point", TaskSubmit{"point", TaskAttackCommand{B::Record, "Point", true}}},
                      {"finish", TaskSubmit{"finish", TaskAttackCommand{B::Finish}}},
                      {"post", TaskSubmit{"post", TaskAttackCommand{B::Complete}}},
                      {"wait", TaskWaitRequest{"post"}}});
    TaskPresentation run({111}, 10, plan, f.player, {}, ports);
    for (unsigned i = 0; i < 8; ++i)
        run.Service(.01, i);
    Check(run.Drained() && run.Status().reason == TaskExitReason::Completed);
    Check(calls == std::vector<std::pair<std::string, K>>{{"A", K::AfterAttackedAdv},
                                                          {"Caster", K::AfterAttack},
                                                          {"A", K::AfterAttacked},
                                                          {"Caster", K::AfterAttackEnd},
                                                          {"A", K::AfterAttackedEnd}});
    // No native-hit/animation service was supplied: history is not inferred
    // from receiver animation admission or final poses.
}
}
void TestTaskPresentation() {
    CompletedAnimationReplacement();
    AnimationParameterParsing();
    AnimationParameters();
    CurrentIdleSelection();
    SkippedLifetimeScope();
    SkippedVisualScope();
    Scopes();
    IndependentParticipants();
    ConfiguredPoses();
    ErrorsAndReentry();
    NamedContexts();
    IndependentTail();
    Sounds();
    Movements();
    MovementInputs();
    AbilityTailScope();
    HitDispatch();
    HitPerformanceBoundary();
    PointHitDispatch();
    TargetDispatch();
    AttackHistory();
    AttackBoundaries();
}
