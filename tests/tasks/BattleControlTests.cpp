#include <hksr/framework/tasks/BattleControl.hpp>
#include <hksr/framework/tasks/TaskCatalog.hpp>
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
SkillSlotSetup SlotSetup() {
    return {{{{"config",4},"ordinary",{}},{{"config",7},"enhanced",{}},{{"config",9},"third",{}},
        {{"config",2},"parent",{"third","enhanced"}}},{{0,"ordinary"},{1,"parent"},{2,std::nullopt}}};
}
void SlotsAndMappingLifetime() {
    SkillSlots slots(SlotSetup());
    Check(slots.Resolve(0)==std::optional<ControlSkill>({"config",4,0}));
    Check(slots.Resolve(1,0)==std::optional<ControlSkill>({"config",9,1}));
    Check(!slots.Resolve(1,2)); // child position is not a global skill index
    slots.Apply(ReplaceSkillKey{"phase-a","ordinary","enhanced"});
    slots.Apply(ReplaceSkillKey{"phase-b","ordinary","third"});
    slots.Apply(ReplaceSkillKey{"phase-a","ordinary","parent"});
    Check(slots.Resolve(0)->index==9); // updating older owner does not reorder it
    slots.Apply(ReplaceSkillKey{"phase-b","ordinary",std::nullopt});
    Check(slots.Resolve(0)->index==2 && !slots.Resolve(1)); // one current slot per skill
    slots.Apply(ReplaceSkillKey{"phase-a","ordinary",std::nullopt});
    Check(slots.Resolve(0)->index==4 && !slots.Resolve(1)); // not implicit default restoration of moved-from slot
    slots.Apply(MapControlSlot{1,"parent"});
    slots.Apply(ReplaceSkillKey{"phase-a","ordinary","enhanced"});
    slots.Apply(ReplaceSkillKey{"phase-b","enhanced","third"});
    Check(slots.Resolve(0)->index==7); // key mapping is one hop
    slots.Apply(OccupyControlSlot{0,"hold"});
    slots.Apply(MapControlSlot{0,"third"});
    Check(slots.Resolve(0)->index==7 && slots.Snapshot().slots.at(0).occupation->changed);
    slots.Apply(ReplaceSkillKey{"phase-a","ordinary","parent"});
    Check(slots.Resolve(0)->index==2); // unlike MapControlSlot, key map updates occupied slots
    slots.Apply(ReleaseControlSlot{0,"unrelated"});Check(slots.Snapshot().slots.at(0).occupation.has_value());
    slots.Apply(ReleaseControlSlot{0,"hold"});Check(slots.Resolve(0)->index==9);
    slots.Apply(OccupyControlSlot{0,"hold"});slots.Apply(MapControlSlot{0,std::nullopt});slots.Apply(ReleaseControlSlot{0,"hold"});
    Check(slots.Resolve(0)->index==4 && !slots.Snapshot().slots.at(0).key); // explicit null is a deferred update too
    slots.Apply(DisableControlSlot{0,"first"});slots.Apply(DisableControlSlot{0,"second"});
    slots.Apply(DisableControlSlot{0,"first",false});Check(slots.Available(0)==TaskAvailability::Unavailable);
    slots.Apply(DisableControlSlot{0,"second",false});Check(slots.Available(0)==TaskAvailability::Available);
    Check(slots.Available(100)==TaskAvailability::Unknown);
    slots.Reset();Check(slots.Resolve(1)->index==2 && slots.Snapshot().mappings.empty());
    slots.Apply(MapControlSlot{2,"third"});
    Check(slots.Resolve(2)->index==9 && !slots.Resolve(1)); // child takes its parent's bound slot away
    slots.Reset();slots.Apply(ReplaceSkillKey{"phase","ordinary","third"});
    slots.Apply(MapControlSlot{2,"third"});Check(!slots.Resolve(0));
    slots.Apply(ReplaceSkillKey{"phase","ordinary","third"});Check(!slots.Resolve(0)); // unchanged alias must not steal it back
    slots.Reset();slots.Apply(ReplaceSkillKey{"phase","ordinary","third"});
    slots.Apply(OccupyControlSlot{0,"held"});slots.Apply(MapControlSlot{0,"third"});slots.Apply(ReleaseControlSlot{0,"held"});
    Check(slots.Snapshot().slots.at(0).key=="ordinary"); // release compares current identity, not requested key
    auto cyclic=SlotSetup();cyclic.skills[2].children={"parent"};Reject([&]{SkillSlots invalid(cyclic);});
}
void SlotControlAdmission() {
    BattleControl state(SlotSetup());auto choose=Path({ControlSelect{ControlSkill{"config",4,0}}});
    choose.conditions={ControlSlotMatches{0,{"config",4}},ControlSlotAvailable{0}};
    Check(state.Query(choose,ControlTrigger::User).availability==TaskAvailability::Available);
    auto hold=Path({ControlChangeSlot{OccupyControlSlot{0,"held"}}});
    hold.completed={ControlChangeSlot{ReleaseControlSlot{0,"held"}}};
    auto receipt=state.Accept(hold,ControlTrigger::User,1);
    state.Accept(Path({ControlChangeSlot{MapControlSlot{0,"enhanced"}}}),ControlTrigger::User,2);
    Check(state.Query(choose,ControlTrigger::User).availability==TaskAvailability::Available);
    state.End(receipt,TaskExitReason::Completed); // deferred mapping must not invalidate its occupation release
    Check(state.Snapshot().slots.slots.at(0).current=="enhanced");
    Check(state.Query(choose,ControlTrigger::User).availability==TaskAvailability::Unavailable);
    auto invalid=Path({ControlSelect{ControlSkill{"config",7,0}},ControlChangeSlot{MapControlSlot{99,"third"}}});
    Reject([&]{state.Accept(invalid,ControlTrigger::User,3);});Check(!state.Snapshot().selection);
    state.Reset();Check(state.Query(choose,ControlTrigger::User).availability==TaskAvailability::Available);
}
void SlotCatalogParsing() {
    const auto catalog=ParseTaskCatalog(R"({"format":"hksr.task-catalog","id":"slots","domain":"battle",
      "visual":{"id":"slots","source":"config","scope":"battle","effects":{}},"sequences":{},
      "control_slots":{"skills":[{"skill":{"source":"config","index":7},"key":"enhanced"}],"defaults":[{"slot":0,"key":"enhanced"}]},
      "tasks":{"select":{"control":{"trigger":"user","presentation":false,"sources":[{"document":"fixture","field":"select"}],
        "requires":[{"op":"slot_available","slot":0},{"op":"slot_skill","slot":0,"skill":{"source":"config","index":7,"slot":0}}],
        "accepted":[{"op":"occupy_slot","slot":0,"owner":"preview"},{"op":"map_slot","slot":0,"key":null}],
        "completed":[{"op":"release_slot","slot":0,"owner":"preview"},{"op":"disable_slot","slot":0,"owner":"source","disabled":false}]}}}})");
    BattleControl state(catalog.control_slots);const auto& path=*catalog.tasks.at("select").definition.control;
    auto ticket=state.Accept(path,ControlTrigger::User,1);state.End(ticket,TaskExitReason::Completed);
    Check(state.Snapshot().slots.slots.at(0).current=="enhanced");
}
}
void TestBattleControl(){ChoicesAndCompletion();QueueAndReturn();SourceOwnership();SlotsAndMappingLifetime();SlotControlAdmission();SlotCatalogParsing();}
