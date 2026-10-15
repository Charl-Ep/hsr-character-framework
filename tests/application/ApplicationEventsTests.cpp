#include <hksr/framework/application/ApplicationService.hpp>
#include <future>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool value) {if(!value)throw std::runtime_error("application events test failed");}
}
void TestApplicationEvents() {
    using namespace hksr::framework;
    namespace api=hksr::api;
    ApplicationService service(3);
    auto initial=service.ReadUpdates();
    Check(initial.changes.reset && initial.changes.cursor.sequence==0 && initial.changes.cursor.stream.size()==32);
    ApplicationSnapshot state;state.prepared=true;state.scene=SceneAddress{"map",1};
    service.Publish(state);
    auto update=service.ReadUpdates(initial.changes.cursor);
    Check(!update.changes.reset && update.changes.events.size()==1 && update.changes.events[0].runtime && update.snapshot.prepared);
    auto cursor=update.changes.cursor;service.Publish(state);
    Check(service.ReadUpdates(cursor).changes.events.empty());

    state.characters.resize(2);state.characters[0].id=7;state.characters[1].id=8;
    state.characters[0].package.display_name="character";
    OperationStatus operation;operation.id=90;state.operations.entries.emplace(90,operation);
    service.Publish(state);update=service.ReadUpdates(cursor);
    Check(update.changes.events[0].characters==std::vector<std::uint64_t>({7,8}));
    Check(update.changes.events[0].operations==std::vector<std::uint64_t>{90});
    const auto old=update.snapshot;cursor=update.changes.cursor;
    std::swap(state.characters[0],state.characters[1]);service.Publish(state);
    Check(service.ReadUpdates(cursor).changes.events.empty()); // identity, not vector ordering
    auto& actor=state.characters[1];
    actor.placement.position={1,2,3};actor.look={LookMode::Point,0,{4,5,6}};
    state.operations.entries.at(90).phase=OperationPhase::Returned;
    service.Publish(state);update=service.ReadUpdates(cursor);cursor=update.changes.cursor;
    Check(update.changes.events[0].characters==std::vector<std::uint64_t>{7} && update.changes.events[0].operations.size()==1);
    Check(old.characters[0].placement.position.x==0 && old.operations.entries.at(90).phase==OperationPhase::Waiting);

    // Rendering/control diagnostics are part of the published value contract,
    // not an excuse to broadcast the full snapshot on every world frame.
    actor.session=CharacterSessionStatus{};actor.handoff=HandoffStatus{};
    actor.playback=ActionPlaybackStatus{};actor.controls=ControlStatus{};actor.outfit=api::CharacterOutfitStatus{};
    actor.action=ActionIntentStatus{};service.Publish(state);cursor=service.ReadUpdates().changes.cursor;
    auto change=[&](const auto& mutate){
        mutate();service.Publish(state);auto result=service.ReadUpdates(cursor);
        Check(!result.changes.reset && result.changes.events.size()==1 && result.changes.events[0].characters==std::vector<std::uint64_t>{7});
        cursor=result.changes.cursor;service.Publish(state);Check(service.ReadUpdates(cursor).changes.events.empty());
    };
    change([&]{actor.session->waiting="loading";});
    change([&]{actor.handoff->ready_domain=ActionDomain::Alternate;});
    change([&]{actor.handoff->initial_state.normalized_time=0.25f;});
    change([&]{actor.playback->completion_note="returning";});
    change([&]{actor.controls->live.ready=true;actor.controls->live.position.x=9;});
    change([&]{actor.controls->gaze_phase=api::GazePhase::TargetUnavailable;});
    change([&]{actor.controls->gaze_error="target absent";});
    change([&]{actor.controls->placement_phase=api::PlacementPhase::Waiting;});
    change([&]{actor.outfit->errors.push_back("outfit failure");});
    change([&]{actor.action->phase=ActionIntentPhase::Returned;});
    change([&]{actor.pending_inputs=1;});
    change([&]{actor.deleting=true;});
    change([&]{actor.battle_idle_state="Idle";});
    change([&]{actor.playback->successor=AutomaticSuccessor{1,2,"Idle",false};});
    change([&]{actor.playback->successor->returned=true;});
    change([&]{actor.package.display_name="new display";});
    change([&]{actor.placement.position.x=std::numeric_limits<float>::quiet_NaN();});
    // Equal NaNs must not cause an endless stream of identical invalid data.
    service.Publish(state);Check(service.ReadUpdates(cursor).changes.events.empty());

    state.characters.erase(state.characters.begin()+1);state.operations.entries.clear();
    service.Publish(state);update=service.ReadUpdates(cursor);cursor=update.changes.cursor;
    Check(update.changes.events[0].characters==std::vector<std::uint64_t>{7} && update.changes.events[0].operations==std::vector<std::uint64_t>{90});
    Check(update.snapshot.characters.size()==1 && update.snapshot.operations.entries.empty());
    state.catalog=std::make_shared<const CatalogSnapshot>();service.Publish(state);
    update=service.ReadUpdates(cursor);cursor=update.changes.cursor;
    Check(update.changes.events[0].catalog && update.snapshot.catalog);
    state.controller_catalog_error="reader unavailable";service.Publish(state);
    update=service.ReadUpdates(cursor);cursor=update.changes.cursor;Check(update.changes.events[0].inspection);

    // Old/future/other-process cursors cannot silently skip changes. Replays
    // contain invalidations plus ONE latest snapshot, not old giant catalogs.
    Check(service.ReadUpdates(initial.changes.cursor).changes.reset);
    auto future=cursor;++future.sequence;Check(service.ReadUpdates(future).changes.reset);
    ApplicationService other;Check(other.ReadUpdates(cursor).changes.reset);
    auto timed=service.WaitUpdates(cursor,std::chrono::milliseconds(0));
    Check(!timed.changes.reset && timed.changes.events.empty());
    std::promise<void> entering;auto entered=entering.get_future();
    auto reader=std::async(std::launch::async,[&]{
        entering.set_value();return service.WaitUpdates(cursor,std::chrono::seconds(2));
    });
    auto second_reader=std::async(std::launch::async,[&]{return service.WaitUpdates(cursor,std::chrono::seconds(2));});
    entered.get();state.scene->incarnation=2;service.Publish(state);
    update=reader.get();Check(update.changes.cursor.sequence>cursor.sequence && update.snapshot.scene->incarnation==2);
    const auto second_update=second_reader.get();
    Check(second_update.changes.cursor.sequence==update.changes.cursor.sequence && second_update.snapshot.scene->incarnation==2);
    cursor=update.changes.cursor;
    // Change already published before subscription: the predicate returns it
    // immediately, so the read-snapshot/subscribe gap cannot lose the wakeup.
    state.map_error="map changed";service.Publish(state);
    update=service.WaitUpdates(cursor,std::chrono::milliseconds(0));
    Check(!update.changes.events.empty() && update.snapshot.map_error=="map changed");
    cursor=update.changes.cursor;service.TakeCommands(true);
    update=service.ReadUpdates(cursor);Check(update.snapshot.closing && update.changes.events[0].runtime);
    cursor=update.changes.cursor;state.closed=true;service.Publish(state);
    update=service.WaitUpdates(cursor,std::chrono::milliseconds(0));Check(update.snapshot.closed);
    cursor=update.changes.cursor;Check(service.WaitUpdates(cursor,std::chrono::milliseconds(0)).snapshot.closed);

    ApplicationService no_replay(0);auto before=no_replay.ReadUpdates().changes.cursor;
    no_replay.Publish(state);Check(no_replay.ReadUpdates(before).changes.reset && no_replay.ReadUpdates(before).snapshot.closed);
    ApplicationService boundary(3);ApplicationSnapshot sample;
    auto first=boundary.ReadUpdates().changes.cursor;
    sample.prepared=true;boundary.Publish(sample);const auto retained=boundary.ReadUpdates().changes.cursor;
    sample.map_error="one";boundary.Publish(sample);
    sample.map_error="two";boundary.Publish(sample);
    sample.map_error="three";boundary.Publish(sample);
    Check(boundary.ReadUpdates(first).changes.reset);
    const auto replay=boundary.ReadUpdates(retained);
    Check(!replay.changes.reset && replay.changes.events.size()==3 && replay.snapshot.map_error=="three");
}
