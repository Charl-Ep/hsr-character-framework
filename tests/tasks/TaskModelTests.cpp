#include <hksr/framework/tasks/TaskContext.hpp>
#include <hksr/framework/tasks/TaskCatalog.hpp>
#include <hksr/framework/tasks/TaskSources.hpp>
#include <hksr/framework/presentation/scene/VisualProgram.hpp>
#include <limits>
#include <stdexcept>

namespace {
using namespace hksr::framework;
void Check(bool value) {
    if (!value)
        throw std::runtime_error("task model test failed");
}
template <class F> void Throws(F&& f) {
    bool threw = false;
    try {
        f();
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    Check(threw);
}
void Contexts() {
    TaskContext context;
    TaskDefinition task;
    task.key = {"actor-config", "task-a"};
    task.entry = "entry-a";
    task.inputs = {{"variant", TaskValueType::Text, TaskValue{std::string("base")}}};
    task.tags = {"attack"};
    TaskQuery query;
    Check(context.Query(task, query).availability == TaskAvailability::Available);
    const auto a = context.Enter("phase-a"), b = context.Enter("phase-b");
    context.SetValue(a, "variant", std::string("a"));
    context.SetPhase(a, "a");
    context.SetValue(b, "variant", std::string("b"));
    context.SetPhase(b, "b");
    auto result = context.Query(task, query);
    Check(result.availability == TaskAvailability::Available &&
          std::get<std::string>(result.inputs.at("variant")) == "b");
    Check(context.Values().at("variant").owner.value == b.value && context.Phase() == "b");
    Check(context.Exit(b) && !context.Exit(b));
    Check(context.Phase() == "a");
    result = context.Query(task, query);
    Check(result.availability == TaskAvailability::Available);
    query.inputs["variant"] = std::string("local");
    Check(std::get<std::string>(context.Query(task, query).inputs.at("variant")) == "local" &&
          std::get<std::string>(context.Values().at("variant").value) == "a");
    Check(context.Exit(a));
    Check(context.Query(task, query).availability == TaskAvailability::Available);
    Check(!context.Phase());
    Throws([&] { context.SetValue(b, "variant", true); });
    const auto fresh = context.Enter("phase-b");
    Check(fresh.value != b.value);
    // A second actor has its own phase/value state.
    TaskContext other;
    Check(other.Query(task, query).availability == TaskAvailability::Available);
}
void Queries() {
    TaskContext context;
    TaskDefinition task;
    task.key = {"source", "non-ready-task"};
    task.entry = "configured-sequence";
    task.inputs = {{"count", TaskValueType::Integer, {}}};
    task.requirements = {TaskValueEquals{"mode", std::string("boost")}, TaskNeedsParticipant{{"partner"}},
                         TaskPhaseEquals{"enhanced"}};
    TaskQuery query;
    auto result = context.Query(task, query);
    Check(result.availability == TaskAvailability::Unavailable &&
          result.reasons.size() == 4); // all reasons, not first gate
    query.inputs["count"] = std::int64_t{2};
    query.participants = {TaskParticipant{"partner"}};
    Check(context.Query(task, query).availability == TaskAvailability::Unavailable);
    const auto phase = context.Enter("boost");
    context.SetValue(phase, "mode", std::string("boost"));
    context.SetPhase(phase, "enhanced");
    Check(context.Query(task, query).availability == TaskAvailability::Available);
    context.SetValue(phase, "mode", std::string("base"));
    Check(context.Query(task, query).availability == TaskAvailability::Unavailable);
    context.SetValue(phase, "mode", true);
    Check(context.Query(task, query).availability == TaskAvailability::Unknown);
    query.inputs["count"] = 2.0;
    Check(context.Query(task, query).availability == TaskAvailability::Unavailable); // no numeric coercion
    query.inputs["count"] = std::int64_t{2};
    query.inputs["typo"] = true;
    Check(context.Query(task, query).availability == TaskAvailability::Unavailable);
    query.inputs.erase("typo");
    task.requirements.clear();
    task.entry.clear();
    Check(context.Query(task, query).availability == TaskAvailability::Unknown);
    task.entry = "entry";
    task.requirements = {TaskUnresolvedRequirement{"config.condition", "not qualified"}};
    Check(context.Query(task, query).availability == TaskAvailability::Unknown);
    task.inputs[0].default_value = std::string("wrong");
    Throws([&] { ValidateTaskDefinition(task); });
    task.inputs.clear();
    task.requirements.clear();
    task.inputs = {{"number", TaskValueType::Number, {}}};
    query.inputs.clear();
    query.inputs["number"] = std::numeric_limits<double>::quiet_NaN();
    Check(context.Query(task, query).availability == TaskAvailability::Unavailable);
}
void Sources() {
    hksr::api::TaskConfigInput input;
    input.actions.path = "character-a";
    input.actions.loaded = true;
    input.actions.skills.enumerated = input.skill_abilities.enumerated = true;
    hksr::api::SkillTaskInput first;
    first.skill.source_index = 0;
    first.skill.name = {true, "SharedName", {}};
    first.skill.entry_ability = {true, "Passive_Entry", {}};
    first.skill.ready_state = {true, "", {}};
    auto second = first;
    second.skill.source_index = 4;
    second.skill.entry_ability.value = "Inserted_Entry";
    input.skills = {first, second};
    hksr::api::SkillAbilityInput group;
    group.source_index = 3;
    group.skill = {true, "SharedName", {}};
    group.abilities = {true, {"Installed"}, {}};
    auto missing = group;
    missing.source_index = 7;
    missing.skill.value = "Unmatched";
    auto unread = group;
    unread.source_index = 9;
    unread.skill.available = false;
    input.skill_abilities.rows = {group, missing, unread};
    const auto inventory = BuildTaskSources(input);
    Check(inventory.loaded && inventory.skills.size() == 2 && inventory.groups.size() == 3 &&
          inventory.issues.size() == 3);
    Check(inventory.skills[0].entries.size() == 1 && inventory.skills[0].entries[0].ability == "Passive_Entry");
    Check(inventory.skills[1].entries[0].source.field == "SkillList[4].EntryAbility");
    Check(inventory.groups[0].skill_indices == std::vector<std::int32_t>({0, 4}));
    Check(inventory.groups[1].skill_indices.empty() && inventory.groups[2].input.abilities.values[0] == "Installed");
}
void InputChoices() {
    TaskContext context;
    TaskDefinition task;
    task.key = {"scripts", "callback"};
    task.entry = "callback";
    TaskInputDefinition input{"event", TaskValueType::Integer, TaskValue{std::int64_t(0)}};
    input.label = "Event";
    input.description = "Select a visual callback";
    input.choices = {{std::int64_t(0), "On hit"}, {std::int64_t(1), "On exit"}};
    task.inputs = {input};
    ValidateTaskDefinition(task);
    Check(context.Query(task, {}).availability == TaskAvailability::Available);
    TaskQuery selected;
    selected.inputs["event"] = std::int64_t(1);
    Check(context.Query(task, selected).availability == TaskAvailability::Available);
    selected.inputs["event"] = std::int64_t(2);
    Check(context.Query(task, selected).availability == TaskAvailability::Unavailable);
    selected.inputs["event"] = 1.0;
    Check(context.Query(task, selected).availability == TaskAvailability::Unavailable);
    task.inputs[0].choices.push_back({std::int64_t(1), "Duplicate"});
    Throws([&] { ValidateTaskDefinition(task); });
    task.inputs[0].choices.pop_back();
    task.inputs[0].default_value = std::int64_t(3);
    Throws([&] { ValidateTaskDefinition(task); });
    task.inputs[0].default_value = std::int64_t(0);
    task.inputs[0].choices[1].value = std::string("1");
    Throws([&] { ValidateTaskDefinition(task); });
}
void Numbers() {
    using I = std::int64_t;
    using Op = ContextNumberOperation;
    using Cmp = ContextNumberComparison;
    const ContextNumberRule rule{I(0), I(0), I(8)};
    Check(ChangeContextNumber(I(7), {"charge", Op::Add, I(4)}, rule) == ContextNumber(I(8)));
    Check(ChangeContextNumber(I(7), {"charge", Op::Add, I(-20)}, rule) == ContextNumber(I(0)));
    Check(ChangeContextNumber(I(2), {"charge", Op::Multiply, I(3)}, rule) == ContextNumber(I(6)));
    Check(CompareContextNumbers(I(9007199254740993LL), I(9007199254740992LL), Cmp::Greater));
    Throws([&] { ChangeContextNumber(I(0), {"charge", Op::Add, 1.0}, rule); });
    Throws([&] { CompareContextNumbers(1.0, I(1), Cmp::Equal); });
    bool overflow = false;
    try {
        ChangeContextNumber((std::numeric_limits<I>::max)(), {"n", Op::Add, I(1)}, {I(0)});
    } catch (const std::overflow_error&) {
        overflow = true;
    }
    Check(overflow);
    overflow = false;
    try {
        ChangeContextNumber((std::numeric_limits<I>::min)(), {"n", Op::Multiply, I(-1)}, {I(0)});
    } catch (const std::overflow_error&) {
        overflow = true;
    }
    Check(overflow);
    TaskContext state;
    auto token = state.Enter("owned");
    state.SetValue(token, "n", I(4));
    Check(state.Evaluate(TaskNumberCompare{"n", I(4), Cmp::GreaterEqual}, {}).availability ==
          TaskAvailability::Available);
    Check(state.Evaluate(TaskNumberCompare{"n", I(4), Cmp::Greater}, {}).availability == TaskAvailability::Unavailable);
    state.Exit(token);
    Check(state.Evaluate(TaskNumberCompare{"n", I(4), Cmp::Greater}, {}).availability == TaskAvailability::Unknown);
}
void ExplicitContextValues() {
    TaskContext state;
    TaskQuery inputs;
    inputs.inputs = {{"enabled", false}, {"label", std::string("captured")}, {"n", std::int64_t(1)}};
    const auto older = state.Enter("mode", "catalog");
    state.SetValue(older, "enabled", true);
    state.SetValue(older, "label", std::string("live"));
    state.SetValue(older, "n", std::int64_t(8));
    const TaskValueContext source{"mode", "catalog"};
    Check(state.Evaluate(TaskValueEquals{"enabled", true, source}, inputs).availability == TaskAvailability::Available);
    Check(state.Evaluate(TaskValueEquals{"label", std::string("live"), source}, inputs).availability ==
          TaskAvailability::Available);
    Check(state.Evaluate(TaskNumberCompare{"n", std::int64_t(8), ContextNumberComparison::Equal, source}, inputs)
              .availability == TaskAvailability::Available);
    Check(state.Evaluate(TaskValueEquals{"enabled", false}, inputs).availability == TaskAvailability::Available);
    state.SetValue(older, "enabled", false);
    Check(state.Evaluate(TaskValueEquals{"enabled", true, source}, inputs).availability ==
          TaskAvailability::Unavailable);
    const auto foreign = state.Enter("mode", "foreign");
    state.SetValue(foreign, "enabled", true);
    Check(state.Evaluate(TaskValueEquals{"enabled", false, source}, inputs).availability ==
          TaskAvailability::Available);
    const auto newer = state.Enter("mode", "catalog");
    state.SetValue(newer, "enabled", true);
    Check(state.Evaluate(TaskValueEquals{"enabled", true, source}, inputs).availability == TaskAvailability::Available);
    Check(state.Evaluate(TaskValueEquals{"label", std::string("live"), source}, inputs).availability ==
          TaskAvailability::Unknown);
    state.Exit(newer);
    state.Exit(older);
    Check(state.Evaluate(TaskValueEquals{"enabled", false, source}, inputs).availability == TaskAvailability::Unknown);
}
void RuntimeTracking() {
    {
        TaskContext live;
        const auto token = live.Enter("external");
        live.SetValue(token, "choice", std::int64_t(1));
        const TaskValueEquals input{"choice", std::int64_t(1), {}, true};
        Check(live.Evaluate(input, {}).availability == TaskAvailability::Unknown);
        TaskQuery captured;
        captured.inputs["choice"] = std::int64_t(1);
        live.SetValue(token, "choice", std::int64_t(2));
        Check(live.Evaluate(input, captured).availability == TaskAvailability::Available);
    }
    TaskCatalogInput input;
    input.id = "tracking";
    auto visual = std::make_shared<VisualProgram>();
    visual->id = input.id;
    visual->source = "actor-config";
    visual->scope = input.id;
    input.visual = visual;
    for (const auto& [name, policy] : std::vector<std::pair<std::string, TaskRuntimePolicy>>{
             {"ready", TaskRuntimePolicy::Ready}, {"perform", TaskRuntimePolicy::Performance},
             {"continuous", TaskRuntimePolicy::Continuous}, {"setting", TaskRuntimePolicy::Setting}}) {
        TaskDefinition definition;
        definition.key = {input.id, name};
        definition.runtime = TaskRuntimeTracking{std::string("Skill01"), policy};
        input.tasks[name] = {definition, {}};
    }
    const TaskCatalog catalog(input);
    Check(input.contexts.empty() && visual->contexts.empty()); // detached preparation
    Check(catalog.contexts.size() == 1 && catalog.contexts.at(TaskPlaybackContext).state_only);
    Check(catalog.initial_contexts == std::vector<std::string>{TaskPlaybackContext});
    Check(catalog.visual->contexts.at(TaskPlaybackContext).restore.has_value());
    const auto& ready = catalog.tasks.at("ready").definition.on;
    const auto& performance = catalog.tasks.at("perform").definition.on;
    const auto& continuous = catalog.tasks.at("continuous").definition.on;
    const auto& setting = catalog.tasks.at("setting").definition.on;
    Check(ready.started.empty() && ready.completed.size() == 1 && ready.interrupted.size() == 1);
    Check(performance.started.size() == 1 && performance.completed.size() == 1 && performance.replaced.size() == 1);
    Check(continuous.started.size() == 1 && continuous.completed.empty() && continuous.interrupted.size() == 1);
    Check(setting.started.empty() && setting.completed.empty() && setting.interrupted.empty() &&
          setting.replaced.empty() && setting.failed.size() == 1);
    for (const auto& [name, plan] : catalog.tasks) {
        (void)name;
        Check(plan.definition.on.accepted.front().values.at("selected") == TaskValue{std::string("Skill01")});
        Check(plan.definition.on.failed.front().values.at("performance") == TaskValue{std::string("failed")});
    }
    const TaskCatalog republished{TaskCatalogInput(catalog)};
    Check(republished.initial_contexts == catalog.initial_contexts);
    Check(republished.tasks.at("perform").definition.on.started.size() == 1);
    Check(republished.tasks.at("ready").definition.on.accepted.size() == 1);
    auto untracked = TaskCatalogInput(catalog);
    for (auto& [name, plan] : untracked.tasks) {
        (void)name;
        plan.definition.runtime.reset();
    }
    const TaskCatalog without_policy(untracked);
    Check(without_policy.contexts.empty() && without_policy.visual->contexts.empty() &&
          without_policy.initial_contexts.empty());
    Check(without_policy.tasks.at("perform").definition.on.accepted.empty() &&
          without_policy.tasks.at("perform").definition.on.started.empty());
    input.tasks.at("continuous").definition.runtime->selected.reset();
    const TaskCatalog preserves_selection(input);
    Check(preserves_selection.tasks.at("continuous").definition.on.accepted.front().values.count("selected") == 0);
    // A runtime predicate is available even when this script has no tracking
    // producer; initial playback information is ready with no selected skill.
    TaskCatalogInput reader;
    reader.id = "reader";
    reader.visual = visual;
    TaskDefinition definition;
    definition.key = {reader.id, "read"};
    definition.requirements = {
        TaskValueEquals{"performance", std::string("ready"), TaskValueContext{TaskPlaybackContext, reader.id}}};
    reader.tasks["read"] = {definition, {}};
    Check(TaskCatalog(reader).contexts.count(TaskPlaybackContext) == 1);
}
}
void TestTaskModel() {
    Contexts();
    Queries();
    Sources();
    Numbers();
    InputChoices();
    ExplicitContextValues();
    RuntimeTracking();
}
