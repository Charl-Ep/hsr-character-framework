#include <hksr/framework/layout/SceneLayout.hpp>
#include <hksr/framework/layout/LayoutStorage.hpp>
#include <hksr/presentation/PresentationBindings.hpp>
#include <hksr/framework/character/SceneCharacters.hpp>
#include <hksr/framework/host/RuntimeServices.hpp>
#include <hksr/framework/presentation/scene/VisualProgramRuntime.hpp>
#include <hksr/framework/tasks/TaskCatalog.hpp>
#include <nlohmann/json.hpp>
#include <Windows.h>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool b) {
    if (!b)
        throw std::runtime_error("scene layout test failed");
}
template <class F> void Reject(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception&) {
        rejected = true;
    }
    Check(rejected);
}
struct NoObjects final : hksr::api::ManagedObjects {
    hksr::api::ObjectResult NewStringUtf8(const char*) override {
        throw std::runtime_error("unexpected game call");
    }
    hksr::api::ObjectResult AllocateObject(const char*, const char*) override {
        throw std::runtime_error("unexpected game call");
    }
    hksr::api::ObjectResult ManagedType(const char*, const char*) override {
        throw std::runtime_error("unexpected game call");
    }
};
}
void TestSceneLayout() {
    using namespace hksr::framework;
    SceneLayout data;
    data.scene = "hsr-map-v1:1:2";
    SavedCharacter first;
    first.key = 17;
    first.source = BrowseNpc{1110};
    first.placement = {{1, 2, 3}, {0, 0, 0, 1}};
    first.action = NpcMotion{123};
    SavedCharacter second;
    second.key = 29;
    second.source = BrowsePlayer{{1217, 0, 0}};
    second.placement = {{4, 5, 6}, {0, 0, 0, 1}};
    second.enabled = false;
    second.look = {LookMode::Instance, 17, {}};
    second.look.max_distance = 12;
    second.action = SavedReadyAction{"official/config", "Ready", "Skill02", -1};
    second.restore.presentation_phase = "authored-phase";
    second.restore.playback_rate = 2;
    second.restore.manual_reaction = TaskVisualReaction{"official/config", 115, DeathOccurrence::Fly};
    data.characters = {first, second};
    const auto bytes = EncodeSceneLayout(data);
    const auto encoded = nlohmann::json::parse(bytes)["characters"][1];
    Check(!encoded["source"].contains("npc_id"));
    Check(!encoded.contains("restore") && encoded["playback_rate"] == 2 &&
          encoded["presentation_phase"] == "authored-phase" && encoded.contains("visual_reaction"));
    const auto parsed = DecodeSceneLayout(bytes);
    Check(parsed.characters[1].restore.playback_rate == 2);
    auto bad_rate = data;
    bad_rate.characters[0].restore.playback_rate = 3;
    Reject([&] { EncodeSceneLayout(bad_rate); });
    auto bad_rate_json = nlohmann::json::parse(bytes);
    bad_rate_json["characters"][0]["playback_rate"] = 1.5;
    Reject([&] { DecodeSceneLayout(bad_rate_json.dump()); });
    auto normal_rate = data;
    normal_rate.characters[1].restore.playback_rate = 1;
    Check(!SameLayout(data, normal_rate));
    Check(EncodeSceneLayout(parsed) == bytes && parsed.characters[1].look.target == 17 &&
          !parsed.characters[1].enabled && std::get<BrowsePlayer>(parsed.characters[1].source).key.id == 1217);
    Check(parsed.characters[1].restore.presentation_phase == "authored-phase");
    Check(parsed.characters[1].restore.manual_reaction && parsed.characters[1].restore.manual_reaction->source == "official/config" &&
          parsed.characters[1].restore.manual_reaction->flag == 115 &&
          parsed.characters[1].restore.manual_reaction->occurrence == DeathOccurrence::Fly);
    auto slot_changed = parsed;
    slot_changed.characters[1].restore.manual_reaction->occurrence = DeathOccurrence::Limbo;
    Check(!SameLayout(parsed, slot_changed));
    slot_changed.characters[0].action = VisualActionKey{"official/config", 122, false, DeathOccurrence::Hit};
    const auto slot_roundtrip = DecodeSceneLayout(EncodeSceneLayout(slot_changed));
    Check(std::get<VisualActionKey>(*slot_roundtrip.characters[0].action).occurrence == DeathOccurrence::Hit &&
          SameLayout(slot_changed, slot_roundtrip));
    auto reaction_changed = parsed;
    reaction_changed.characters[1].restore.manual_reaction->flag = 133;
    Check(!SameLayout(parsed, reaction_changed));
    reaction_changed = parsed;
    reaction_changed.characters[1].restore.manual_reaction.reset();
    Check(!SameLayout(parsed, reaction_changed));
    reaction_changed = parsed;
    reaction_changed.characters[1].restore.manual_reaction->source.clear();
    Reject([&] { EncodeSceneLayout(reaction_changed); });
    Check(parsed.characters[1].look.max_distance == 12);
    auto skin_layout = parsed;
    skin_layout.characters[1].source = BrowseSkin{1151101};
    const auto skin_saved = DecodeSceneLayout(EncodeSceneLayout(skin_layout));
    Check(std::get<BrowseSkin>(skin_saved.characters[1].source).skin_id == 1151101 &&
          SameLayout(skin_layout, skin_saved));
    auto other_skin = skin_saved;
    other_skin.characters[1].source = BrowseSkin{1141501};
    Check(!SameLayout(skin_saved, other_skin));
    other_skin.characters[1].source = BrowseSkin{0};
    Reject([&] { EncodeSceneLayout(other_skin); });
    auto different = parsed;
    different.characters[1].restore.presentation_phase.clear();
    Check(!SameLayout(parsed, different));
    auto visual = parsed;
    VisualContextSnapshot context;
    context.name = "phase";
    context.inputs.targets["Target"] = 17;
    context.inputs.choices["variant"] = "left";
    context.variables["progress"] = 0.75f;
    context.numbers = {{"count", std::int64_t(9007199254740993LL)}, {"energy", 4.0}};
    context.values = {{"enabled", true},
                      {"stance", std::string("enhanced")},
                      {"wide", std::int64_t(9007199254740993LL)},
                      {"weight", 0.5}};
    context.effects = {"free", "normal", "free"};
    context.inputs.anchors["origin"] = {{1, 2, 3}, {0, 0, 0, 1}};
    context.inputs.position_targets["empty-space"] = {{6, -15, 58}, {0, 1, 0, 0}};
    context.inputs.target_aliases["ParamEntity"] = "Target";
    context.inputs.target_collections["Neighbours"] = {"Target", "empty-space"};
    context.inputs.target_collections["None"] = {};
    visual.characters[1].restore.visual_state = VisualProgramSnapshot{"program", {}, {context}};
    Check(!SameLayout(parsed, visual) && SameLayout(visual, DecodeSceneLayout(EncodeSceneLayout(visual))));
    auto changed_visual = visual;
    changed_visual.characters[1].restore.visual_state->contexts[0].variables["progress"] = 1;
    Check(!SameLayout(visual, changed_visual));
    changed_visual = visual;
    changed_visual.characters[1].restore.visual_state->contexts[0].numbers["energy"] = std::int64_t(4);
    Check(!SameLayout(visual, changed_visual)); // numeric type survives JSON persistence
    changed_visual = visual;
    changed_visual.characters[1].restore.visual_state->contexts[0].values["enabled"] = false;
    Check(!SameLayout(visual, changed_visual));
    changed_visual = visual;
    changed_visual.characters[1].restore.visual_state->contexts[0].values["weight"] = std::numeric_limits<double>::infinity();
    Reject([&] { EncodeSceneLayout(changed_visual); });
    changed_visual = visual;
    changed_visual.characters[1].restore.visual_state->contexts[0].effects = {"normal", "free", "free"};
    Check(!SameLayout(visual, changed_visual)); // resource order is not a set or a count-derived guess
    changed_visual = visual;
    changed_visual.characters[1].restore.visual_state->contexts[0].inputs.position_targets.at("empty-space").position.x = 7;
    Check(!SameLayout(visual, changed_visual));
    changed_visual = visual;
    changed_visual.characters[1].restore.visual_state->contexts[0].inputs.targets["Target"] = 999;
    Reject([&] { EncodeSceneLayout(changed_visual); });
    changed_visual = visual;
    changed_visual.characters[1].restore.visual_state->contexts.push_back(context);
    Reject([&] { EncodeSceneLayout(changed_visual); });
    changed_visual = visual;
    changed_visual.characters[1].restore.visual_state->contexts[0].variables["progress"] =
        std::numeric_limits<float>::infinity();
    Reject([&] { EncodeSceneLayout(changed_visual); });
    auto idle = visual;
    idle.characters[1].action = BattleIdle{};
    Check(SameLayout(idle, DecodeSceneLayout(EncodeSceneLayout(idle))));
    Check(std::holds_alternative<BattleIdle>(RestoreLayoutAction(idle.characters[1].action, {})->request));
    auto task_layout = visual;
    auto& task_actor = task_layout.characters[1];
    task_actor.action.reset();
    task_actor.task_catalog = "test";
    TaskSelection selection{{"test", "hold"}};
    selection.query.inputs = {{"boost", true}, {"count", std::int64_t(4)}, {"weight", 2.0}, {"mode", std::string("a")}};
    selection.visual = context.inputs;
    task_actor.restore.task_selection = selection;
    task_actor.restore.task_selection->inputs_captured = true;
    Check(SameLayout(task_layout, DecodeSceneLayout(EncodeSceneLayout(task_layout))));
    auto task_changed = task_layout;
    task_changed.characters[1].restore.task_selection->query.inputs["count"] = std::int64_t(5);
    Check(!SameLayout(task_layout, task_changed));
    task_changed = task_layout;
    task_changed.characters[1].restore.task_selection->visual.targets["Target"] = 999;
    Reject([&] { EncodeSceneLayout(task_changed); });
    task_changed = task_layout;
    task_changed.characters[1].action = BattleIdle{};
    Reject([&] { EncodeSceneLayout(task_changed); });
    task_changed = task_layout;
    task_changed.characters[1].task_catalog = "foreign";
    Reject([&] { EncodeSceneLayout(task_changed); });
    task_changed = task_layout;
    task_changed.characters[1].restore.task_selection->visual.targets["Target"] = 0;
    Check(SameLayout(task_changed, DecodeSceneLayout(EncodeSceneLayout(task_changed))));
    RemapTaskTargets(*task_changed.characters[1].restore.task_selection, {}); // explicit unbound aliases need no map entry
    RemapTaskTargets(selection, {{17, 100}, {29, 200}});
    Check(selection.visual.targets.at("Target") == 100);
    auto remapped = *visual.characters[1].restore.visual_state;
    RemapVisualTargets(remapped, {{17, 100}, {29, 200}});
    Check(remapped.contexts[0].inputs.targets.at("Target") == 100 &&
          visual.characters[1].restore.visual_state->contexts[0].inputs.targets.at("Target") == 17);
    auto instanced = visual;
    auto& instances = instanced.characters[1].restore.visual_state->contexts;
    instances[0].instance = 12;
    instances[0].binding = VisualContextBinding{"runtime_mark", "CharacterSkill", "Skill01", 17, 29};
    auto sibling_context = instances[0];
    sibling_context.instance = 13;
    instances.push_back(sibling_context);
    Check(SameLayout(instanced, DecodeSceneLayout(EncodeSceneLayout(instanced))));
    auto mapped = *instanced.characters[1].restore.visual_state;
    RemapVisualTargets(mapped, {{17, 100}, {29, 200}});
    Check(mapped.contexts[0].instance == 12 && mapped.contexts[1].instance == 13 &&
          mapped.contexts[0].binding->receiver == 100 && mapped.contexts[0].binding->caster == 200);
    auto invalid_instances = instanced;
    invalid_instances.characters[1].restore.visual_state->contexts[1].instance = 12;
    Reject([&] { EncodeSceneLayout(invalid_instances); });
    invalid_instances = instanced;
    invalid_instances.characters[1].restore.visual_state->contexts[1].binding->receiver = 999;
    Reject([&] { EncodeSceneLayout(invalid_instances); });
    auto grounded = parsed;
    grounded.characters[0].placement.snap_to_ground = false;
    grounded.characters[1].placement.snap_to_ground = true;
    Check(!SameLayout(parsed, grounded) && SameLayout(grounded, DecodeSceneLayout(EncodeSceneLayout(grounded))));
    auto bad = data;
    bad.characters[1].key = 17;
    Reject([&] { EncodeSceneLayout(bad); });
    bad = data;
    bad.characters[1].look.target = 99;
    Reject([&] { EncodeSceneLayout(bad); });
    bad = data;
    bad.characters[0].placement.position.x = std::numeric_limits<float>::infinity();
    Reject([&] { EncodeSceneLayout(bad); });
    bad = data;
    bad.characters[0].placement.rotation.w = 0;
    Reject([&] { EncodeSceneLayout(bad); });
    Reject([&] { DecodeSceneLayout("{broken"); });
    // JSON arithmetic conversions must not silently truncate/wrap identity.
    for (const auto* variant : {"1.5", "4294967295", "18446744073709551615", "true"}) {
        auto malformed = bytes;
        const auto at = malformed.find("\"variant\": -1");
        Check(at != std::string::npos);
        malformed.replace(at, 13, std::string("\"variant\": ") + variant);
        Reject([&] { DecodeSceneLayout(malformed); });
    }

    ActionCatalogStatus catalog;
    catalog.proposals.push_back({ActionDomain::Battle, "Idle", {}, {}});
    auto ready = std::make_shared<ReadyActions>();
    ready->source = "official/config";
    ReadyPose other;
    other.state = "Other";
    other.skill_name = "Skill01";
    ready->poses.push_back(other);
    ReadyPose pose;
    pose.state = "Ready";
    pose.skill_name = "Skill02";
    ready->poses.push_back(pose);
    catalog.ready_actions.push_back(ready);
    VisualState reaction;
    reaction.character_config = "official/config";
    reaction.source = "global";
    reaction.value.flag = {true, 28, {}};
    reaction.value.occur.resolved_state = {true, "Stagger", {}};
    reaction.value.loop.resolved_state = {true, "Stagger_Loop", {}};
    reaction.value.recover.resolved_state = {true, "Stagger_Recover", {}};
    reaction.value.occur.transition = reaction.value.loop.transition =
        reaction.value.recover.transition = {true, 0.15f, {}};
    reaction.expression = {true, "Emo_Stun", {}};
    reaction.expression_transition = {true, 0.2f, {}};
    auto no_face = reaction;
    no_face.value.flag.value = 6;
    no_face.expression.value.clear();
    catalog.visual_states = {reaction, no_face};
    const VisualActionKey enter_key{"official/config", 28, false}, recover_key{"official/config", 28, true};
    auto reacting = data;
    reacting.characters[1].action = enter_key;
    Check(SameLayout(reacting, DecodeSceneLayout(EncodeSceneLayout(reacting))));
    auto recovering = reacting;
    recovering.characters[1].action = recover_key;
    Check(!SameLayout(reacting, recovering));
    Check(std::get<VisualState>(RestoreLayoutAction(recover_key, catalog)->request).recover);
    // Shared body names never merge different official flags/expressions.
    Check(ResolveVisualAction(catalog, enter_key).expression.value == "Emo_Stun");
    Check(ResolveVisualAction(catalog, {"official/config", 6, false}).expression.value.empty());
    Reject([&] { ResolveVisualAction(catalog, {"another/config", 28, false}); });
    auto duplicate = catalog;
    duplicate.visual_states.push_back(reaction);
    Reject([&] { ResolveVisualAction(duplicate, enter_key); });
    auto incomplete = reaction;
    incomplete.value.loop.resolved_state.available = false;
    Check(!QueryVisualAction(incomplete, false).available && QueryVisualAction(incomplete, true).available);
    incomplete = reaction;
    incomplete.value.occur.resolved_state.value.clear();
    incomplete.value.loop.resolved_state.value.clear();
    Check(!QueryVisualAction(incomplete, false).available); // material-only is not a fake playable animation
    // A complete reaction source doesn't depend on the rest of the skill graph.
    auto reaction_pending = catalog;
    reaction_pending.pending_domains.insert(ActionDomain::Battle);
    CharacterActionCatalog reaction_view{37, reaction_pending};
    Check(std::get<VisualState>(ResolveCatalogAction(reaction_view, {37, enter_key}).request).expression.value ==
          "Emo_Stun");
    Reject([&] { ResolveCatalogAction(reaction_view, {36, enter_key}); });
    Check(std::get<VisualState>(RestoreLayoutAction(enter_key, reaction_pending)->request).value.flag.value == 28);
    hksr::api::VisualConfigSources read_source;
    read_source.character_config = "official/config";
    reaction_pending.visual_sources.push_back(read_source);
    Check(VisualActionSourceReady(reaction_pending, "official/config") &&
          !VisualActionSourceReady(reaction_pending, "other"));
    auto restored = RestoreLayoutAction(data.characters[1].action, catalog);
    Check(std::get<ReadyState>(restored->request).pose == 1); // not a persisted old array index
    ready->poses.push_back(pose);
    Reject([&] { RestoreLayoutAction(data.characters[1].action, catalog); });
    ready->poses.pop_back();
    restored = RestoreLayoutAction(SavedConfiguredAction{ActionDomain::Battle, "Idle"}, catalog);
    Check(std::get<FreeStyleState>(restored->request).name == "Idle");
    Reject([&] { RestoreLayoutAction(SavedConfiguredAction{ActionDomain::Battle, "Missing"}, catalog); });
    auto observed = data;
    observed.characters[0].action = SavedConfiguredAction{ActionDomain::Battle, "Base.OtherPhaseIdle", true};
    const auto observed_read = DecodeSceneLayout(EncodeSceneLayout(observed));
    Check(SameLayout(observed, observed_read));
    restored = RestoreLayoutAction(observed_read.characters[0].action, catalog);
    Check(std::get<FreeStyleState>(restored->request).controller_observed &&
          std::get<FreeStyleState>(restored->request).name == "Base.OtherPhaseIdle");
    auto authored = data;
    authored.characters[0].action = ProgramAction{"fixture", "second", {{"branch", "b"}}};
    const auto authored_read = DecodeSceneLayout(EncodeSceneLayout(authored));
    Check(SameLayout(authored, authored_read));
    ActionCatalogStatus pending_catalog;
    pending_catalog.pending_domains.insert(ActionDomain::Battle);
    restored = RestoreLayoutAction(authored_read.characters[0].action, pending_catalog);
    Check(restored->domain == ActionDomain::Battle && std::get<ProgramAction>(restored->request).action == "second");
    Check(std::get<ProgramAction>(restored->request).choices.at("branch") == "b");
    auto another = authored;
    std::get<ProgramAction>(*another.characters[0].action).action = "first";
    Check(!SameLayout(authored, another));
    another = authored;
    std::get<ProgramAction>(*another.characters[0].action).choices["branch"] = "a";
    Check(!SameLayout(authored, another));

    NoObjects objects;
    hksr::api::PresentationBindings bindings;
    int info = 0;
    const auto retain = +[](void*, int, void*) -> int { throw std::runtime_error("unexpected game retain"); };
    const auto target = +[](int, void*) -> void* { throw std::runtime_error("unexpected game target"); };
    const auto release = +[](int, void*) { throw std::runtime_error("unexpected game free"); };
    bindings.references = {{reinterpret_cast<void*>(retain), &info},
                           {reinterpret_cast<void*>(target), &info},
                           {reinterpret_cast<void*>(release), &info},
                           {},
                           {}};
    RuntimeServices runtime(objects, bindings);
    SceneCharacters scene(runtime.Sessions(false), SceneCharacters::Create{});
    CharacterPlan plan;
    plan.package.npc_id = 1110;
    plan.source_intent = BrowseNpc{1110};
    auto id = scene.Add(data.scene, plan, PlacedAction{ActionDomain::Exploration, NpcMotion{123}}, first.placement);
    Check(CaptureSceneLayout(scene, data.scene).characters.size() == 1);
    {
        // Saving a map projects only its own logical entries, even when the
        // registry retains other maps and a session-only development actor.
        SceneCharacters maps(runtime.Sessions(false), SceneCharacters::Create{});
        const auto a = maps.Add("map-one", plan, {}, first.placement);
        const auto b = maps.Add("map-two", plan, {}, second.placement);
        maps.SetEnabled(b, false);
        auto temporary = plan;
        temporary.persistence = CharacterPersistence::Session;
        maps.Add("map-one", temporary);
        const auto one = CaptureSceneLayout(maps, "map-one");
        const auto two = CaptureSceneLayout(maps, "map-two");
        Check(one.scene == "map-one" && one.characters.size() == 1 && one.characters.front().key == a);
        Check(two.scene == "map-two" && two.characters.size() == 1 && two.characters.front().key == b &&
              !two.characters.front().enabled);
        Check(CaptureSceneLayout(maps, "empty-map").characters.empty() && maps.Ids().size() == 3);
    }
    scene.Select(id, {ActionDomain::Exploration, AnimatorState{"IdleShow", 0, 0}});
    Check(std::get<SavedConfiguredAction>(*CaptureSceneLayout(scene, data.scene).characters[0].action).state ==
          "IdleShow");
    scene.Select(id, {ActionDomain::Battle, reaction});
    const auto reaction_layout = CaptureSceneLayout(scene, data.scene);
    Check(std::get<VisualActionKey>(*reaction_layout.characters[0].action).flag == 28);
    Check(SameLayout(reaction_layout, DecodeSceneLayout(EncodeSceneLayout(reaction_layout))));
    auto program = std::make_shared<VisualProgram>();
    program->id = "fixture";
    program->source = "official/config";
    program->actions["first"].state = program->actions["second"].state = "SameBody";
    auto input = std::make_shared<VisualProgramInput>();
    input->program = program;
    plan.visual_program = input;
    scene.ReplacePlan(id, plan);
    scene.Select(id, *restored);
    Check(scene.Status(id).program_actions.size() == 2 && scene.Status(id).visual_program == "fixture");
    Check(std::get<ProgramAction>(*CaptureSceneLayout(scene, data.scene).characters[0].action).action == "second");
    Reject([&] { scene.Select(id, {ActionDomain::Battle, ProgramAction{"foreign", "first"}}); });
    Reject([&] { scene.Select(id, {ActionDomain::Battle, ProgramAction{"fixture", "absent"}}); });
    Reject([&] { scene.Select(id, {ActionDomain::Exploration, ProgramAction{"fixture", "first"}}); });
    Check(std::get<ProgramAction>(scene.DesiredAction(id)->request).action == "second");
    // Explicit task catalogs cannot override the automatic scene of this source.
    auto tasks = std::make_shared<TaskCatalog>(ParseTaskCatalog(R"({
      "format":"hksr.script","id":"tasks","applies_to":"official/config",
      "actions":{"idle":{"run":[{"op":"animation","as":"body","state":"StandBy"}]}}})"));
    auto task_input = std::make_shared<VisualProgramInput>();
    task_input->tasks = tasks;
    task_input->program = tasks->visual;
    task_input->default_selection = false;
    scene.ConfigurePrograms({{{"official/config", input}}, {{"tasks", task_input}}});
    plan.visual_program.reset();
    plan.action_sources.push_back({ActionDomain::Battle, {hksr::api::ActionConfigKind::Battle, "official/config"}});
    auto automatic = plan;
    scene.ReplacePlan(id, automatic);
    Check(scene.Plan(id).visual_program->program == input->program && !scene.Plan(id).visual_program->tasks);
    // A complete source script supplies both task and cued actions.
    TaskCatalogInput default_task_input(*tasks);
    default_task_input.visual = input->program;
    auto default_tasks = std::make_shared<TaskCatalog>(default_task_input);
    auto default_input = std::make_shared<VisualProgramInput>();
    default_input->program = default_tasks->visual;
    default_input->tasks = default_tasks;
    default_input->default_selection = true;
    scene.ConfigurePrograms({{{"official/config", default_input}}, {{"tasks", default_input}}});
    const auto default_id = scene.Add(data.scene, automatic);
    Check(scene.Status(default_id).task_catalog == "tasks" && scene.Status(default_id).task_catalog_revision > 0 &&
          scene.Status(default_id).program_actions.size() == 2);
    scene.Select(default_id, {ActionDomain::Battle, ProgramAction{"fixture", "first"}});
    scene.RestoreTask(default_id, {{"tasks", "idle"}});
    Check(scene.Status(default_id).tasks->definitions.count("idle") == 1);
    const auto default_revision = scene.Status(default_id).task_catalog_revision;
    const auto new_revision = scene.PublishTaskCatalogs({{"tasks", default_input}});
    const auto fresh_id = scene.Add(data.scene, automatic);
    Check(new_revision > default_revision && scene.Status(fresh_id).task_catalog_revision == new_revision &&
          scene.Status(default_id).task_catalog_revision == default_revision);
    scene.Delete(fresh_id);
    scene.PublishTaskCatalogs({});
    const auto without_script = scene.Add(data.scene, automatic);
    Check(scene.Status(without_script).task_catalog.empty() && scene.Status(without_script).program_actions.empty() &&
          !scene.Plan(without_script).visual_program);
    scene.Delete(without_script);
    scene.Delete(default_id);
    scene.ConfigurePrograms({{{"official/config", input}}, {{"tasks", task_input}}});
    scene.ConfigureTaskPreview(plan, "tasks");
    Check(plan.visual_program->tasks == tasks && plan.persistence == CharacterPersistence::Session);
    auto durable = automatic;
    scene.ConfigureTaskCatalog(durable, "tasks");
    Check(durable.persistence == CharacterPersistence::Layout);
    const auto durable_id = scene.Add(data.scene, durable);
    scene.RestoreTask(durable_id, {{"tasks", "idle"}});
    auto durable_saved = CaptureSceneLayout(scene, data.scene);
    Check(durable_saved.characters.back().task_catalog == "tasks" &&
          durable_saved.characters.back().restore.task_selection->task.name == "idle");
    Check(SameLayout(durable_saved, DecodeSceneLayout(EncodeSceneLayout(durable_saved))));
    auto target_plan = automatic;
    const auto target_id = scene.Add(data.scene, target_plan);
    TaskSelection targeted{{"tasks", "idle"}};
    targeted.visual.targets["Target"] = target_id;
    targeted.inputs_captured = true;
    scene.RestoreTask(durable_id, targeted);
    scene.Delete(target_id);
    durable_saved = CaptureSceneLayout(scene, data.scene);
    Check(durable_saved.characters.back().restore.task_selection->visual.targets.at("Target") == 0);
    Check(SameLayout(durable_saved, DecodeSceneLayout(EncodeSceneLayout(durable_saved))));
    scene.Select(durable_id, {ActionDomain::Battle, BattleIdle{}});
    Check(!CaptureSceneLayout(scene, data.scene).characters.back().restore.task_selection);
    scene.Delete(durable_id);
    const auto pinned_input = plan.visual_program;
    auto replacement = std::make_shared<TaskCatalog>(*tasks);
    auto replacement_input = std::make_shared<VisualProgramInput>(*task_input);
    replacement_input->tasks = replacement;
    const auto publication = scene.PublishTaskCatalogs({{"tasks", replacement_input}});
    auto next_preview = automatic;
    scene.ConfigureTaskPreview(next_preview, "tasks");
    Check(next_preview.visual_program->tasks == replacement &&
          next_preview.visual_program->catalog_revision == publication);
    Check(plan.visual_program == pinned_input && plan.visual_program->tasks == tasks &&
          plan.visual_program->catalog_revision < publication);
    Reject([&] { scene.PublishTaskCatalogs({{"mismatch", replacement_input}}); });
    auto after_rejection = automatic;
    scene.ConfigureTaskPreview(after_rejection, "tasks");
    Check(after_rejection.visual_program == next_preview.visual_program);
    scene.PublishTaskCatalogs({});
    auto removed_preview = automatic;
    Reject([&] { scene.ConfigureTaskPreview(removed_preview, "tasks"); });
    Check(plan.visual_program->tasks == tasks); // removing publication cannot retire a live owner
    auto default_after = automatic;
    scene.ReplacePlan(id, default_after);
    Check(!scene.Plan(id).visual_program);
    TaskCatalogInput rebound(*tasks);
    auto rebound_visual = std::make_shared<VisualProgram>(*tasks->visual);
    rebound_visual->source = "new/config";
    rebound.visual = rebound_visual;
    auto rebound_input = std::make_shared<VisualProgramInput>();
    rebound_input->tasks = std::make_shared<TaskCatalog>(rebound);
    rebound_input->program = rebound_input->tasks->visual;
    const auto rebound_revision = scene.PublishTaskCatalogs({{"tasks", rebound_input}});
    auto rebound_plan = automatic;
    rebound_plan.action_sources = {{ActionDomain::Battle, {hksr::api::ActionConfigKind::Battle, "new/config"}}};
    const auto rebound_id = scene.Add(data.scene, rebound_plan);
    Check(scene.Status(rebound_id).task_catalog == "tasks" &&
          scene.Status(rebound_id).task_catalog_revision == rebound_revision &&
          plan.visual_program == pinned_input);
    scene.Delete(rebound_id);
    auto ambiguous = std::make_shared<VisualProgramInput>(*rebound_input);
    TaskCatalogInput ambiguous_catalog(*rebound_input->tasks);
    ambiguous_catalog.id = "ambiguous";
    for (auto& [name, task] : ambiguous_catalog.tasks) {
        (void)name;
        task.definition.key.catalog = ambiguous_catalog.id;
    }
    ambiguous->tasks = std::make_shared<TaskCatalog>(ambiguous_catalog);
    ambiguous->program = ambiguous->tasks->visual;
    Reject([&] { scene.PublishTaskCatalogs({{"tasks", rebound_input}, {"ambiguous", ambiguous}}); });
    auto after_ambiguity = rebound_plan;
    scene.ConfigureTaskPreview(after_ambiguity, "tasks");
    Check(after_ambiguity.visual_program->catalog_revision == rebound_revision);
    scene.PublishTaskCatalogs({});
    auto foreign = automatic;
    foreign.action_sources.clear();
    Reject([&] { scene.ConfigureTaskPreview(foreign, "tasks"); });
    Reject([&] { scene.ConfigureTaskPreview(foreign, "absent"); });
    Check(!foreign.visual_program && foreign.persistence == CharacterPersistence::Layout);
    // Omission precedes source/action/context serialization, including unsupported
    // development-only values. The rest of the map still saves without error.
    plan.source_intent.reset();
    plan.package.source = SourceKind::External;
    const auto before = EncodeSceneLayout(CaptureSceneLayout(scene, data.scene));
    const auto transient =
        scene.Add(data.scene, plan, PlacedAction{ActionDomain::Battle, AnimatorState{"Face", 1, 0}});
    Check(scene.Status(transient).transient && !scene.Status(id).transient);
    Check(EncodeSceneLayout(CaptureSceneLayout(scene, data.scene)) == before && !scene.CaptureVisualProgram(transient));
    Reject([&] { scene.Look(id, {LookMode::Instance, transient, {}}); });
    Check(EncodeSceneLayout(CaptureSceneLayout(scene, data.scene)) == before);
    scene.ReplacePlan(transient, automatic);
    Check(scene.Plan(transient).persistence == CharacterPersistence::Session);
    Check(EncodeSceneLayout(CaptureSceneLayout(scene, data.scene)) == before);
    scene.Delete(transient);
    scene.SetEnabled(id, false);
    Check(!CaptureSceneLayout(scene, data.scene).characters[0].enabled);
    scene.Delete(id);
    Check(CaptureSceneLayout(scene, data.scene).characters.empty());
    scene.Shutdown();

    {
        SceneCharacters restored_scene(runtime.Sessions(false), SceneCharacters::Create{});
        CharacterPlan restore_plan;
        restore_plan.package.npc_id = 1110;
        restore_plan.source_intent = BrowseNpc{1110};
        CharacterRestoreState initial;
        initial.playback_rate = 2;
        initial.manual_reaction = TaskVisualReaction{"official/config", 115, DeathOccurrence::Fly};
        const auto actor = restored_scene.Add(data.scene, restore_plan, {}, {}, initial);
        initial.playback_rate = 1;
        initial.manual_reaction.reset();
        const auto captured = restored_scene.CaptureRestoreState(actor);
        Check(captured.playback_rate == 2 && captured.manual_reaction->flag == 115);
        auto edited = captured;
        edited.manual_reaction->flag = 133;
        Check(restored_scene.CaptureRestoreState(actor).manual_reaction->flag == 115);
        restored_scene.SetPlaybackRate(actor, 1);
        Check(captured.playback_rate == 2 && restored_scene.CaptureRestoreState(actor).playback_rate == 1);
        restored_scene.ReplacePlan(actor, restore_plan);
        Check(!restored_scene.CaptureRestoreState(actor).manual_reaction &&
              restored_scene.CaptureRestoreState(actor).playback_rate == 1 &&
              restored_scene.Plan(actor).package.npc_id == 1110);
        restored_scene.Delete(actor);
        restored_scene.Shutdown();
    }

    const auto folder =
        std::filesystem::temp_directory_path() /
        ("hksr-layout-test-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{folder};
    LayoutStorage store(folder);
    Check(!store.Current(data.scene));
    store.SaveCurrent(data);
    Check(EncodeSceneLayout(*store.Current(data.scene)) == bytes);
    store.SaveNamed("布局一", data);
    Check(store.Names(data.scene) == std::vector<std::string>{"布局一"});
    auto other_map = data;
    other_map.scene = "hsr-map-v1:7:8";
    other_map.characters.front().placement.position.x = 123;
    store.SaveCurrent(other_map);
    store.SaveNamed("布局一", other_map);
    Check(store.Current(data.scene)->characters.front().placement.position.x == 1 &&
          store.Current(other_map.scene)->characters.front().placement.position.x == 123);
    Check(store.Named(data.scene, "布局一").characters.front().placement.position.x == 1 &&
          store.Named(other_map.scene, "布局一").characters.front().placement.position.x == 123);
    Reject([&] { store.SaveNamed("布局一", data); });
    Check(EncodeSceneLayout(store.Named(data.scene, "布局一")) == bytes); // rejected overwrite preserved prior data
    Reject([&] { store.SaveNamed("../escape", data); });
    auto changed = data;
    changed.characters[0].placement.position.x = 99;
    store.SaveCurrent(changed);
    Check(store.Current(data.scene)->characters[0].placement.position.x == 99);
    Check(store.Named(data.scene, "布局一").characters[0].placement.position.x == 1);
    store.SaveNamed("布局一", changed, true);
    Check(store.Named(data.scene, "布局一").characters[0].placement.position.x == 99);
    store.DeleteNamed(data.scene, "布局一");
    Check(store.Names(data.scene).empty());
    Check(store.Names(other_map.scene) == std::vector<std::string>{"布局一"} &&
          store.Named(other_map.scene, "布局一").characters.front().placement.position.x == 123);
    Check(!store.Current("hsr-map-v1/1/2")); // distinct map keys cannot alias through filename escaping
}
