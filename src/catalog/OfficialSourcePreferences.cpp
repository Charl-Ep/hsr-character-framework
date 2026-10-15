#include <hksr/framework/catalog/OfficialSourcePreferences.hpp>

namespace hksr::framework {
const std::vector<OfficialSourcePreference>& OfficialSourcePreferences() {
    // Reviewed resource references, HSR 4.6.0. Keep policy here rather than
    // character-name conditionals in the resolver or separate character packs.
    // Git records revisions; no per-version copies, hashes or asset reads.
    static const std::vector<OfficialSourcePreference> rows=[] {
        std::vector<OfficialSourcePreference> result={
        {"停云：常规NPC基础",
         "Config/ConfigCharacter/LocalPlayer/LocalPlayer_Tingyun_00_Config.json",
         "Config/ConfigCharacter/NPC/Avatar/NPC_Avatar_Tingyun_00_Config.json",
         "Config/ConfigEntity/NPC/Avatar/NPC_Avatar_Maid_Tingyun_00_Entity.json",
         "docs/source-policy.md#player-carriers"},
        {"刃：刃的NPC基础",
         "Config/ConfigCharacter/LocalPlayer/LocalPlayer_Ren_00_Config.json",
         "Config/ConfigCharacter/NPC/Avatar/NPC_Avatar_Ren_00_Config.json",
         "Config/ConfigEntity/NPC/Avatar/NPC_Avatar_Male_Ren_00_Entity.json",
         "docs/source-policy.md#player-carriers"},
        {"远坂凛：常规NPC基础",
         "Config/ConfigCharacter/LocalPlayer/LocalPlayer_TohsakaRin_00_Config.json",
         "Config/ConfigCharacter/NPC/Avatar/NPC_Avatar_TohsakaRin_00_Config.json",
         "Config/ConfigEntity/NPC/Avatar/NPC_Avatar_Maid_TohsakaRin_00_Entity.json",
         "docs/source-policy.md#player-carriers"},
        {"飞霄：对应资源的NPC基础",
         "Config/ConfigCharacter/LocalPlayer/LocalPlayer_Feixiao_00_Config.json",
         "Config/ConfigCharacter/NPC/Avatar/NPC_Avatar_Feixiao_00_Config.json",
         "Config/ConfigEntity/NPC/Avatar/NPC_Avatar_Lady_Feixiao_00_Entity.json",
         "docs/source-policy.md#player-carriers"},
        {"万敌：普通尺寸NPC基础",
         "Config/ConfigCharacter/LocalPlayer/LocalPlayer_Mydeimos_00_Config.json",
         "Config/ConfigCharacter/NPC/Avatar/NPC_Avatar_Mydeimos_00_Config.json",
         "Config/ConfigEntity/NPC/Avatar/NPC_Avatar_Male_Mydeimos_00_Entity.json",
         "docs/source-policy.md#player-carriers"},
        {"砂金·戏浪：对应资源的NPC基础",
         "Config/ConfigCharacter/LocalPlayer/LocalPlayer_AventurineW_00_Config.json",
         "Config/ConfigCharacter/NPC/Avatar/NPC_Avatar_AventurineW_00_Config.json",
         "Config/ConfigEntity/NPC/Avatar/NPC_Avatar_Lad_AventurineW_00_Entity.json",
         "docs/source-policy.md#player-carriers"}
        };
        // Ten catalog paths observed in 4.6; this is a reviewed carrier policy,
        // not an inference that every future suffix is supported. The selected
        // Avatar ID separately drives official outfit and battle sources.
        for(const auto* sex:{"Boy","Girl"})for(const auto* path:{"00","10","20","30","40"}) {
            const auto hero=std::string("Player")+sex;
            const auto rig=std::string(sex)=="Boy"?"Lad":"Miss";
            result.push_back({"主角：同性别NPC基础",
                "Config/ConfigCharacter/LocalPlayer/LocalPlayer_"+hero+"_"+path+"_Config.json",
                "Config/ConfigCharacter/NPC/Avatar/NPC_Avatar_"+hero+"_00_Config.json",
                std::string("Config/ConfigEntity/NPC/Avatar/NPC_Avatar_")+rig+"_"+hero+"_00_Entity.json",
                "docs/source-policy.md#protagonist-carriers"});
        }
        return result;
    }();
    return rows;
}
}
