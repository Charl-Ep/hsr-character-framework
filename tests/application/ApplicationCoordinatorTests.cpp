#include "../support/LegacyScriptFixture.hpp"
#include <hksr/framework/application/ApplicationCoordinator.hpp>
#include <hksr/framework/layout/LayoutStorage.hpp>
#include <hksr/presentation/PresentationBindings.hpp>
#include <hksr/framework/host/RuntimeServices.hpp>
#include <hksr/framework/tasks/TaskCatalog.hpp>
#include <Windows.h>
#include <algorithm>
#include <stdexcept>

namespace {
void Check(bool value) {
    if (!value)
        throw std::runtime_error("application coordinator test failed");
}
struct NoObjects final : hksr::api::ManagedObjects {
    hksr::api::ObjectResult NewStringUtf8(const char*) override {
        throw std::runtime_error("unexpected engine call");
    }
    hksr::api::ObjectResult AllocateObject(const char*, const char*) override {
        throw std::runtime_error("unexpected engine call");
    }
    hksr::api::ObjectResult ManagedType(const char*, const char*) override {
        throw std::runtime_error("unexpected engine call");
    }
};
}
void TestApplicationCoordinator() {
    using namespace hksr::framework;
    namespace api = hksr::api;
    ApplicationService service;
    NoObjects objects;
    api::PresentationBindings bindings;
    int info = 0;
    const auto retain = +[](void*, int, void*) -> int { throw std::runtime_error("unexpected retain call"); };
    const auto target = +[](int, void*) -> void* { throw std::runtime_error("unexpected target call"); };
    const auto release = +[](int, void*) { throw std::runtime_error("unexpected free call"); };
    bindings.references = {{reinterpret_cast<void*>(retain), &info},
                           {reinterpret_cast<void*>(target), &info},
                           {reinterpret_cast<void*>(release), &info},
                           {},
                           {}};
    RuntimeServices runtime(objects, bindings);
    SceneCharacters characters(runtime.Sessions(false), SceneCharacters::Create{});
    unsigned reads = 0, player_reads = 0, observations = 0, invalidations = 0, diagnostics = 0;
    CatalogReaders readers;
    readers.characters = [&] {
        ++reads;
        api::CharacterCatalog out;
        out.npcs.enumerated = true;
        out.npcs.rows = {{77, "Npc/config", "Npc/model"}};
        out.players.enumerated = true;
        out.players.rows = {{11, 0, 0, 0, "Player/config", "Player.prefab"}};
        return out;
    };
    readers.document = [](auto kind, const std::string& path) {
        api::ActionConfigInput out;
        out.kind = kind;
        out.path = path;
        out.loaded = true;
        return out;
    };
    ApplicationRuntimePorts ports;
    ports.player_placement = [&](const auto&) {
        ++player_reads;
        return CharacterPlacement{{10, 20, 30}, {0, 1, 0, 0}};
    };
    ports.observe_character = [&](auto) { ++observations; };
    ports.source_replaced = [&](auto) { ++invalidations; };
    ports.npc_actions = [](auto, const auto&) { throw std::runtime_error("query failure"); };
    bool camera = false, camera_failure = false;
    ports.camera_presentations = [&](bool enabled) {
        if (camera_failure)
            throw std::runtime_error("view policy unavailable");
        camera = enabled;
    };
    ApplicationCoordinator coordinator(service, characters, std::make_unique<CatalogBrowser>(readers),
                                       {true, true, false}, std::move(ports), [&](const auto&) {
                                           ++diagnostics;
                                           throw std::runtime_error("log failure");
                                       });
    const api::WorldFrame frame{};
    const SceneAddress scene{"saved-map", 1};
    auto dispatch = [&](bool closing = false) {
        auto batch = service.TakeCommands(closing);
        for (auto& pending : batch)
            coordinator.Dispatch(pending, scene, frame, closing);
        return service.TakeReplies();
    };
    auto snapshot = [&](bool closing = false) {
        ApplicationSnapshot out;
        out.closing = closing;
        coordinator.Populate(out);
        return out;
    };
    Check(!snapshot().camera_presentations);
    service.Post(SetCameraPresentations{true});
    Check(dispatch().at(0).accepted && camera && snapshot().camera_presentations);
    camera_failure = true;
    service.Post(SetCameraPresentations{false});
    Check(!dispatch().at(0).accepted && snapshot().camera_presentations &&
          snapshot().camera_presentations_revision == 1);
    camera_failure = false;
    service.Post(SetCameraPresentations{false});
    Check(dispatch().at(0).accepted && !camera && !snapshot().camera_presentations &&
          snapshot().camera_presentations_revision == 2);
    service.Post(PrepareSource{BrowseNpc{77}});
    auto replies = dispatch();
    const auto source = replies.at(0).source_request;
    Check(source && replies[0].accepted && reads == 0 && player_reads == 0);
    coordinator.Catalog()->Advance();
    coordinator.Catalog()->Advance();
    Check(snapshot().catalog->Source(source)->phase == SourceRequestPhase::Ready);
    Check(snapshot().operations.entries.at(replies[0].serial).phase == OperationPhase::Returned);

    AddPreparedCharacter add;
    add.source_request = source;
    add.near_player = true;
    add.player_world_offset = {-2, 3, 4};
    service.Post(add);
    replies = dispatch();
    const auto id = replies.at(0).character;
    Check(id && replies[0].accepted && player_reads == 1 && observations == 0);
    const auto state = characters.Status(id);
    Check(state.scene == scene.key && state.placement.position.x == 8 && state.placement.position.y == 23 &&
          state.placement.position.z == 34 && state.placement.rotation.y == 1);
    Check(!state.active && state.creations == 0); // intent only; no engine session serviced
    Check(snapshot().operations.entries.at(replies[0].serial).phase == OperationPhase::Waiting);

    auto input = std::make_shared<VisualProgramInput>();
    input->tasks = std::make_shared<const TaskCatalog>(ParseLegacyTaskCatalog(R"({
      "format":"hksr.task-catalog","id":"test-preview","domain":"exploration",
      "visual":{"id":"preview-scene","source":"Player/config","scope":"preview","effects":{}},
      "sequences":{"idle":{"entry":{"op":"animation","request":"body","participant":"body","state":"StandBy"}}},
      "tasks":{"idle":{"entry":"idle"}}})"));
    input->program = input->tasks->visual;
    characters.ConfigurePrograms({{}, {{"test-preview", input}}});
    service.Post(PublishTaskCatalogs{{{"test-preview", input}}});
    replies = dispatch();
    Check(replies[0].accepted && replies[0].task_catalog_revision == 2 && !replies[0].operation);
    service.Post(PublishTaskCatalogs{{{"wrong-key", input}}});
    replies = dispatch();
    Check(!replies[0].accepted && !replies[0].task_catalog_revision);
    service.Post(PrepareSource{BrowsePlayer{{11, 0, 0}}});
    replies = dispatch();
    const auto player_source = replies[0].source_request;
    coordinator.Catalog()->Advance(0);
    Check(snapshot().catalog->Source(player_source)->phase == SourceRequestPhase::Ready);
    auto preview = add;
    preview.source_request = player_source;
    preview.near_player = false;
    preview.task_catalog = "test-preview";
    preview.transient = true;
    service.Post(preview);
    replies = dispatch();
    Check(replies[0].accepted && characters.Plan(replies[0].character).visual_program->tasks == input->tasks);
    Check(characters.Status(replies[0].character).transient && !characters.Status(id).transient &&
          !characters.Plan(id).visual_program);
    const auto preview_id = replies[0].character;
    auto persistent = preview;
    persistent.transient = false;
    service.Post(persistent);
    replies = dispatch();
    const auto persistent_id = replies[0].character;
    Check(replies[0].accepted && !characters.Status(persistent_id).transient &&
          characters.Plan(persistent_id).visual_program->tasks == input->tasks);
    service.Post(DeleteCharacter{persistent_id});
    dispatch();
    preview.task_catalog = "missing";
    const auto count = characters.Ids().size();
    service.Post(preview);
    replies = dispatch();
    Check(!replies[0].accepted && characters.Ids().size() == count);
    service.Post(ReplacePreparedCharacter{preview_id, source, {}, {}});
    replies = dispatch();
    Check(replies[0].accepted && characters.Status(preview_id).transient);
    service.Post(DeleteCharacter{preview_id});
    dispatch();
    invalidations = 0; // the next assertions concern the original source replacement

    service.Post(ReadCharacterNpcActions{id}); // diagnostic failure cannot block ordinary control
    const auto old_action = service.Post(SelectCharacterAction{id, {ActionDomain::Exploration, NpcMotion{1}}});
    const auto new_action = service.Post(SelectCharacterAction{id, {ActionDomain::Exploration, NpcMotion{2}}});
    replies = dispatch();
    Check(replies.size() == 3 && !replies[0].accepted && replies[0].error == "query failure");
    Check(replies[1].accepted && replies[2].accepted && observations == 0);
    auto out = snapshot();
    Check(out.operations.entries.at(old_action).phase == OperationPhase::Superseded);
    Check(out.operations.entries.at(new_action).phase == OperationPhase::Waiting);
    service.Post(ObserveCharacter{id});
    dispatch();
    Check(observations == 1);

    // Source replacement is application policy; runtime query caches are
    // invalidated only after the actual replacement, not on rejected input.
    service.Post(ReplacePreparedCharacter{id, 999, {}, {}});
    replies = dispatch();
    Check(!replies[0].accepted && invalidations == 0);
    service.Post(ReplacePreparedCharacter{id, source, {}, {}});
    replies = dispatch();
    Check(replies[0].accepted && invalidations == 1 && !characters.Status(id).action);
    service.Post(ReleaseSource{source});
    dispatch();
    Check(!snapshot().catalog->Source(source) && characters.Plan(id).package.npc_id == 77);
    auto battle_plan = characters.Plan(id);
    battle_plan.alternate = BattlePresentation{};
    battle_plan.alternate->initial_state = {true, "OfficialIdle", {}};
    characters.ReplacePlan(id, std::move(battle_plan));
    service.Post(IdleCharacter{id});
    replies = dispatch();
    Check(replies[0].accepted);
    Check(characters.DesiredAction(id)->domain == ActionDomain::Battle &&
          std::holds_alternative<BattleIdle>(characters.DesiredAction(id)->request));

    // Callbacks are outside the command batch and may fail or enqueue work;
    // neither failure nor logging exceptions replay already-applied intent.
    service.Post(RebuildCharacter{id}, [&](const auto& reply) {
        Check(reply.accepted);
        service.Post(EnableCharacter{id, false});
        throw std::runtime_error("receiver failure");
    });
    Check(dispatch().empty() && characters.Status(id).enabled);
    replies = dispatch();
    Check(replies.size() == 1 && replies[0].accepted && !characters.Status(id).enabled);
    Check(diagnostics > 0);
    service.Post(RebuildCharacter{id});
    characters.Shutdown();
    replies = dispatch(true);
    Check(!replies[0].accepted &&
          snapshot(true).operations.entries.at(replies[0].serial).phase == OperationPhase::Cancelled);
    coordinator.CloseCatalog();
    coordinator.CloseCatalog();
    Check(!coordinator.Catalog() && characters.Closed());
    Check(!snapshot(true).catalog);

    // Missing optional directory/inspection support does not prevent a saved
    // source from being added. Only the creation ownership capability matters.
    ApplicationService direct;
    SceneCharacters saved(runtime.Sessions(false), SceneCharacters::Create{});
    ApplicationCoordinator plain(direct, saved, nullptr, {true, false, false}, {});
    CharacterPlan plan;
    plan.package.npc_id = 77;
    direct.Post(AddCharacter{plan, "saved", {}, {}});
    direct.Post(RequestCatalog{});
    auto batch = direct.TakeCommands(false);
    for (auto& pending : batch)
        plain.Dispatch(pending, {}, frame, false);
    replies = direct.TakeReplies();
    Check(replies[0].accepted && !replies[1].accepted && saved.Ids().size() == 1);
    saved.Shutdown();
    plain.CloseCatalog();
    Check(saved.Closed());

    // Drive the real layout IO/controller through dispatch interleavings. No
    // engine actor is created: unrelated/query task commands reach the normal
    // inactive-creation validation, rather than being blocked by the layout.
    {
        const auto folder = std::filesystem::temp_directory_path() /
                            ("hksr-layout-task-edits-" + std::to_string(GetCurrentProcessId()) + "-" +
                             std::to_string(GetTickCount64()));
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(path, e);
            }
        } cleanup{folder};
        ApplicationService app;
        SceneCharacters registry(runtime.Sessions(false), SceneCharacters::Create{});
        ApplicationCoordinator coordinated(app, registry, std::make_unique<CatalogBrowser>(readers),
                                           {true, true, false}, {});
        auto io = std::make_shared<LayoutIo>(folder);
        coordinated.ConfigureLayouts(io);
        const SceneAddress mapA{"mapA", 1}, mapB{"mapB", 2};
        api::WorldFrame safe_frame;
        safe_frame.battle = {false, {true, 0}};
        auto send = [&](CharacterCommand command) {
            app.Post(std::move(command));
            auto commands = app.TakeCommands(false);
            Check(commands.size() == 1);
            coordinated.Dispatch(commands[0], mapA, safe_frame, false);
            auto result = app.TakeReplies();
            Check(result.size() == 1);
            return result[0];
        };
        auto layout_status = [&] {
            ApplicationSnapshot value;
            coordinated.Populate(value);
            return value.layouts.maps.at(mapA.key);
        };
        coordinated.AdvanceLayouts(mapB);
        io->Pump();
        coordinated.AdvanceLayouts(mapB);
        coordinated.AdvanceLayouts(mapA);
        io->Pump();
        coordinated.AdvanceLayouts(mapA);
        Check(layout_status().phase == LayoutPhase::Ready);
        CharacterPlan original;
        original.package.npc_id = 77;
        original.source_intent = BrowseNpc{77};
        const auto old = registry.Add(mapA.key, original);
        const auto other_map = registry.Add(mapB.key, original);
        auto session = original;
        session.persistence = CharacterPersistence::Session;
        const auto temporary = registry.Add(mapA.key, session);
        for (auto actor : {old, other_map, temporary})
            registry.SetEnabled(actor, false);
        const auto unchanged = CaptureSceneLayout(registry, mapA.key);
        SceneLayout incoming;
        incoming.scene = mapA.key;
        SavedCharacter replacement;
        replacement.key = 100;
        replacement.source = BrowseNpc{77};
        replacement.enabled = false;
        incoming.characters.push_back(replacement);
        LayoutStorage storage(folder);
        storage.SaveNamed("replacement", incoming);
        auto invalid = incoming;
        invalid.characters[0].source = BrowseNpc{999};
        storage.SaveNamed("invalid", invalid);
        auto task = [&](std::uint64_t actor, TaskControlOperation operation) {
            TaskControlInput input;
            input.operation = operation;
            if (operation == TaskControlOperation::Select)
                input.selection = TaskSelection{{"test-preview", "idle"}};
            return send(ControlCharacterTask{actor, 1, std::move(input)});
        };
        auto forwarded = [&](std::uint64_t actor, TaskControlOperation operation) {
            const auto result = task(actor, operation);
            Check(!result.accepted && result.error == "task command targets an inactive or replaced creation" &&
                  !result.operation);
        };
        auto replacing = [&] {
            for (auto operation :
                 {TaskControlOperation::Select, TaskControlOperation::Finish, TaskControlOperation::Cancel}) {
                const auto result = task(old, operation);
                Check(!result.accepted &&
                      result.error == "this map is restoring its layout; edit after replacement completes" &&
                      !result.operation);
                forwarded(other_map, operation);
                forwarded(temporary, operation);
            }
            forwarded(old, TaskControlOperation::Query);
            Check(SameLayout(unchanged, CaptureSceneLayout(registry, mapA.key)));
            const auto action = send(SelectCharacterAction{old, {ActionDomain::Exploration, NpcMotion{1}}});
            Check(!action.accepted && action.operation &&
                  action.error.find("restoring its layout") != std::string::npos);
            Check(send(SelectCharacterAction{temporary, {ActionDomain::Exploration, NpcMotion{1}}}).accepted);
            Check(send(SelectCharacterAction{other_map, {ActionDomain::Exploration, NpcMotion{1}}}).accepted);
            Check(send(RequestCatalog{}).accepted);
        };
        Check(send(LayoutCommand{LayoutOperation::Load, "replacement"}).accepted);
        Check(layout_status().busy && layout_status().operation == LayoutActivity::Load);
        replacing(); // file read has not completed
        io->Pump();
        coordinated.AdvanceLayouts(mapA);
        Check(layout_status().phase == LayoutPhase::Preparing);
        replacing(); // sources have not advanced; the old actor still exists
        Check(registry.Contains(old) && !registry.Status(old).deleting);
        for (unsigned i = 0; i < 12; ++i) {
            coordinated.Catalog()->Advance(0);
            io->Pump();
            coordinated.AdvanceLayouts(mapA);
            registry.Service(safe_frame, mapA, runtime.Appearance(), {});
        }
        Check(layout_status().phase == LayoutPhase::Ready && !registry.Contains(old));
        Check(registry.Contains(other_map) && registry.Contains(temporary));
        auto restored = registry.Ids();
        restored.erase(std::remove(restored.begin(), restored.end(), other_map), restored.end());
        restored.erase(std::remove(restored.begin(), restored.end(), temporary), restored.end());
        Check(restored.size() == 1);
        for (auto operation : {TaskControlOperation::Select, TaskControlOperation::Finish, TaskControlOperation::Cancel,
                               TaskControlOperation::Query})
            forwarded(restored[0], operation);
        Check(send(SelectCharacterAction{restored[0], {ActionDomain::Exploration, NpcMotion{2}}}).accepted);
        Check(send(LayoutCommand{LayoutOperation::Load, "invalid"}).accepted);
        for (unsigned i = 0; i < 12; ++i) {
            io->Pump();
            coordinated.AdvanceLayouts(mapA);
            coordinated.Catalog()->Advance(0);
        }
        Check(layout_status().phase == LayoutPhase::Failed && registry.Contains(restored[0]) &&
              !registry.Status(restored[0]).deleting);
        for (auto operation : {TaskControlOperation::Select, TaskControlOperation::Finish, TaskControlOperation::Cancel,
                               TaskControlOperation::Query})
            forwarded(restored[0], operation);
        Check(send(SelectCharacterAction{restored[0], {ActionDomain::Exploration, NpcMotion{3}}}).accepted);
        coordinated.CaptureLayouts(true);
        registry.Shutdown();
        io->Pump();
        coordinated.CloseCatalog();
        Check(registry.Closed());
    }
}
