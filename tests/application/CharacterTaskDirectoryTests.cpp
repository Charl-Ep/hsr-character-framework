#include "../support/LegacyScriptFixture.hpp"
#include "../../src/application/ReactionDisplayComparison.hpp"
#include <hksr/framework/application/CharacterTasks.hpp>
#include <hksr/framework/tasks/TaskCatalog.hpp>
namespace api = hksr::api;
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
using namespace hksr::framework;
using Json = nlohmann::json;
void Check(bool value) {
    if (!value)
        throw std::runtime_error("character task directory test failed");
}
void Scripts() {
    const auto document = Json::parse(R"({
      "format":"hksr.task-catalog","id":"directory","domain":"battle",
      "visual":{"id":"scene","source":"fixture","scope":"scope","effects":{"fx":{"path":"Effects/Fx.prefab","target":"Caster"}},
        "contexts":{"fx":{"restore":[]},"data":{"restore":[]}}},
      "contexts":{"fx":{"callbacks":{"enter":"show","exit":"hide"}},"data":{"state_only":true,"values":{"performance_count":1}}},
      "sequences":{
        "empty":{"entry":{"op":"omitted","request":"missing","reason":"not implemented"},
          "interruption":{"op":"animation","request":"cleanup","participant":"body","state":"StandBy"}},
        "cleanup":{"entry":{"op":"animation","request":"return","participant":"body","state":"StandBy","current_idle":true}},
        "empty_return":{"entry":{"op":"call_sequence","sequence":"cleanup","source":"arbitrary provenance"}},
        "partial":{"entry":{"op":"serial","children":[{"op":"omitted","request":"missing","reason":"not implemented"},
          {"op":"branch","condition":{"op":"value","name":"optional","is":true},
           "yes":{"op":"sound","request":"sound","participant":"body","event":"Actor_SFX"},"no":{"op":"wait_frame"}}]}},
        "show":{"entry":{"op":"visual","request":"fx","instruction":{"op":"create","effects":["fx"],"source":"OnCreate"}}},
        "hide":{"entry":{"op":"visual","request":"hide","instruction":{"op":"remove","effects":["fx"],"source":"OnDestroy"}}},
        "enter_fx":{"entry":{"op":"context_enter","request":"enter","name":"fx"}},
        "data":{"entry":{"op":"context_enter","request":"data","name":"data"}}
      },
      "tasks":{"empty":{"entry":"empty"},"empty_return":{"entry":"empty_return"},"partial":{"entry":"partial"},
        "enter_fx":{"entry":"enter_fx"},"data":{"entry":"data"},
        "phase_reset":{"entry":"enter_fx","tags":["visual-phase-reset"]}}
    })");
    const auto parsed = std::make_shared<const TaskCatalog>(ParseLegacyTaskCatalog(document.dump()));
    Check(!parsed->Prepared("empty").has_presentation);
    Check(!parsed->Prepared("empty_return").has_presentation);
    Check(parsed->Prepared("partial").has_presentation);
    Check(parsed->Prepared("enter_fx").has_presentation);
    Check(!parsed->Prepared("data").has_presentation);
    TaskCatalogInput with_idle(*parsed);
    with_idle.tasks.at("empty_return").definition.tags = {"script-idle"};
    const TaskCatalog idle(with_idle);
    Check(idle.Prepared("empty_return").has_presentation);
    auto tasks = std::make_shared<CharacterTaskStatus>();
    tasks->catalog = parsed;
    tasks->domain = parsed->domain;
    for (const auto& row : parsed->tasks) {
        tasks->definitions.emplace(row.first, row.second.definition);
        tasks->control.catalog.emplace(row.first, TaskQueryResult{});
    }
    ApplicationSnapshot snapshot;
    SceneCharacterStatus actor;
    actor.id = 41;
    actor.active = actor.enabled = true;
    actor.creations = 1;
    actor.tasks = tasks;
    actor.task_catalog = parsed->id;
    snapshot.characters.push_back(actor);
    const auto directory = CharacterTasks(snapshot, 41);
    auto entry = [&](const char* name) -> const CharacterTaskEntry& {
        const auto found =
            std::find_if(directory.begin(), directory.end(), [&](const auto& row) { return row.label == name; });
        Check(found != directory.end());
        return *found;
    };
    Check(entry("empty").diagnostic && entry("empty").query.availability == TaskAvailability::Available);
    Check(entry("data").diagnostic && entry("empty_return").diagnostic);
    Check(!entry("partial").diagnostic && !entry("enter_fx").diagnostic);
    Check(entry("phase_reset").query.availability == TaskAvailability::Unavailable);
    snapshot.characters[0].visual_phase_override = true;
    const auto phased = CharacterTasks(snapshot, 41);
    const auto reset =
        std::find_if(phased.begin(), phased.end(), [](const auto& row) { return row.label == "phase_reset"; });
    Check(reset != phased.end() && reset->query.availability == TaskAvailability::Available);
}
void Reactions() {
    ApplicationSnapshot snapshot;
    SceneCharacterStatus actor;
    actor.id = 42;
    actor.creations = 7;
    actor.active = actor.enabled = true;
    snapshot.characters.push_back(actor);
    auto catalog = std::make_shared<CatalogSnapshot>();
    snapshot.catalog = catalog;
    auto actions = std::make_shared<CharacterActionCatalog>();
    catalog->characters.emplace(42, actions);
    auto visual = [](int flag, std::string body = "Stagger", std::string face = "Emo_Stun") {
        VisualState row;
        row.character_config = "Body";
        row.source = "Global";
        row.value.flag = {true, flag, {}};
        row.value.occur.state = row.value.occur.resolved_state = {true, body, {}};
        row.value.loop.resolved_state = {true, body + "_Loop", {}};
        row.value.recover.resolved_state = {true, body + "_Recover", {}};
        row.value.occur.transition = row.value.loop.transition = row.value.recover.transition = {true, .15f, {}};
        row.expression = {true, face, {}};
        row.expression_transition = {true, .2f, {}};
        row.value.force_hit_h = {true, false, {}};
        row.value.repeat_occur_at = {true, -1, {}};
        return row;
    };
    auto ordinary = visual(115), duplicate = visual(147), breakage = visual(28), forced = visual(133);
    breakage.value.reactions.present = true;
    breakage.value.reactions.hit_h = {true, "Stagger", {}};
    forced.value.force_hit_h = {true, true, {}};
    forced.value.reactions.present = true;
    forced.value.reactions.hit_h = {true, "Hit_H_Break", {}};
    actions->actions.visual_states = {ordinary,
                                      duplicate,
                                      breakage,
                                      forced,
                                      visual(106, "Stun"),
                                      visual(122, "Die", "Emo_Dead"),
                                      visual(132, "Confine", "")};
    auto values = std::make_shared<CharacterControllerActions>();
    values->creation = 7;
    values->checked_domain = ActionDomain::Battle;
    values->checked_model_epoch = 1;
    for (const auto* body : {"Stagger", "Stun", "Die", "Confine"})
        for (const auto* suffix : {"", "_Loop", "_Recover"})
            values->checked_states[{0, std::string(body) + suffix}] = {std::string(body) != "Confine", {true, 0}};
    snapshot.controller_actions.emplace(42, values);
    auto rows = CharacterTasks(snapshot, 42);
    auto lookup = [&](const std::string& label) -> const CharacterTaskEntry& {
        const auto found = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.label == label; });
        Check(found != rows.end());
        return *found;
    };
    const auto& stagger = lookup("Stagger · Emo_Stun");
    Check(stagger.variants.size() == 3 && stagger.sources.size() == 4);
    Check(stagger.query.availability == TaskAvailability::Available && HasTaskVariants(stagger));
    Check(lookup("Stagger_Recover · Emo_Stun").variants.size() == 1);
    Check(lookup("Stagger_Recover · Emo_Stun").query.availability == TaskAvailability::Unavailable);
    auto tasks = std::make_shared<CharacterTaskStatus>();
    TaskContextView active;
    active.name = "visual-reaction";
    active.visual_reaction = TaskVisualReaction{"Body", 133};
    tasks->control.contexts.push_back(active);
    snapshot.characters[0].tasks = tasks;
    rows = CharacterTasks(snapshot, 42);
    Check(lookup("Stagger_Recover · Emo_Stun").query.availability == TaskAvailability::Unavailable);
    tasks->control.manual_reaction = active.visual_reaction;
    rows = CharacterTasks(snapshot, 42);
    const auto& recovery = lookup("Stagger_Recover · Emo_Stun");
    const auto& recovery_key = std::get<VisualActionKey>(std::get<SelectCatalogAction>(recovery.command).action.key);
    Check(recovery.query.availability == TaskAvailability::Available && recovery_key.flag == 133 &&
          recovery_key.recover && recovery_key.character_config == "Body");
    Check(recovery.variants.size() == 1 &&
          std::get<VisualActionKey>(std::get<SelectCatalogAction>(recovery.variants.front().command).action.key).flag ==
              133);
    tasks->control.contexts.back().visual_reaction = TaskVisualReaction{"Body", 106};
    rows = CharacterTasks(snapshot, 42);
    Check(lookup("Stagger_Recover · Emo_Stun").query.availability == TaskAvailability::Available);
    Check(lookup("Stun · Emo_Stun").query.availability == TaskAvailability::Available);
    Check(lookup("Die · Emo_Dead").query.availability == TaskAvailability::Available);
    Check(lookup("Confine").query.availability == TaskAvailability::Unavailable);
    values->checked_domain = ActionDomain::Exploration;
    rows = CharacterTasks(snapshot, 42);
    Check(lookup("Confine").query.availability == TaskAvailability::Unknown);
    values->checked_domain = ActionDomain::Battle;
    values->creation = 6;
    rows = CharacterTasks(snapshot, 42);
    Check(lookup("Stagger · Emo_Stun").query.availability == TaskAvailability::Unknown);
}
void DeathVariants() {
    ApplicationSnapshot snapshot;
    SceneCharacterStatus actor;
    actor.id = 43;
    actor.creations = 8;
    actor.active = actor.enabled = true;
    snapshot.characters.push_back(actor);
    auto catalog = std::make_shared<CatalogSnapshot>();
    snapshot.catalog = catalog;
    auto actions = std::make_shared<CharacterActionCatalog>();
    catalog->characters.emplace(43, actions);
    VisualState death;
    death.character_config = "DeathBody";
    death.source = "Global";
    death.value.flag = {true, 122, {}};
    death.value.occur.category = {true, 3, {}};
    death.value.occur.source_category = {true, 0, {}};
    death.value.occur.state = death.value.occur.resolved_state = {true, "DefaultDeath", {}};
    death.value.loop.resolved_state = {true, "DeathLoop", {}};
    death.value.recover.resolved_state = {true, "Revive", {}};
    death.value.occur.transition = death.value.loop.transition = death.value.recover.transition = {true, .1f, {}};
    death.expression = {true, "Emo_Dead", {}};
    death.expression_transition = {true, .2f, {}};
    actions->actions.visual_states.push_back(death);
    api::VisualReactionInput names;
    names.present = true;
    names.die = {true, "ConfiguredDie", {}};
    names.die_hit = {true, "ConfiguredHit", {}};
    names.die_fly = {true, "ConfiguredFly", {}};
    names.die_limbo = {true, "ConfiguredLimbo", {}};
    actions->death_names.emplace("DeathBody", names);
    auto values = std::make_shared<CharacterControllerActions>();
    values->creation = 8;
    values->checked_domain = ActionDomain::Battle;
    for (const auto* state :
         {"DefaultDeath", "DeathLoop", "Revive", "ConfiguredDie", "ConfiguredHit", "ConfiguredLimbo"})
        values->checked_states[{0, state}] = {true, {true, 0}};
    values->checked_states[{0, "ConfiguredFly"}] = {false, {true, 0}};
    snapshot.controller_actions.emplace(43, values);
    auto rows = CharacterTasks(snapshot, 43);
    auto entry = [&]() -> const CharacterTaskEntry& {
        const auto found = std::find_if(rows.begin(), rows.end(),
                                        [](const auto& row) { return row.label == "DefaultDeath · Emo_Dead"; });
        Check(found != rows.end());
        return *found;
    };
    Check(entry().variants.size() == 4); // default plus three slots actually present on this model
    for (const auto& variant : entry().variants) {
        const auto& key = std::get<VisualActionKey>(std::get<SelectCatalogAction>(variant.command).action.key);
        Check(key.occurrence != DeathOccurrence::Fly && variant.query.availability == TaskAvailability::Available);
        const auto resolved = ResolveVisualAction(actions->actions, key, &names);
        Check(resolved.value.loop.resolved_state.value == "DeathLoop" &&
              resolved.value.recover.resolved_state.value == "Revive" && resolved.expression.value == "Emo_Dead");
        Check(key.occurrence == DeathOccurrence::Configured ||
              (resolved.death_defaults && resolved.death_defaults->die_fly.value == "ConfiguredFly"));
    }
    // A missing cached state is unknown, never promoted to a verified success.
    values->checked_states.erase({0, "ConfiguredLimbo"});
    rows = CharacterTasks(snapshot, 43);
    const auto limbo = std::find_if(entry().variants.begin(), entry().variants.end(),
                                    [](const auto& row) { return row.label == "ConfiguredLimbo"; });
    Check(limbo != entry().variants.end() && limbo->query.availability == TaskAvailability::Unknown);
    values->checked_states[{0, "DefaultDeath"}] = {false, {true, 0}};
    rows = CharacterTasks(snapshot, 43);
    Check(entry().variants.size() == 3 && entry().query.availability == TaskAvailability::Available);
    for (const auto& variant : entry().variants)
        Check(std::get<VisualActionKey>(std::get<SelectCatalogAction>(variant.command).action.key).occurrence !=
              DeathOccurrence::Configured);
    // The active phase may supply another name: base-domain negatives cannot
    // suppress the choice before the existing runtime phase resolver runs.
    snapshot.characters[0].visual_phase_override = true;
    rows = CharacterTasks(snapshot, 43);
    Check(entry().variants.size() == 5 && entry().query.availability == TaskAvailability::Unknown);
    for (const auto& variant : entry().variants)
        Check(variant.query.availability == TaskAvailability::Unknown);
    // Override precedence is explicit row, phase, then the official defaults.
    auto selected = death;
    api::VisualReactionInput phase;
    phase.present = true;
    phase.die_fly = {true, "PhaseFly", {}};
    ApplyDeathOccurrence(selected, DeathOccurrence::Fly, names, &phase);
    Check(selected.value.occur.resolved_state.value == "PhaseFly");
    selected = death;
    selected.value.reactions.present = true;
    selected.value.reactions.die_fly = {true, "RowFly", {}};
    ApplyDeathOccurrence(selected, DeathOccurrence::Fly, names, &phase);
    Check(selected.value.occur.resolved_state.value == "RowFly");
}
}
void TestCharacterTaskDirectory() {
    Scripts();
    Reactions();
    DeathVariants();
}
