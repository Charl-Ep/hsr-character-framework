#include "../../src/ui/ActionMenuState.hpp"
#include <stdexcept>

void TestTaskInputDraft() {
    using namespace hksr::framework;
    auto check = [](bool value) {
        if (!value)
            throw std::runtime_error("task input draft");
    };
    CharacterTaskEntry entry;
    entry.kind = CharacterTaskKind::Sequence;
    entry.inputs = {{"count", TaskValueType::Integer, {}}};
    entry.bindings.targets = {"Target"};
    entry.command = ControlCharacterTask{7, 3, {TaskControlOperation::Select, TaskSelection{{"test", "attack"}}}};
    entry.query = {TaskAvailability::Unavailable, {}, {{TaskReasonKind::MissingInput, "Target", "absent"}}};
    check(CanConfigureTask(entry));
    entry.query.reasons.push_back({TaskReasonKind::Condition, "phase", "not enhanced"});
    check(!CanConfigureTask(entry));
    entry.query.reasons.pop_back();
    entry.query.reasons.push_back({TaskReasonKind::Unimplemented, "entry", "missing"});
    check(!CanConfigureTask(entry));
    entry.query.reasons.pop_back();
    ui::TaskInputDraft draft(entry);
    ApplicationService host;
    draft.Query(host);
    draft.Query(host);
    auto batch = host.TakeCommands(false);
    check(batch.size() == 1);
    check(std::get<ControlCharacterTask>(batch.front().command).input.operation == TaskControlOperation::Query);
    check(!draft.Ready());
    draft.Selection().query.inputs["count"] = std::int64_t(2);
    draft.Changed();
    CommandReply reply;
    reply.serial = batch.front().serial;
    reply.accepted = true;
    reply.task.emplace();
    reply.task->selection = TaskSelectionResult{0, {"test", "attack"}, {}};
    check(draft.Reply(reply) && !draft.Ready()); // stale response cannot authorize an edited selection
    draft.Query(host);
    batch = host.TakeCommands(false);
    check(batch.size() == 1);
    reply.serial = batch.front().serial;
    check(draft.Reply(reply) && draft.Ready());
    check(!draft.Reply(reply));
    ApplicationSnapshot snapshot;
    SceneCharacterStatus actor;
    actor.id = 7;
    actor.creations = 3;
    actor.active = actor.enabled = true;
    actor.scene = "map";
    snapshot.characters.push_back(actor);
    draft.Selection().visual.targets["Target"] = 7;
    draft.Observe(snapshot, actor);
    check(!draft.Ready());
    const auto revision = draft.revision;
    draft.Observe(snapshot, actor);
    check(draft.revision == revision);
    snapshot.characters.front().creations = 4;
    draft.Observe(snapshot, actor);
    check(draft.revision > revision);
    draft.Query(host);
    batch = host.TakeCommands(false);
    reply.serial = batch.front().serial;
    reply.accepted = false;
    reply.error = "target no longer exists";
    check(draft.Reply(reply) && !draft.Ready() && !draft.error.empty());
    check(std::get<std::int64_t>(draft.Selection().query.inputs.at("count")) == 2);
    check(!draft.Selection().inputs_captured); // effective defaults remain the engine-frame query's responsibility
    auto tasks = std::make_shared<CharacterTaskStatus>();
    actor.tasks = tasks;
    draft.Observe(snapshot, actor);
    auto changed = std::make_shared<CharacterTaskStatus>(*tasks);
    actor.tasks = changed;
    TaskContextView context;
    context.token = {17};
    context.name = "appearance";
    context.values["charge"] = std::int64_t(1);
    changed->control.contexts.push_back(context);
    draft.Observe(snapshot, actor);
    draft.Query(host);
    batch = host.TakeCommands(false);
    reply.serial = batch.front().serial;
    reply.accepted = true;
    const auto before_value = draft.revision;
    changed->control.contexts.front().values["charge"] = std::int64_t(4);
    draft.Observe(snapshot, actor);
    check(draft.revision > before_value);
    check(draft.Reply(reply) && !draft.Ready()); // same owner, changed input: old answer is stale
    const auto stable = draft.revision;
    draft.Observe(snapshot, actor);
    check(draft.revision == stable);
    changed->control.contexts.front().phase = "enhanced";
    draft.Observe(snapshot, actor);
    check(draft.revision > stable);
    draft.Query(host);
    batch = host.TakeCommands(false);
    reply.serial = batch.front().serial;
    check(draft.Reply(reply) && draft.Ready());
    check(std::get<std::int64_t>(draft.Selection().query.inputs.at("count")) == 2); // keep explicit user edits
    draft.CollectionMember("recipients", 7, true);
    draft.CollectionMember("recipients", 7, true);
    check(draft.Selection().visual.target_collections.at("recipients").size() == 1);
    check(draft.Selection().visual.targets.at("@collection:recipients:7") == 7);
    draft.CollectionMember("recipients", 7, false);
    check(draft.Selection().visual.target_collections.at("recipients").empty()); // explicit empty != default
    check(!draft.Selection().visual.targets.count("@collection:recipients:7"));
    draft.CollectionMember("recipients", 7, true);
    draft.DefaultCollection("recipients");
    check(!draft.Selection().visual.target_collections.count("recipients"));
    check(!draft.Selection().visual.targets.count("@collection:recipients:7"));
    check(draft.Selection().visual.targets.at("Target") == 7); // unrelated binding survives

    ui::TaskInputSettings settings;
    {
        auto fixed = entry;
        fixed.bindings = {};
        TaskInputDefinition field;
        field.name = "constant";
        field.type = TaskValueType::Boolean;
        field.default_value = true;
        field.choices = {{true, "Only value"}};
        fixed.inputs = {field};
        check(!ui::HasEditableTaskInputs(fixed, false) && !ui::HasEditableTaskInputs(fixed, true));
        fixed.inputs[0].choices.push_back({false, "Other value"});
        check(ui::HasEditableTaskInputs(fixed, false));
        fixed.inputs[0].diagnostic = true;
        check(!ui::HasEditableTaskInputs(fixed, false) && ui::HasEditableTaskInputs(fixed, true));
        fixed.inputs[0].diagnostic = false;
        fixed.inputs[0].choices[1].diagnostic = true;
        check(ui::TaskInputChoiceCount(fixed.inputs[0], false) == 1 &&
              ui::TaskInputChoiceCount(fixed.inputs[0], true) == 2);
        check(!ui::TaskInputChoiceVisible(fixed.inputs[0].choices[1], false));
        check(ui::TaskInputChoiceVisible(fixed.inputs[0].choices[1], true));
        check(!ui::HasEditableTaskInputs(fixed, false) && ui::HasEditableTaskInputs(fixed, true));
        check(fixed.inputs[0].default_value ==
              TaskValue(true)); // view filtering cannot silently rewrite the selected value
    }
    settings.Remember(draft.command);
    const auto configured = settings.Apply(entry);
    check(ui::ActionRowIdentity(configured) == ui::ActionRowIdentity(entry));
    const auto& configured_command = std::get<ControlCharacterTask>(configured.command);
    check(configured_command.input.operation == TaskControlOperation::Select);
    check(configured_command.input.selection->visual.targets.at("Target") == 7);
    check(std::get<std::int64_t>(configured_command.input.selection->query.inputs.at("count")) == 2);
    check(!configured_command.input.selection->inputs_captured);
    check(configured.query.availability == entry.query.availability); // no cached authorization
    host.Post(configured.command);
    batch = host.TakeCommands(false);
    check(batch.size() == 1);
    const auto& run = std::get<ControlCharacterTask>(batch.front().command);
    check(run.input.operation == TaskControlOperation::Select && run.input.selection->visual.targets.at("Target") == 7);
    check(std::get<std::int64_t>(run.input.selection->query.inputs.at("count")) ==
          2); // action row submits the saved values
    ui::TaskInputDraft reopened(configured);
    check(reopened.CatalogMatches(2) && reopened.CatalogMatches(2));
    check(!reopened.CatalogMatches(3)); // hot-replaced schemas must not retain an open old editor
    check(std::get<std::int64_t>(reopened.Selection().query.inputs.at("count")) == 2);
    reopened.Selection().query.inputs.erase("count");
    settings.Remember(reopened.command);
    check(!std::get<ControlCharacterTask>(settings.Apply(entry).command).input.selection->query.inputs.count("count"));
    auto rebuilt = entry;
    std::get<ControlCharacterTask>(rebuilt.command).creation++;
    check(std::get<ControlCharacterTask>(settings.Apply(rebuilt).command).input.selection->visual.targets.empty());
    auto other = entry;
    std::get<ControlCharacterTask>(other.command).id++;
    check(std::get<ControlCharacterTask>(settings.Apply(other).command).input.selection->visual.targets.empty());
    other = entry;
    std::get<ControlCharacterTask>(other.command).input.selection->task.name = "other";
    check(std::get<ControlCharacterTask>(settings.Apply(other).command).input.selection->visual.targets.empty());

    using ui::ActionView;
    auto visible = [&](const CharacterTaskEntry& row, ActionView view, bool development = false,
                       bool unavailable = false) {
        return ui::ActionVisible(row, false, view, development, unavailable);
    };
    auto listed = entry;
    listed.domain = ActionDomain::Battle;
    listed.query = {};
    check(visible(listed, ActionView::Battle) && !visible(listed, ActionView::Exploration));
    check(!visible(listed, ActionView::Preview)); // listed skill.* and reaction scripts are normal actions
    listed.diagnostic = true;
    check(!visible(listed, ActionView::Preview) && visible(listed, ActionView::Preview, true));
    listed.kind = CharacterTaskKind::Setting;
    check(!visible(listed, ActionView::Preview));
    listed.diagnostic = false;
    check(visible(listed, ActionView::Battle)); // settings keep the source domain
    listed.kind = CharacterTaskKind::Configured;
    listed.diagnostic = true;
    check(!visible(listed, ActionView::Preview) && visible(listed, ActionView::Preview, true));
    listed.kind = CharacterTaskKind::Program;
    listed.diagnostic = false;
    check(!visible(listed, ActionView::Preview) && visible(listed, ActionView::Preview, true));
    listed.kind = CharacterTaskKind::Reaction;
    listed.diagnostic = false;
    check(visible(listed, ActionView::Battle)); // existing stagger/recovery entries are retained
    listed.query = {TaskAvailability::Unavailable, {}, {{TaskReasonKind::Unimplemented, "entry", "missing"}}};
    check(!visible(listed, ActionView::Battle, false, true));
    check(!visible(listed, ActionView::Battle, true, false));
    check(visible(listed, ActionView::Battle, true, true));
    listed.kind = CharacterTaskKind::Sequence;
    listed.query = {TaskAvailability::Unavailable, {}, {{TaskReasonKind::MissingInput, "Target", "absent"}}};
    check(visible(listed, ActionView::Battle)); // inputs can be supplied with the row settings
    {
        ui::ActionVariantSettings variants;
        CharacterTaskEntry row;
        row.kind = CharacterTaskKind::Reaction;
        row.domain = ActionDomain::Battle;
        row.label = "Stagger";
        row.detail = "Occur -> Loop -> Recover";
        row.command = SelectCatalogAction{7, {11, VisualActionKey{"source", 1, false}}};
        row.variants = {
            {"normal", "Normal hit", row.detail, row.command, {}},
            {"heavy", "Heavy hit", row.detail, SelectCatalogAction{7, {11, VisualActionKey{"source", 2, false}}}, {}}};
        const auto key = ui::ActionRowIdentity(row);
        check(variants.Selected(row, 7, 3) == "normal");
        variants.Choose(row, 7, 3, "heavy");
        check(host.TakeCommands(false).empty()); // settings never play a variant
        auto selected = variants.Apply(row, 7, 3);
        check(ui::ActionRowIdentity(selected) == key);
        check(std::get<VisualActionKey>(std::get<SelectCatalogAction>(selected.command).action.key).flag == 2);
        host.Post(selected.command);
        batch = host.TakeCommands(false);
        check(batch.size() == 1);
        check(std::get<VisualActionKey>(std::get<SelectCatalogAction>(batch.front().command).action.key).flag == 2);
        check(variants.Selected(row, 7, 4) == "normal" && variants.Selected(row, 8, 3) == "normal");
        auto fresh = row;
        std::reverse(fresh.variants.begin(), fresh.variants.end());
        fresh.variants.front().command = SelectCatalogAction{7, {12, VisualActionKey{"source", 2, false}}};
        fresh.variants.front().query = {
            TaskAvailability::Unavailable, {}, {{TaskReasonKind::Unimplemented, "model.state", "absent"}}};
        selected = variants.Apply(fresh, 7, 3);
        check(variants.Selected(fresh, 7, 3) == "heavy"); // key, never old index
        check(std::get<SelectCatalogAction>(selected.command).action.revision == 12);
        check(!ui::ActionCanRequest(selected));       // fresh query overrides old availability
        check(visible(selected, ActionView::Battle)); // normal variant remains usable; keep the gear
        fresh.variants.erase(fresh.variants.begin());
        check(variants.Selected(fresh, 7, 3) == "normal"); // removed option falls back to a current option
        variants.Choose(fresh, 7, 3, "not-present");
        check(variants.Selected(fresh, 7, 3) == "normal");
        row.query = {
            TaskAvailability::Unknown, {}, {{TaskReasonKind::Unresolved, "model.state", "target model not prepared"}}};
        check(ui::ActionCanRequest(row) && row.query.availability == TaskAvailability::Unknown);
        check(visible(row, ActionView::Battle)); // first request may initiate the normal handoff
        row.query.reasons.push_back({TaskReasonKind::Participant, "body", "not active"});
        check(!ui::ActionCanRequest(row));
        row.query = {TaskAvailability::Unknown, {}, {{TaskReasonKind::Unresolved, "catalog", "not read"}}};
        check(!ui::ActionCanRequest(row));
        row.query = {
            TaskAvailability::Unavailable, {}, {{TaskReasonKind::Unimplemented, "model.state", "real absence"}}};
        check(!ui::ActionCanRequest(row));
    }
}
