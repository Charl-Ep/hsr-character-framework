#include <hksr/framework/tasks/BattleControl.hpp>
#include <stdexcept>

namespace {
using namespace hksr::framework;
void Check(bool ok){if(!ok)throw std::runtime_error("battle control test failed");}
template<class F> void Reject(F call){bool rejected=false;try{call();}catch(const std::exception&){rejected=true;}Check(rejected);}
ControlRelation Path(std::vector<ControlChange> changes={}) {
    ControlRelation path;path.sources={{"fixture","reviewed control boundary"}};path.accepted=std::move(changes);return path;
}
void ChoicesAndCompletion() {
    BattleControl state;ControlSkill a{"source-a",0,1},b{"source-a",1,2},variant{"source-b",0,1};
    auto choose=Path({ControlSelect{a}});choose.conditions={ControlPerforming{{ControlPerformance::Ready}}};
    auto first=state.Accept(choose,ControlTrigger::User,1);Check(state.Snapshot().selection==a);
    auto execute=Path({ControlSetPerformance{ControlPerformance::Accepted}});
    execute.conditions={ControlSelected{a},ControlPerforming{{ControlPerformance::Ready}}};
    execute.started={ControlSetPerformance{ControlPerformance::Working}};
    execute.completed={ControlSetPerformance{ControlPerformance::Finished},ControlSelect{std::nullopt}};
    auto run=state.Accept(execute,ControlTrigger::User,2);
    Check(state.Query(choose,ControlTrigger::User).availability==TaskAvailability::Unavailable);
    state.Started(run);Check(state.Snapshot().performance==ControlPerformance::Working);
    // A separately reviewed forced selection can change selection while the
    // old execution still owns its completion phase.
    state.Accept(Path({ControlSelect{b}}),ControlTrigger::User,3);
    state.End(run,TaskExitReason::Completed);
    Check(state.Snapshot().selection==b && state.Snapshot().performance==ControlPerformance::Finished);
    auto wrong=Path();wrong.conditions={ControlSelected{variant}};
    Check(state.Query(wrong,ControlTrigger::User).availability==TaskAvailability::Unavailable);
    state.End(first,TaskExitReason::Completed);Check(state.Snapshot().selection==b);
    auto unknown=Path();unknown.conditions={TaskUnresolvedRequirement{"source/condition","not bound"}};
    Check(state.Query(unknown,ControlTrigger::User).availability==TaskAvailability::Unknown);
    auto event=Path();event.trigger=ControlTrigger::Event;
    Check(state.Query(event,ControlTrigger::User).availability==TaskAvailability::Unavailable);
    Check(state.Query(event,ControlTrigger::Event).availability==TaskAvailability::Available);
    auto stale=state.Accept(execute=Path(),ControlTrigger::User,4);
    stale.relation.completed={ControlSelect{variant}};stale.versions["selection"]=0;
    state.Reset();state.End(stale,TaskExitReason::Completed);Check(!state.Snapshot().selection);
}
void QueueAndReturn() {
    BattleControl state;ControlSkill base{"config",2,1},ultra{"config",9,2};
    state.Accept(Path({ControlSelect{base}}),ControlTrigger::User,1);
    state.Accept(Path({ControlQueueInsertion{"ultra",ultra}}),ControlTrigger::User,2);
    Check(state.Snapshot().selection==base && state.Snapshot().pending.size()==1 && !state.Snapshot().insertion);
    auto enter=Path({ControlEnterInsertion{"ultra"}});enter.conditions={ControlPerforming{{ControlPerformance::Ready}},ControlInsertionAt{}};
    state.Accept(enter,ControlTrigger::User,3);
    Check(state.Snapshot().selection==ultra && state.Snapshot().pending.empty());
    auto cancel=Path({ControlLeaveInsertion{}});cancel.conditions={ControlInsertionAt{ControlInsertionPhase::Waiting}};
    Check(state.Query(cancel,ControlTrigger::User).availability==TaskAvailability::Unavailable);
    state.Accept(Path({ControlAdvanceInsertion{ControlInsertionPhase::Waiting}}),ControlTrigger::User,4);
    state.Accept(Path({ControlQueueInsertion{"second",ultra}}),ControlTrigger::User,5);
    state.Accept(cancel,ControlTrigger::User,6);
    Check(state.Snapshot().selection==base && !state.Snapshot().insertion && state.Snapshot().pending.size()==1);
    auto withdraw=Path({ControlWithdrawInsertion{"second"}});state.Accept(withdraw,ControlTrigger::User,7);
    Check(state.Query(withdraw,ControlTrigger::User).availability==TaskAvailability::Unavailable);
    // Failing the second change does not leave the first selection committed.
    auto invalid=Path({ControlSelect{ultra},ControlEnterInsertion{"missing"}});
    Reject([&]{state.Accept(invalid,ControlTrigger::User,8);});Check(state.Snapshot().selection==base);
    state.Accept(Path({ControlQueueInsertion{"earlier",base},ControlQueueInsertion{"priority",ultra}}),ControlTrigger::User,9);
    state.Accept(Path({ControlEnterInsertion{"priority"}}),ControlTrigger::User,10);
    Check(state.Snapshot().insertion->id=="priority" && state.Snapshot().pending.front().id=="earlier");
}
void SourceOwnership() {
    BattleControl state;
    state.Accept(Path({ControlAddSource{{"first",10,4}}}),ControlTrigger::User,1);
    state.Accept(Path({ControlAddSource{{"second",20,2}}}),ControlTrigger::User,2);
    state.Accept(Path({ControlAddSource{{"tie",30,2}}}),ControlTrigger::User,3);
    Check(state.Snapshot().dominant_source->id=="second");
    auto recover=Path({ControlRemoveSource{"second"}});recover.conditions={ControlSourcePresent{"second"}};
    state.Accept(recover,ControlTrigger::User,4);
    Check(state.Snapshot().dominant_source->id=="tie" && state.Snapshot().sources.size()==2);
    Check(state.Query(recover,ControlTrigger::User).availability==TaskAvailability::Unavailable);
    auto owner=Path({ControlAddSource{{"held",40,0}}});owner.completed={ControlRemoveSource{"held"}};
    auto old=state.Accept(owner,ControlTrigger::User,5);
    state.Accept(Path({ControlAddSource{{"held",50,1}}}),ControlTrigger::User,6);
    state.End(old,TaskExitReason::Completed);
    Check(state.Snapshot().dominant_source->flag==50); // old completion cannot remove a renewed source
}
}
void TestBattleControl(){ChoicesAndCompletion();QueueAndReturn();SourceOwnership();}
