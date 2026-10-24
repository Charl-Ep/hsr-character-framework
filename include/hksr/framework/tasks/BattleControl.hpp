#pragma once
#include "TaskModel.hpp"
#include "SkillSlots.hpp"
#include <deque>

namespace hksr::framework {
enum class ControlTrigger { User,Event,Automatic };
enum class ControlPerformance { Ready,Accepted,Working,Finished,Resetting,Failed };
enum class ControlInsertionPhase { Preparing,Waiting,Executing,Finishing };
struct ControlSource {
    std::string id;
    std::int32_t flag=0,priority=0;
    std::uint64_t order=0;
    bool operator==(const ControlSource& other) const {return std::tie(id,flag,priority,order)==std::tie(other.id,other.flag,other.priority,other.order);}
};
struct ControlInsertion {
    std::string id;
    ControlSkill skill;
    std::optional<ControlSkill> return_selection;
    ControlPerformance return_performance=ControlPerformance::Ready;
    ControlInsertionPhase phase=ControlInsertionPhase::Preparing;
    bool operator==(const ControlInsertion& other) const {
        return id==other.id && skill==other.skill && return_selection==other.return_selection &&
            return_performance==other.return_performance && phase==other.phase;
    }
};
struct BattleControlSnapshot {
    SkillSlotsSnapshot slots;
    std::optional<ControlSkill> selection;
    ControlPerformance performance=ControlPerformance::Ready;
    std::deque<ControlInsertion> pending;
    std::optional<ControlInsertion> insertion;
    std::vector<ControlSource> sources;
    std::optional<ControlSource> dominant_source;
    bool operator==(const BattleControlSnapshot& other) const {
        return slots==other.slots && selection==other.selection && performance==other.performance && pending==other.pending &&
            insertion==other.insertion && sources==other.sources && dominant_source==other.dominant_source;
    }
};

struct ControlSelected {std::optional<ControlSkill> skill;};
struct ControlPerforming {std::vector<ControlPerformance> phases;};
struct ControlSourcePresent {std::string source;bool present=true;};
struct ControlInsertionAt {std::optional<ControlInsertionPhase> phase;};
struct ControlQueued {std::string id;bool present=true;};
struct ControlSlotMatches {std::int32_t slot;ControlSkill skill;std::optional<std::size_t> child;};
struct ControlSlotAvailable {std::int32_t slot;};
using ControlRequirement=std::variant<ControlSelected,ControlPerforming,ControlSourcePresent,
    ControlInsertionAt,ControlQueued,ControlSlotMatches,ControlSlotAvailable,TaskUnresolvedRequirement>;

struct ControlSelect {std::optional<ControlSkill> skill;};
struct ControlSetPerformance {ControlPerformance phase;};
struct ControlAddSource {ControlSource source;};
struct ControlRemoveSource {std::string source;};
struct ControlQueueInsertion {std::string id;ControlSkill skill;};
struct ControlEnterInsertion {std::string id;};
struct ControlAdvanceInsertion {ControlInsertionPhase phase;};
struct ControlLeaveInsertion {};
struct ControlWithdrawInsertion {std::string id;};
struct ControlChangeSlot {SkillSlotChange operation;};
using ControlChange=std::variant<ControlSelect,ControlSetPerformance,ControlAddSource,ControlRemoveSource,
    ControlQueueInsertion,ControlEnterInsertion,ControlAdvanceInsertion,ControlLeaveInsertion,ControlWithdrawInsertion,ControlChangeSlot>;

// Authored control paths refer to official skill identities and producers.
// Presentation plans do not supply permission or infer these transitions.
struct ControlRelation {
    std::string label;
    ControlTrigger trigger=ControlTrigger::User;
    bool presentation=true;
    std::vector<TaskSourceLocation> sources;
    std::vector<ControlRequirement> conditions;
    std::vector<ControlChange> accepted,started,completed,interrupted,replaced,failed;
};
struct ControlTicket {
    std::uint64_t intent=0,epoch=0;
    std::map<std::string,std::uint64_t> versions;
    ControlRelation relation;
    bool started=false,ended=false;
};

// Value-only, one owned actor. No game entity, global event, battle resource
// or native call is involved. A caller supplies reviewed relations and drives
// their lifecycle using the existing presentation owner's receipts.
class BattleControl {
public:
    explicit BattleControl(SkillSlotSetup={});
    TaskQueryResult Query(const ControlRelation&,ControlTrigger) const;
    ControlTicket Accept(const ControlRelation&,ControlTrigger,std::uint64_t intent,
        const std::vector<ControlChange>& sequence_changes={});
    // Sequence changes share the accepted operation's channel ownership. False
    // means this operation ended or another operation replaced one of its writes.
    bool Advance(ControlTicket&,const std::vector<ControlChange>&);
    void Started(ControlTicket&);
    void End(ControlTicket&,TaskExitReason);
    const BattleControlSnapshot& Snapshot() const {return state_;}
    void Reset();
private:
    SkillSlots slots_;
    BattleControlSnapshot state_;
    std::map<std::string,std::uint64_t> versions_;
    std::uint64_t revision_=0,source_order_=0,epoch_=1;
    void Apply(const std::vector<ControlChange>&,ControlTicket*,bool accepting);
};
void ValidateControlRelation(const ControlRelation&);
void ValidateControlChanges(const std::vector<ControlChange>&);
const char* ControlName(ControlPerformance);
const char* ControlName(ControlInsertionPhase);
const char* ControlName(ControlTrigger);
}
