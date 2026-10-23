#include "../../src/ui/TaskInputDraft.hpp"
#include <stdexcept>

void TestTaskInputDraft() {
    using namespace hksr::framework;
    auto check=[](bool value){if(!value)throw std::runtime_error("task input draft");};
    CharacterTaskEntry entry;entry.kind=CharacterTaskKind::Sequence;
    entry.inputs={{"count",TaskValueType::Integer,{}}};entry.bindings.targets={"Target"};
    entry.command=ControlCharacterTask{7,3,{TaskControlOperation::Select,TaskSelection{{"test","attack"}}}};
    entry.query={TaskAvailability::Unavailable,{},{{TaskReasonKind::MissingInput,"Target","absent"}}};
    check(CanConfigureTask(entry));
    entry.query.reasons.push_back({TaskReasonKind::Condition,"phase","not enhanced"});check(!CanConfigureTask(entry));
    entry.query.reasons.pop_back();entry.query.reasons.push_back({TaskReasonKind::Unimplemented,"entry","missing"});check(!CanConfigureTask(entry));
    entry.query.reasons.pop_back();
    ui::TaskInputDraft draft(entry);ApplicationService host;
    draft.Query(host);draft.Query(host);auto batch=host.TakeCommands(false);check(batch.size()==1);
    check(std::get<ControlCharacterTask>(batch.front().command).input.operation==TaskControlOperation::Query);
    check(!draft.Ready());
    draft.Selection().query.inputs["count"]=std::int64_t(2);draft.Changed();
    CommandReply reply;reply.serial=batch.front().serial;reply.accepted=true;reply.task.emplace();
    reply.task->selection=TaskSelectionResult{0,{"test","attack"},{}};
    check(draft.Reply(reply) && !draft.Ready()); // stale response cannot authorize an edited selection
    draft.Query(host);batch=host.TakeCommands(false);check(batch.size()==1);
    reply.serial=batch.front().serial;check(draft.Reply(reply) && draft.Ready());
    check(!draft.Reply(reply));
    ApplicationSnapshot snapshot;SceneCharacterStatus actor;actor.id=7;actor.creations=3;actor.active=actor.enabled=true;
    actor.scene="map";snapshot.characters.push_back(actor);
    draft.Selection().visual.targets["Target"]=7;draft.Observe(snapshot,actor);check(!draft.Ready());
    const auto revision=draft.revision;draft.Observe(snapshot,actor);check(draft.revision==revision);
    snapshot.characters.front().creations=4;draft.Observe(snapshot,actor);check(draft.revision>revision);
    draft.Query(host);batch=host.TakeCommands(false);reply.serial=batch.front().serial;
    reply.accepted=false;reply.error="target no longer exists";check(draft.Reply(reply) && !draft.Ready() && !draft.error.empty());
    check(std::get<std::int64_t>(draft.Selection().query.inputs.at("count"))==2);
    check(!draft.Selection().inputs_captured); // effective defaults remain the engine-frame query's responsibility
    auto tasks=std::make_shared<CharacterTaskStatus>();actor.tasks=tasks;draft.Observe(snapshot,actor);
    const auto before_control=draft.revision;
    auto changed=std::make_shared<CharacterTaskStatus>(*tasks);actor.tasks=changed;
    changed->control.status.control.performance=ControlPerformance::Working;
    draft.Observe(snapshot,actor);check(draft.revision>before_control);
}
