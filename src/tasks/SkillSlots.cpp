#include <hksr/framework/tasks/SkillSlots.hpp>
#include <algorithm>
#include <set>
#include <stdexcept>

namespace hksr::framework {
namespace {
void Need(bool ok,const char* message){if(!ok)throw std::invalid_argument(message);}
void Name(const std::string& value){Need(!value.empty() && value.find('\0')==std::string::npos,"invalid skill slot key or owner");}
}
void ValidateSkillSlots(const SkillSlotSetup& setup) {
    std::set<std::string> keys;std::set<std::pair<std::string,std::int32_t>> identities;
    for(const auto& skill:setup.skills) {
        Name(skill.key);Name(skill.skill.source);Need(skill.skill.index>=0 && !skill.skill.slot,"slot definition requires source/index identity");
        Need(keys.insert(skill.key).second && identities.emplace(skill.skill.source,skill.skill.index).second,"duplicate skill definition");
    }
    for(const auto& skill:setup.skills)for(const auto& child:skill.children)Need(keys.count(child)!=0,"unknown child skill key");
    std::map<std::string,std::string> parents;
    for(const auto& skill:setup.skills)for(const auto& child:skill.children)parents[child]=skill.key;
    for(const auto& skill:setup.skills) {
        std::set<std::string> seen;auto current=skill.key;
        while(parents.count(current)){Need(seen.insert(current).second,"cyclic child skill parent binding");current=parents.at(current);}
    }
    for(const auto& [slot,key]:setup.defaults) {
        Need(slot>=0,"negative control slot");if(key)Need(keys.count(*key)!=0,"unknown default skill key");
    }
}
void ValidateSkillSlotChange(const SkillSlotChange& change) {
    std::visit([](const auto& value) {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,ReplaceSkillKey>) {Name(value.owner);Name(value.from);if(value.to)Name(*value.to);}
        else {
            Need(value.slot>=0,"negative control slot");
            if constexpr(std::is_same_v<T,MapControlSlot>) {if(value.key)Name(*value.key);}
            else Name(value.owner);
        }
    },change);
}
std::string SkillSlotChannel(const SkillSlotChange& change) {
    return std::visit([](const auto& value)->std::string {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,ReplaceSkillKey>)return "skill-map/"+std::to_string(value.owner.size())+":"+value.owner+value.from;
        else if constexpr(std::is_same_v<T,MapControlSlot>)return "slot-map/"+std::to_string(value.slot);
        else if constexpr(std::is_same_v<T,DisableControlSlot>)return "slot-disable/"+std::to_string(value.slot)+"/"+value.owner;
        else return "slot-occupation/"+std::to_string(value.slot);
    },change);
}
SkillSlots::SkillSlots(SkillSlotSetup setup):setup_(std::move(setup)) {
    ValidateSkillSlots(setup_);
    for(const auto& skill:setup_.skills)for(const auto& child:skill.children)parents_[child]=skill.key;
    Reset();
}
const SlotSkillDefinition* SkillSlots::Find(const std::string& key) const {
    const auto found=std::find_if(setup_.skills.begin(),setup_.skills.end(),[&](const auto& row){return row.key==key;});
    return found==setup_.skills.end()?nullptr:&*found;
}
std::optional<std::string> SkillSlots::Mapped(const std::optional<std::string>& key) const {
    if(!key)return {};
    // The official lookup chooses the newest matching entry once, then does
    // literal SkillData lookup. It does not recursively follow alias chains.
    for(auto it=state_.mappings.rbegin();it!=state_.mappings.rend();++it)
        if(it->from==*key)return Find(it->to)?std::optional<std::string>(it->to):std::nullopt;
    return Find(*key)?key:std::nullopt;
}
std::optional<std::int32_t> SkillSlots::Binding(const std::string& key) const {
    auto current=key;
    for(;;) {
        if(const auto found=state_.bindings.find(current);found!=state_.bindings.end())return found->second;
        const auto parent=parents_.find(current);if(parent==parents_.end())return {};current=parent->second;
    }
}
void SkillSlots::Assign(std::int32_t slot,std::optional<std::string> key) {
    auto& target=state_.slots.at(slot);if(!key)key=target.default_key;
    if(target.current==key){if(key)state_.bindings[*key]=slot;return;}
    if(target.current && Binding(*target.current)==slot)state_.bindings.erase(*target.current);
    target.current=std::move(key);
    if(target.current) {
        // SkillData resolves its binding through its parent if it has none.
        // Moving it clears that prior slot, without restoring a default there.
        const auto previous=Binding(*target.current);
        if(previous && *previous!=slot)state_.slots.at(*previous).current.reset();
        state_.bindings[*target.current]=slot;
    }
}
void SkillSlots::Map(std::int32_t slot,std::optional<std::string> key) {
    auto& target=state_.slots.at(slot);
    if(target.occupation) {
        target.occupation->deferred=std::move(key);target.occupation->changed=true;return;
    }
    target.key=key;Assign(slot,Mapped(key));
}
void SkillSlots::Reset() {
    state_={};for(const auto& [slot,key]:setup_.defaults) {
        state_.slots.emplace(slot,SkillSlotState{key,key,key});if(key)state_.bindings[*key]=slot;
    }
}
void SkillSlots::Apply(const SkillSlotChange& change) {
    ValidateSkillSlotChange(change);
    std::visit([&](const auto& value) {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,ReplaceSkillKey>) {
            auto found=std::find_if(state_.mappings.begin(),state_.mappings.end(),[&](const auto& row){return row.owner==value.owner && row.from==value.from;});
            if(!value.to || value.to->empty() || *value.to==value.from) {
                if(found==state_.mappings.end())return;
                state_.mappings.erase(found);
            }else if(found!=state_.mappings.end()) {
                if(found->to==*value.to)return;
                found->to=*value.to;
            }
            else state_.mappings.push_back({value.owner,value.from,*value.to});
            // This key-replacement operation updates matching slots even if
            // occupied. Only direct slot remapping has the deferred contract.
            for(auto& [slot,row]:state_.slots)if(row.key==value.from)Assign(slot,Mapped(row.key));
        }else {
            const auto found=state_.slots.find(value.slot);Need(found!=state_.slots.end(),"control slot is not defined");auto& slot=found->second;
            if constexpr(std::is_same_v<T,MapControlSlot>)Map(value.slot,value.key);
            else if constexpr(std::is_same_v<T,OccupyControlSlot>) {
                Need(!slot.occupation || slot.occupation->owner==value.owner,"control slot has another occupation owner");
                if(!slot.occupation)slot.occupation=SkillSlotOccupation{value.owner,slot.key};
            }else if constexpr(std::is_same_v<T,ReleaseControlSlot>) {
                if(!slot.occupation || slot.occupation->owner!=value.owner)return;
                const auto record=*slot.occupation;slot.occupation.reset();
                const auto key=record.changed?record.deferred:record.previous;
                // The entity-side release wrapper compares the effective
                // current trigger key before requesting another remapping.
                if(!key || slot.current!=key)Map(value.slot,key);
            }else if constexpr(std::is_same_v<T,DisableControlSlot>) {
                const auto owner=std::find(slot.disables.begin(),slot.disables.end(),value.owner);
                if(value.disabled && owner==slot.disables.end())slot.disables.push_back(value.owner);
                else if(!value.disabled && owner!=slot.disables.end())slot.disables.erase(owner);
            }
        }
    },change);
}
std::optional<ControlSkill> SkillSlots::Resolve(std::int32_t slot,std::optional<std::size_t> child) const {
    const auto found=state_.slots.find(slot);if(found==state_.slots.end() || !found->second.current)return {};
    const auto* skill=Find(*found->second.current);if(!skill)return {};
    if(child) {
        if(*child>=skill->children.size())return {};
        skill=Find(skill->children[*child]);if(!skill)return {};
    }
    auto result=skill->skill;result.slot=slot;return result;
}
TaskAvailability SkillSlots::Available(std::int32_t slot) const {
    const auto found=state_.slots.find(slot);
    if(found==state_.slots.end())return TaskAvailability::Unknown;
    return found->second.disables.empty() && found->second.current?TaskAvailability::Available:TaskAvailability::Unavailable;
}
}
