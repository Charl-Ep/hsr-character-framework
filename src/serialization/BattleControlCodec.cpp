#include "BattleControlCodec.hpp"
#include <nlohmann/json.hpp>
#include <limits>
#include <stdexcept>
#include <cmath>

namespace hksr::framework::control_codec {
namespace {
using Json=nlohmann::json;
void Fields(const Json& row,std::initializer_list<const char*> names) {
    if(!row.is_object())throw std::invalid_argument("control object expected");
    for(auto it=row.begin();it!=row.end();++it) {
        bool known=false;for(auto name:names)known=known || it.key()==name;
        if(!known)throw std::invalid_argument("unknown control field: "+it.key());
    }
}
std::string Name(const Json& row) {
    auto out=row.get<std::string>();
    if(out.empty() || out.find('\0')!=std::string::npos)throw std::invalid_argument("empty control name");
    return out;
}
std::int32_t Int(const Json& row) {
    if(!row.is_number_integer() || (row.is_number_unsigned() && row.get<std::uint64_t>()>INT32_MAX) ||
        row.get<std::int64_t>()<INT32_MIN || row.get<std::int64_t>()>INT32_MAX)
        throw std::invalid_argument("control integer out of range");
    return row.get<std::int32_t>();
}
template<class T> T Enum(const Json& row,std::initializer_list<T> values) {
    const auto name=Name(row);for(auto value:values)if(name==ControlName(value))return value;
    throw std::invalid_argument("unknown control value: "+name);
}
ControlPerformance Performance(const Json& row) {
    return Enum<ControlPerformance>(row,{ControlPerformance::Ready,ControlPerformance::Accepted,ControlPerformance::Working,
        ControlPerformance::Finished,ControlPerformance::Resetting,ControlPerformance::Failed});
}
ControlInsertionPhase Phase(const Json& row) {
    return Enum<ControlInsertionPhase>(row,{ControlInsertionPhase::Preparing,ControlInsertionPhase::Waiting,
        ControlInsertionPhase::Executing,ControlInsertionPhase::Finishing});
}
ControlSkill Skill(const Json& row) {
    Fields(row,{"source","index","slot"});ControlSkill out{Name(row.at("source")),Int(row.at("index"))};
    if(row.contains("slot"))out.slot=Int(row.at("slot"));return out;
}
std::optional<ControlSkill> Selection(const Json& row) {return row.is_null()?std::nullopt:std::optional<ControlSkill>(Skill(row));}
std::optional<std::string> Key(const Json& row){return row.is_null()?std::nullopt:std::optional<std::string>(Name(row));}
ControlRequirement Requirement(const Json& row) {
    const auto op=Name(row.at("op"));
    if(op=="selected") {Fields(row,{"op","skill"});return ControlSelected{Selection(row.at("skill"))};}
    if(op=="slot_skill") {
        Fields(row,{"op","slot","skill","child"});ControlSlotMatches out{Int(row.at("slot")),Skill(row.at("skill"))};
        if(row.contains("child")){const auto child=Int(row.at("child"));if(child<0)throw std::invalid_argument("negative child index");out.child=static_cast<std::size_t>(child);}return out;
    }
    if(op=="slot_available") {Fields(row,{"op","slot"});return ControlSlotAvailable{Int(row.at("slot"))};}
    if(op=="performance") {
        Fields(row,{"op","in"});ControlPerforming out;
        for(const auto& phase:row.at("in").get<std::vector<Json>>())out.phases.push_back(Performance(phase));return out;
    }
    if(op=="source") {Fields(row,{"op","id","present"});return ControlSourcePresent{Name(row.at("id")),row.value("present",true)};}
    if(op=="queued") {Fields(row,{"op","id","present"});return ControlQueued{Name(row.at("id")),row.value("present",true)};}
    if(op=="insertion") {
        Fields(row,{"op","phase"});return ControlInsertionAt{row.at("phase").is_null()?std::nullopt:std::optional<ControlInsertionPhase>(Phase(row.at("phase")))};
    }
    if(op=="unresolved") {Fields(row,{"op","source","detail"});return TaskUnresolvedRequirement{Name(row.at("source")),Name(row.at("detail"))};}
    throw std::invalid_argument("unknown control condition: "+op);
}
ControlChange Change(const Json& row) {
    const auto op=Name(row.at("op"));
    if(op=="map_slot") {Fields(row,{"op","slot","key"});return ControlChangeSlot{MapControlSlot{Int(row.at("slot")),Key(row.at("key"))}};}
    if(op=="replace_skill") {Fields(row,{"op","owner","from","to"});return ControlChangeSlot{ReplaceSkillKey{Name(row.at("owner")),Name(row.at("from")),Key(row.at("to"))}};}
    if(op=="occupy_slot") {Fields(row,{"op","slot","owner"});return ControlChangeSlot{OccupyControlSlot{Int(row.at("slot")),Name(row.at("owner"))}};}
    if(op=="release_slot") {Fields(row,{"op","slot","owner"});return ControlChangeSlot{ReleaseControlSlot{Int(row.at("slot")),Name(row.at("owner"))}};}
    if(op=="disable_slot") {Fields(row,{"op","slot","owner","disabled"});return ControlChangeSlot{DisableControlSlot{Int(row.at("slot")),Name(row.at("owner")),row.value("disabled",true)}};}
    if(op=="select") {Fields(row,{"op","skill"});return ControlSelect{Selection(row.at("skill"))};}
    if(op=="performance") {Fields(row,{"op","phase"});return ControlSetPerformance{Performance(row.at("phase"))};}
    if(op=="add_source") {
        Fields(row,{"op","id","flag","priority","behaviors","hit_effects"});ControlSource source{Name(row.at("id")),Int(row.at("flag")),Int(row.at("priority"))};
        if(row.contains("behaviors")) {
            source.behaviors=std::vector<std::int32_t>{};
            for(const auto& flag:row.at("behaviors").get<std::vector<Json>>())source.behaviors->push_back(Int(flag));
        }
        if(row.contains("hit_effects"))for(const auto& value:row.at("hit_effects").get<std::vector<Json>>()) {
            Fields(value,{"stacking","path","death_path","priority","count","death_count","offset","keep_original","replay_on_mutex"});
            HitEffectOverride effect;const auto mode=Name(value.at("stacking"));
            if(mode=="multiple")effect.stacking=HitEffectStacking::Multiple;
            else if(mode!="highest")throw std::invalid_argument("unknown hit effect stacking");
            if(!value.at("path").is_null())effect.path=value.at("path").get<std::string>();
            effect.death_path=value.value("death_path",std::string{});
            if((effect.path && effect.path->find('\0')!=std::string::npos) || effect.death_path.find('\0')!=std::string::npos)
                throw std::invalid_argument("invalid hit effect source path");
            effect.priority=Int(value.at("priority"));effect.count=Int(value.value("count",Json(1)));
            effect.death_count=Int(value.value("death_count",Json(0)));
            effect.keep_original=value.value("keep_original",false);effect.replay_on_mutex=value.value("replay_on_mutex",false);
            if(value.contains("offset")) {
                const auto xyz=value.at("offset").get<std::vector<float>>();
                if(xyz.size()!=3 || !std::isfinite(xyz[0]) || !std::isfinite(xyz[1]) || !std::isfinite(xyz[2]))
                    throw std::invalid_argument("invalid hit effect offset");
                effect.offset=api::Position3{xyz[0],xyz[1],xyz[2]};
            }
            source.hit_effects.push_back(std::move(effect));
        }
        return ControlAddSource{std::move(source)};
    }
    if(op=="remove_source") {Fields(row,{"op","id"});return ControlRemoveSource{Name(row.at("id"))};}
    if(op=="queue") {Fields(row,{"op","id","skill"});return ControlQueueInsertion{Name(row.at("id")),Skill(row.at("skill"))};}
    if(op=="enter_insertion") {Fields(row,{"op","id"});return ControlEnterInsertion{Name(row.at("id"))};}
    if(op=="insertion") {Fields(row,{"op","phase"});return ControlAdvanceInsertion{Phase(row.at("phase"))};}
    if(op=="leave_insertion") {Fields(row,{"op"});return ControlLeaveInsertion{};}
    if(op=="withdraw") {Fields(row,{"op","id"});return ControlWithdrawInsertion{Name(row.at("id"))};}
    throw std::invalid_argument("unknown control change: "+op);
}
Json SkillValue(const ControlSkill& skill) {
    Json out={{"source",skill.source},{"index",skill.index}};if(skill.slot)out["slot"]=*skill.slot;return out;
}
Json SelectionValue(const std::optional<ControlSkill>& skill){return skill?SkillValue(*skill):Json(nullptr);}
Json InsertionValue(const ControlInsertion& value) {
    return {{"id",value.id},{"skill",SkillValue(value.skill)},{"phase",ControlName(value.phase)},
        {"return_selection",SelectionValue(value.return_selection)},{"return_performance",ControlName(value.return_performance)}};
}
Json SourceValue(const ControlSource& value){
    Json out={{"id",value.id},{"flag",value.flag},{"priority",value.priority},{"order",value.order}};
    out["behaviors"]=value.behaviors?Json(*value.behaviors):Json(nullptr);
    out["hit_effects"]=Json::array();out["hit_effect_version"]=value.hit_effect_version;
    for(const auto& effect:value.hit_effects) {
        Json row={{"stacking",effect.stacking==HitEffectStacking::Multiple?"multiple":"highest"},
            {"path",effect.path?Json(*effect.path):Json(nullptr)},{"death_path",effect.death_path},{"priority",effect.priority},
            {"count",effect.count},{"death_count",effect.death_count},{"keep_original",effect.keep_original},{"replay_on_mutex",effect.replay_on_mutex}};
        if(effect.offset)row["offset"]={effect.offset->x,effect.offset->y,effect.offset->z};out["hit_effects"].push_back(std::move(row));
    }
    return out;
}
Json KeyValue(const std::optional<std::string>& value){return value?Json(*value):Json(nullptr);}
Json SlotsValue(const SkillSlotsSnapshot& state) {
    Json out={{"slots",Json::array()},{"mappings",Json::array()},{"bindings",state.bindings}};
    for(const auto& [index,slot]:state.slots) {
        Json occupation=nullptr;
        if(slot.occupation) {
            const auto& value=*slot.occupation;
            occupation={{"owner",value.owner},{"previous",KeyValue(value.previous)},{"deferred",KeyValue(value.deferred)},{"changed",value.changed}};
        }
        out["slots"].push_back({{"slot",index},{"default",KeyValue(slot.default_key)},{"key",KeyValue(slot.key)},
            {"current",KeyValue(slot.current)},{"occupation",std::move(occupation)},{"disables",slot.disables}});
    }
    for(const auto& value:state.mappings)out["mappings"].push_back({{"owner",value.owner},{"from",value.from},{"to",value.to}});
    return out;
}
}
SkillSlotSetup Slots(const Json& row) {
    Fields(row,{"skills","defaults"});SkillSlotSetup out;
    for(const auto& skill:row.at("skills").get<std::vector<Json>>()) {
        Fields(skill,{"skill","key","children"});SlotSkillDefinition definition{Skill(skill.at("skill")),Name(skill.at("key"))};
        if(skill.contains("children"))for(const auto& child:skill.at("children").get<std::vector<Json>>())definition.children.push_back(Name(child));
        out.skills.push_back(std::move(definition));
    }
    for(const auto& entry:row.at("defaults").get<std::vector<Json>>()) {
        Fields(entry,{"slot","key"});if(!out.defaults.emplace(Int(entry.at("slot")),Key(entry.at("key"))).second)
            throw std::invalid_argument("duplicate default control slot");
    }
    ValidateSkillSlots(out);return out;
}
std::vector<ControlChange> Changes(const Json& row) {
    std::vector<ControlChange> out;
    for(const auto& value:row.get<std::vector<Json>>())out.push_back(Change(value));
    ValidateControlChanges(out);return out;
}
ControlRelation Parse(const Json& row) {
    Fields(row,{"label","trigger","presentation","sources","requires","accepted","started","completed","interrupted","replaced","failed"});
    ControlRelation out;out.label=row.value("label",std::string{});out.presentation=row.value("presentation",true);
    out.trigger=Enum<ControlTrigger>(row.at("trigger"),{ControlTrigger::User,ControlTrigger::Event,ControlTrigger::Automatic});
    for(const auto& source:row.at("sources").get<std::vector<Json>>()) {
        Fields(source,{"document","field"});out.sources.push_back({Name(source.at("document")),Name(source.at("field"))});
    }
    if(row.contains("requires"))for(const auto& condition:row.at("requires").get<std::vector<Json>>())out.conditions.push_back(Requirement(condition));
    for(const auto& [name,changes]:std::initializer_list<std::pair<const char*,std::vector<ControlChange>*>>{
        {"accepted",&out.accepted},{"started",&out.started},{"completed",&out.completed},
        {"interrupted",&out.interrupted},{"replaced",&out.replaced},{"failed",&out.failed}})
        if(row.contains(name))*changes=Changes(row.at(name));
    ValidateControlRelation(out);return out;
}
Json Snapshot(const BattleControlSnapshot& value) {
    Json out={{"slots",SlotsValue(value.slots)},{"selection",SelectionValue(value.selection)},{"performance",ControlName(value.performance)},
        {"pending",Json::array()},{"insertion",value.insertion?InsertionValue(*value.insertion):Json(nullptr)},
        {"sources",Json::array()},{"dominant_source",value.dominant_source?SourceValue(*value.dominant_source):Json(nullptr)}};
    for(const auto& pending:value.pending)out["pending"].push_back(InsertionValue(pending));
    for(const auto& source:value.sources)out["sources"].push_back(SourceValue(source));return out;
}
}
