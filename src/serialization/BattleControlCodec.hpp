#pragma once
#include <hksr/framework/tasks/BattleControl.hpp>
#include <nlohmann/json_fwd.hpp>

namespace hksr::framework::control_codec {
ControlRelation Parse(const nlohmann::json&);
std::vector<ControlChange> Changes(const nlohmann::json&);
SkillSlotSetup Slots(const nlohmann::json&);
nlohmann::json Snapshot(const BattleControlSnapshot&);
}
