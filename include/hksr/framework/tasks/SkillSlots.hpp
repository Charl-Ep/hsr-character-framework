#pragma once
#include "TaskModel.hpp"

namespace hksr::framework {
struct ControlSkill {
    std::string source;
    std::int32_t index=0;
    std::optional<std::int32_t> slot;
    bool operator==(const ControlSkill& other) const {
        return source==other.source && index==other.index && slot==other.slot;
    }
};
struct SlotSkillDefinition {
    ControlSkill skill;
    std::string key;
    std::vector<std::string> children;
};
struct SkillSlotSetup {
    std::vector<SlotSkillDefinition> skills;
    std::map<std::int32_t,std::optional<std::string>> defaults;
};
struct SkillSlotOccupation {
    std::string owner;
    std::optional<std::string> previous,deferred;
    bool changed=false;
    bool operator==(const SkillSlotOccupation& other) const {
        return std::tie(owner,previous,deferred,changed)==std::tie(other.owner,other.previous,other.deferred,other.changed);
    }
};
struct SkillSlotState {
    std::optional<std::string> default_key,key,current;
    std::optional<SkillSlotOccupation> occupation;
    std::vector<std::string> disables;
    bool operator==(const SkillSlotState& other) const {
        return std::tie(default_key,key,current,occupation,disables)==std::tie(other.default_key,other.key,other.current,other.occupation,other.disables);
    }
};
struct SkillKeyMapping {
    std::string owner,from,to;
    bool operator==(const SkillKeyMapping& other) const {return std::tie(owner,from,to)==std::tie(other.owner,other.from,other.to);}
};
struct SkillSlotsSnapshot {
    std::map<std::int32_t,SkillSlotState> slots;
    std::vector<SkillKeyMapping> mappings;
    std::map<std::string,std::int32_t> bindings;
    bool operator==(const SkillSlotsSnapshot& other) const {return slots==other.slots && mappings==other.mappings && bindings==other.bindings;}
};
struct MapControlSlot {std::int32_t slot;std::optional<std::string> key;};
struct ReplaceSkillKey {std::string owner,from;std::optional<std::string> to;};
struct OccupyControlSlot {std::int32_t slot;std::string owner;};
struct ReleaseControlSlot {std::int32_t slot;std::string owner;};
struct DisableControlSlot {std::int32_t slot;std::string owner;bool disabled=true;};
using SkillSlotChange=std::variant<MapControlSlot,ReplaceSkillKey,OccupyControlSlot,ReleaseControlSlot,DisableControlSlot>;

// Actor-local skill identities and mapping ownership. Keys are exact config
// values, not Animator names. Does not call official battle services/events.
class SkillSlots {
public:
    explicit SkillSlots(SkillSlotSetup={});
    void Apply(const SkillSlotChange&);
    std::optional<ControlSkill> Resolve(std::int32_t slot,std::optional<std::size_t> child={}) const;
    TaskAvailability Available(std::int32_t slot) const;
    const SkillSlotsSnapshot& Snapshot() const {return state_;}
    void Reset();
private:
    SkillSlotSetup setup_;
    SkillSlotsSnapshot state_;
    std::map<std::string,std::string> parents_;
    const SlotSkillDefinition* Find(const std::string&) const;
    std::optional<std::int32_t> Binding(const std::string&) const;
    std::optional<std::string> Mapped(const std::optional<std::string>&) const;
    void Assign(std::int32_t,std::optional<std::string>);
    void Map(std::int32_t,std::optional<std::string>);
};
void ValidateSkillSlots(const SkillSlotSetup&);
void ValidateSkillSlotChange(const SkillSlotChange&);
std::string SkillSlotChannel(const SkillSlotChange&);
}
