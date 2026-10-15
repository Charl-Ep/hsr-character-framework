#include <hksr/framework/application/OperationHistory.hpp>
#include <stdexcept>

namespace {
void Check(bool value){if(!value)throw std::runtime_error("operation history test failed");}
}
void TestOperationHistory() {
    using namespace hksr::framework;
    OperationHistory history(3);
    SceneCharacterStatus actor;actor.id=7;actor.revision=1;
    OperationStatus create;create.id=1;create.character=7;create.revision=1;
    history.Add(create);history.Observe({actor},nullptr,false);
    Check(history.Snapshot().entries.at(1).phase==OperationPhase::Waiting);
    actor.active=true;actor.active_revision=1;actor.creations=1;
    history.Observe({actor},nullptr,false);
    Check(history.Snapshot().entries.at(1).phase==OperationPhase::Running);
    // Merely being active is deliberately insufficient.
    actor.creation_ready=true;history.Observe({actor},nullptr,false);
    Check(history.Snapshot().entries.at(1).phase==OperationPhase::Returned);
    const auto saved=history.Snapshot();const auto seq=saved.sequence;
    OperationHistory deletion;
    OperationStatus removing;removing.id=99;removing.character=7;removing.revision=actor.revision;removing.kind=OperationKind::Delete;
    deletion.Add(removing);deletion.Observe({actor},nullptr,false);
    Check(deletion.Snapshot().entries.at(99).phase==OperationPhase::Running);
    deletion.Observe({},nullptr,false);Check(deletion.Snapshot().entries.at(99).phase==OperationPhase::Returned);
    actor.active=false;actor.active_revision=0;actor.creation_ready=false;
    history.Observe({actor},nullptr,false);
    Check(history.Snapshot().sequence==seq && saved.entries.at(1).creation==1);

    OperationStatus action;action.id=2;action.kind=OperationKind::Action;action.character=7;action.revision=1;action.action_intent=12;
    actor.action=ActionIntentStatus{12,0,ActionIntentPhase::Waiting,{},"not in owner scene"};
    history.Add(action);history.Observe({actor},nullptr,false);
    Check(history.Snapshot().entries.at(2).phase==OperationPhase::Waiting);
    actor.action=ActionIntentStatus{13,0,ActionIntentPhase::Waiting,{},"new action"};
    history.Observe({actor},nullptr,false);
    Check(history.Snapshot().entries.at(2).phase==OperationPhase::Superseded);
    action.id=3;action.action_intent=13;history.Add(action);
    actor.action=ActionIntentStatus{13,2,ActionIntentPhase::Returned,ActionOutcome::Returned,"played"};
    history.Observe({actor},nullptr,false);
    Check(history.Snapshot().entries.at(3).creation==2 && history.Snapshot().entries.at(3).Terminal());
    // Later automatic idle/failure or scene departure cannot rewrite history.
    actor.action->phase=ActionIntentPhase::Failed;history.Observe({actor},nullptr,false);
    Check(history.Snapshot().entries.at(3).phase==OperationPhase::Returned);

    OperationStatus remove;remove.id=4;remove.kind=OperationKind::Disable;remove.character=7;remove.revision=2;
    actor.revision=2;actor.enabled=false;actor.retiring=1;
    history.Add(remove);history.Observe({actor},nullptr,false);
    Check(history.Snapshot().entries.at(4).phase==OperationPhase::Running);
    history.Observe({actor},nullptr,true); // shutdown must still await cleanup
    Check(history.Snapshot().entries.at(4).phase==OperationPhase::Running);
    actor.retiring=0;actor.pending_inputs=1;history.Observe({actor},nullptr,true);
    Check(history.Snapshot().entries.at(4).phase==OperationPhase::Running);
    actor.pending_inputs=0;history.Observe({actor},nullptr,true);
    Check(history.Snapshot().entries.at(4).phase==OperationPhase::Returned && !history.Snapshot().entries.count(1));
    Check(saved.entries.at(1).creation==1); // published copies survive trimming

    create.id=5;create.revision=3;create.kind=OperationKind::Rebuild;
    actor.enabled=true;actor.revision=3;actor.active=true;actor.active_revision=2;actor.creation_ready=true;
    history.Add(create);history.Observe({actor},nullptr,false);
    Check(history.Snapshot().entries.at(5).phase==OperationPhase::Waiting); // old generation cannot complete rebuild
    actor.creation_error="exception after factory returned";actor.retiring=1;
    history.Observe({actor},nullptr,false);
    Check(history.Snapshot().entries.at(5).phase==OperationPhase::Failed && actor.retiring==1);
    // Failure reports the operation outcome, not resource drainage.
    create.id=6;create.revision=4;actor.revision=4;actor.creation_error.clear();
    history.Add(create);history.Observe({actor},nullptr,false);
    actor.revision=5;history.Observe({actor},nullptr,false);
    Check(history.Snapshot().entries.at(6).phase==OperationPhase::Superseded);

    CatalogSnapshot catalog;auto request=std::make_shared<SourceRequestStatus>();request->id=8;
    catalog.sources[8]=request;
    OperationStatus source;source.id=7;source.kind=OperationKind::PrepareSource;source.source_request=8;
    history.Add(source);history.Observe({},&catalog,false);
    Check(!history.Snapshot().entries.at(7).Terminal());
    request->phase=SourceRequestPhase::Preparing;history.Observe({},&catalog,false);
    Check(history.Snapshot().entries.at(7).phase==OperationPhase::Running);
    catalog.sources.clear();history.Observe({},&catalog,false);
    Check(history.Snapshot().entries.at(7).phase==OperationPhase::Cancelled);
    source.id=8;history.Add(source);catalog.sources[8]=request;
    request->phase=SourceRequestPhase::Unresolved;request->error="ambiguous NPC";
    history.Observe({},&catalog,false);
    Check(history.Snapshot().entries.at(8).detail=="ambiguous NPC");
    source.id=9;history.Add(source);request->phase=SourceRequestPhase::Ready;
    history.Observe({},&catalog,false);catalog.sources.clear();history.Observe({},&catalog,false);
    Check(history.Snapshot().entries.at(9).phase==OperationPhase::Returned);

    create.id=10;create.revision=actor.revision;history.Add(create);
    actor.active_revision=actor.revision;actor.handoff.emplace();actor.handoff->phase=HandoffPhase::Failed;actor.handoff->error="model input failed";
    history.Observe({actor},nullptr,false);
    Check(history.Snapshot().entries.at(10).phase==OperationPhase::Failed && history.Snapshot().entries.at(10).detail=="model input failed");
    actor.handoff.reset();actor.active_revision=0;

    OperationHistory short_history(1);
    create.id=1;create.revision=actor.revision;short_history.Add(create);
    auto rejected=create;rejected.phase=OperationPhase::Failed;rejected.id=2;
    short_history.Add(rejected);rejected.id=3;short_history.Add(rejected);
    Check(short_history.Snapshot().entries.count(1) && !short_history.Snapshot().entries.count(2));
    short_history.Observe({actor},nullptr,true);
    Check(short_history.Snapshot().entries.at(1).phase==OperationPhase::Cancelled);

    OperationHistory controls;
    OperationStatus position;position.id=1;position.kind=OperationKind::Placement;position.character=7;
    auto submission=std::make_shared<ControlSubmission>();
    auto write=std::make_shared<hksr::api::ControlResult>();write->id=18;
    submission->Bind(3,write);controls.Add(position,submission);
    controls.Observe({},nullptr,false);Check(controls.Snapshot().entries.at(1).phase==OperationPhase::Waiting);
    write->phase=hksr::api::ControlPhase::Running;controls.Observe({},nullptr,false);
    Check(controls.Snapshot().entries.at(1).phase==OperationPhase::Running);
    write->phase=hksr::api::ControlPhase::Returned;controls.Observe({},nullptr,false);
    Check(controls.Snapshot().entries.at(1).creation==3 && controls.Snapshot().entries.at(1).phase==OperationPhase::Returned);
    // Retaining a terminal observation does not retain its live receipt bridge.
    std::weak_ptr<const ControlSubmission> lifetime=submission;submission.reset();Check(lifetime.expired());

    auto gaze=std::make_shared<ControlSubmission>();auto old=std::make_shared<hksr::api::ControlResult>();
    old->phase=hksr::api::ControlPhase::Running;gaze->Bind(3,old);
    position.id=2;position.kind=OperationKind::Gaze;controls.Add(position,gaze);
    gaze->AwaitCreation();old->phase=hksr::api::ControlPhase::Cancelled;
    controls.Observe({},nullptr,false);
    Check(controls.Snapshot().entries.at(2).phase==OperationPhase::Waiting && controls.Snapshot().entries.at(2).creation==0);
    auto next=std::make_shared<hksr::api::ControlResult>();gaze->Bind(4,next);
    next->phase=hksr::api::ControlPhase::Returned;gaze->AwaitCreation();
    gaze->Bind(5,std::make_shared<hksr::api::ControlResult>());controls.Observe({},nullptr,false);
    Check(controls.Snapshot().entries.at(2).phase==OperationPhase::Returned && controls.Snapshot().entries.at(2).creation==4);
    auto pending=std::make_shared<ControlSubmission>();position.id=3;controls.Add(position,pending);
    pending->Complete(hksr::api::ControlPhase::Superseded,"new gaze");controls.Observe({},nullptr,false);
    Check(controls.Snapshot().entries.at(3).phase==OperationPhase::Superseded);
    auto inflight=std::make_shared<ControlSubmission>();auto call=std::make_shared<hksr::api::ControlResult>();
    call->phase=hksr::api::ControlPhase::Running;inflight->Bind(9,call);
    position.id=4;position.kind=OperationKind::Placement;controls.Add(position,inflight);
    controls.Observe({},nullptr,true); // owner has not yet harvested movement completion
    Check(controls.Snapshot().entries.at(4).phase==OperationPhase::Running);
    call->phase=hksr::api::ControlPhase::Returned;controls.Observe({},nullptr,true);
    Check(controls.Snapshot().entries.at(4).phase==OperationPhase::Returned);
    auto reentrant=std::make_shared<ControlSubmission>();call=std::make_shared<hksr::api::ControlResult>();
    call->phase=hksr::api::ControlPhase::Running;reentrant->Bind(10,call);reentrant->Supersede(true);
    call->phase=hksr::api::ControlPhase::Returned;
    Check(reentrant->Snapshot().result.phase==hksr::api::ControlPhase::Returned);
}
