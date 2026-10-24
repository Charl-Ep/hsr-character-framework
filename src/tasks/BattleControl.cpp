#include <hksr/framework/tasks/BattleControl.hpp>
#include <algorithm>
#include <set>
#include <stdexcept>

namespace hksr::framework {
namespace {
void Need(bool ok,const char* text){if(!ok)throw std::invalid_argument(text);}
void Skill(const ControlSkill& skill){Need(!skill.source.empty() && skill.index>=0,"invalid control skill identity");}
std::vector<std::string> Channels(const ControlChange& change) {
    return std::visit([](const auto& value)->std::vector<std::string> {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,ControlSelect>)return {"selection"};
        else if constexpr(std::is_same_v<T,ControlSetPerformance>)return {"performance"};
        else if constexpr(std::is_same_v<T,ControlAddSource>)return {"source/"+value.source.id};
        else if constexpr(std::is_same_v<T,ControlRemoveSource>)return {"source/"+value.source};
        else if constexpr(std::is_same_v<T,ControlEnterInsertion>)
            return {"selection","performance","insertion","queue"};
        else if constexpr(std::is_same_v<T,ControlLeaveInsertion>)return {"selection","performance","insertion"};
        else if constexpr(std::is_same_v<T,ControlAdvanceInsertion>)return {"insertion"};
        else if constexpr(std::is_same_v<T,ControlChangeSlot>)return {SkillSlotChannel(value.operation)};
        else return {"queue"};
    },change);
}
void ValidateChange(const ControlChange& change) {
    std::visit([](const auto& value) {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,ControlSelect>) {if(value.skill)Skill(*value.skill);}
        else if constexpr(std::is_same_v<T,ControlAddSource>)Need(!value.source.id.empty(),"empty control source identity");
        else if constexpr(std::is_same_v<T,ControlRemoveSource>)Need(!value.source.empty(),"empty control source identity");
        else if constexpr(std::is_same_v<T,ControlQueueInsertion>) {Need(!value.id.empty(),"empty insertion identity");Skill(value.skill);}
        else if constexpr(std::is_same_v<T,ControlEnterInsertion> || std::is_same_v<T,ControlWithdrawInsertion>)Need(!value.id.empty(),"empty insertion identity");
        else if constexpr(std::is_same_v<T,ControlChangeSlot>)ValidateSkillSlotChange(value.operation);
    },change);
}
}
const char* ControlName(ControlPerformance value) {
    switch(value){case ControlPerformance::Ready:return "ready";case ControlPerformance::Accepted:return "accepted";
    case ControlPerformance::Working:return "working";case ControlPerformance::Finished:return "finished";
    case ControlPerformance::Resetting:return "resetting";default:return "failed";}
}
const char* ControlName(ControlInsertionPhase value) {
    switch(value){case ControlInsertionPhase::Preparing:return "preparing";case ControlInsertionPhase::Waiting:return "waiting";
    case ControlInsertionPhase::Executing:return "executing";default:return "finishing";}
}
const char* ControlName(ControlTrigger value) {
    switch(value){case ControlTrigger::User:return "user";case ControlTrigger::Event:return "event";default:return "automatic";}
}
void ValidateControlRelation(const ControlRelation& relation) {
    Need(!relation.sources.empty(),"control relation requires provenance");
    for(const auto& source:relation.sources)Need(!source.document.empty() && !source.field.empty(),"incomplete control provenance");
    for(const auto& condition:relation.conditions)std::visit([](const auto& value) {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,ControlSelected>) {if(value.skill)Skill(*value.skill);}
        else if constexpr(std::is_same_v<T,ControlPerforming>)Need(!value.phases.empty(),"empty performance condition");
        else if constexpr(std::is_same_v<T,ControlSourcePresent>)Need(!value.source.empty(),"empty source condition");
        else if constexpr(std::is_same_v<T,ControlQueued>)Need(!value.id.empty(),"empty queue condition");
        else if constexpr(std::is_same_v<T,ControlSlotMatches>) {
            Need(value.slot>=0,"negative control slot");Skill(value.skill);
            Need(!value.skill.slot || *value.skill.slot==value.slot,"conflicting control slot condition");
        }
        else if constexpr(std::is_same_v<T,ControlSlotAvailable>)Need(value.slot>=0,"negative control slot");
        else if constexpr(std::is_same_v<T,TaskUnresolvedRequirement>)Need(!value.source.empty() && !value.detail.empty(),"empty unresolved condition");
    },condition);
    for(const auto* changes:{&relation.accepted,&relation.started,&relation.completed,&relation.interrupted,&relation.replaced,&relation.failed})
        for(const auto& change:*changes)ValidateChange(change);
}
TaskQueryResult BattleControl::Query(const ControlRelation& relation,ControlTrigger trigger) const {
    ValidateControlRelation(relation);TaskQueryResult out;
    auto reject=[&](TaskAvailability availability,TaskReasonKind kind,std::string source,std::string detail) {
        if(availability==TaskAvailability::Unavailable || out.availability==TaskAvailability::Available)out.availability=availability;
        out.reasons.push_back({kind,std::move(source),std::move(detail)});
    };
    if(trigger!=relation.trigger)reject(TaskAvailability::Unavailable,TaskReasonKind::Condition,"trigger","different control origin");
    for(const auto& condition:relation.conditions)std::visit([&](const auto& value) {
        using T=std::decay_t<decltype(value)>;bool matches=true;const char* name="control";
        if constexpr(std::is_same_v<T,ControlSelected>) {matches=state_.selection==value.skill;name="selection";}
        else if constexpr(std::is_same_v<T,ControlPerforming>) {
            matches=std::find(value.phases.begin(),value.phases.end(),state_.performance)!=value.phases.end();name="performance";
        } else if constexpr(std::is_same_v<T,ControlSourcePresent>) {
            matches=std::any_of(state_.sources.begin(),state_.sources.end(),[&](const auto& row){return row.id==value.source;})==value.present;name="source";
        } else if constexpr(std::is_same_v<T,ControlQueued>) {
            matches=std::any_of(state_.pending.begin(),state_.pending.end(),[&](const auto& row){return row.id==value.id;})==value.present;name="queue";
        } else if constexpr(std::is_same_v<T,ControlInsertionAt>) {
            matches=value.phase?(state_.insertion && state_.insertion->phase==*value.phase):!state_.insertion;name="insertion";
        } else if constexpr(std::is_same_v<T,ControlSlotMatches>) {
            auto expected=value.skill;expected.slot=value.slot;
            matches=slots_.Resolve(value.slot,value.child)==std::optional<ControlSkill>(expected);name="slot skill";
        } else if constexpr(std::is_same_v<T,ControlSlotAvailable>) {
            const auto availability=slots_.Available(value.slot);
            if(availability==TaskAvailability::Unknown)reject(availability,TaskReasonKind::Unresolved,"control slot","slot definition is unavailable");
            else {matches=availability==TaskAvailability::Available;name="control slot";}
        } else reject(TaskAvailability::Unknown,TaskReasonKind::Unresolved,value.source,value.detail);
        if(!matches)reject(TaskAvailability::Unavailable,TaskReasonKind::Condition,name,"control condition is false");
    },condition);
    // Inspect structural preconditions without mutating the actor. These are
    // also checked by the atomic commit, not separate UI-only restrictions.
    try {auto copy=*this;copy.Apply(relation.accepted,nullptr,true);}
    catch(const std::exception& error){reject(TaskAvailability::Unavailable,TaskReasonKind::Condition,"transition",error.what());}
    return out;
}
void BattleControl::Apply(const std::vector<ControlChange>& changes,ControlTicket* ticket,bool accepting) {
    for(const auto& change:changes) {
        const auto channels=Channels(change);
        if(!accepting && ticket && std::any_of(channels.begin(),channels.end(),[&](const auto& channel) {
            return ticket->versions.at(channel)!=versions_[channel];
        }))continue; // A newer accepted operation owns this channel.
        std::visit([&](const auto& value) {
            using T=std::decay_t<decltype(value)>;
            if constexpr(std::is_same_v<T,ControlSelect>)state_.selection=value.skill;
            else if constexpr(std::is_same_v<T,ControlSetPerformance>)state_.performance=value.phase;
            else if constexpr(std::is_same_v<T,ControlAddSource>) {
                const auto found=std::find_if(state_.sources.begin(),state_.sources.end(),[&](const auto& row){return row.id==value.source.id;});
                auto source=value.source;
                if(found==state_.sources.end()){source.order=++source_order_;state_.sources.push_back(std::move(source));}
                else {source.order=found->order;*found=std::move(source);}
            } else if constexpr(std::is_same_v<T,ControlRemoveSource>) {
                const auto found=std::find_if(state_.sources.begin(),state_.sources.end(),[&](const auto& row){return row.id==value.source;});
                Need(found!=state_.sources.end(),"control source is not active");state_.sources.erase(found);
            } else if constexpr(std::is_same_v<T,ControlQueueInsertion>) {
                Need(!state_.insertion || state_.insertion->id!=value.id,"insertion is already active");
                Need(std::none_of(state_.pending.begin(),state_.pending.end(),[&](const auto& row){return row.id==value.id;}),"insertion is already pending");
                state_.pending.push_back({value.id,value.skill});
            } else if constexpr(std::is_same_v<T,ControlEnterInsertion>) {
                Need(!state_.insertion,"another insertion is active");
                const auto found=std::find_if(state_.pending.begin(),state_.pending.end(),[&](const auto& row){return row.id==value.id;});
                Need(found!=state_.pending.end(),"insertion is not pending");
                auto next=*found;state_.pending.erase(found);
                next.return_selection=state_.selection;next.return_performance=state_.performance;
                state_.selection=next.skill;state_.performance=ControlPerformance::Ready;state_.insertion=std::move(next);
            } else if constexpr(std::is_same_v<T,ControlAdvanceInsertion>) {
                Need(state_.insertion.has_value(),"no active insertion");state_.insertion->phase=value.phase;
            } else if constexpr(std::is_same_v<T,ControlLeaveInsertion>) {
                Need(state_.insertion.has_value(),"no active insertion");
                state_.selection=state_.insertion->return_selection;state_.performance=state_.insertion->return_performance;state_.insertion.reset();
            } else if constexpr(std::is_same_v<T,ControlWithdrawInsertion>) {
                const auto found=std::find_if(state_.pending.begin(),state_.pending.end(),[&](const auto& row){return row.id==value.id;});
                Need(found!=state_.pending.end(),"insertion is not pending");state_.pending.erase(found);
            } else if constexpr(std::is_same_v<T,ControlChangeSlot>) {
                slots_.Apply(value.operation);
            }
        },change);
        for(const auto& channel:channels){versions_[channel]=++revision_;if(ticket)ticket->versions[channel]=versions_[channel];}
    }
    state_.dominant_source.reset();
    state_.slots=slots_.Snapshot();
    for(const auto& source:state_.sources)
        if(!state_.dominant_source || source.priority<state_.dominant_source->priority)state_.dominant_source=source;
}
ControlTicket BattleControl::Accept(const ControlRelation& relation,ControlTrigger trigger,std::uint64_t intent) {
    const auto query=Query(relation,trigger);
    if(query.availability!=TaskAvailability::Available)throw std::logic_error("control path was not accepted");
    Need(intent!=0,"control intent is zero");
    auto next=*this;ControlTicket ticket;ticket.intent=intent;ticket.epoch=epoch_;ticket.relation=relation;
    for(const auto* changes:{&relation.accepted,&relation.started,&relation.completed,&relation.interrupted,&relation.replaced,&relation.failed})
        for(const auto& change:*changes)for(const auto& channel:Channels(change))ticket.versions[channel]=next.versions_[channel];
    next.Apply(relation.accepted,&ticket,true);*this=std::move(next);return ticket;
}
void BattleControl::Started(ControlTicket& ticket) {
    if(ticket.epoch!=epoch_ || ticket.started || ticket.ended)return;
    auto next=*this;auto updated=ticket;next.Apply(ticket.relation.started,&updated,false);updated.started=true;
    *this=std::move(next);ticket=std::move(updated);
}
void BattleControl::End(ControlTicket& ticket,TaskExitReason reason) {
    if(ticket.epoch!=epoch_ || ticket.ended)return;
    auto next=*this;auto updated=ticket;
    // Each exit has its own authored result; none falls back to completion.
    if(reason==TaskExitReason::Completed)next.Apply(ticket.relation.completed,&updated,false);
    else if(reason==TaskExitReason::Interrupted)next.Apply(ticket.relation.interrupted,&updated,false);
    else if(reason==TaskExitReason::Replaced)next.Apply(ticket.relation.replaced,&updated,false);
    else if(reason==TaskExitReason::Failed)next.Apply(ticket.relation.failed,&updated,false);
    updated.ended=true;*this=std::move(next);ticket=std::move(updated);
}
BattleControl::BattleControl(SkillSlotSetup setup):slots_(std::move(setup)){state_.slots=slots_.Snapshot();}
void BattleControl::Reset(){state_={};slots_.Reset();state_.slots=slots_.Snapshot();versions_.clear();++epoch_;++revision_;}
}
