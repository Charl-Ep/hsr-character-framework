#pragma once
#include <hksr/framework/character/CharacterDirectory.hpp>
#include <hksr/framework/assets/CharacterPackage.hpp>
#include <hksr/framework/catalog/SourceList.hpp>
#include <imgui.h>
#include "Localization.hpp"

namespace hksr::framework::ui {
inline void Text(const std::string& value) {if(!value.empty())ImGui::TextWrapped("%s",value.c_str());}
inline std::string ResourceName(const std::string& path) {
    auto name=SourceFile(path);
    for(const auto* prefix:{"LocalPlayer_","NPC_Avatar_","NPC_Normal_","NPC_Special_"})
        if(name.rfind(prefix,0)==0){name.erase(0,std::char_traits<char>::length(prefix));break;}
    const std::string suffix="_Config.json";
    if(name.size()>=suffix.size() && name.compare(name.size()-suffix.size(),suffix.size(),suffix)==0)name.resize(name.size()-suffix.size());
    return name;
}
inline std::string FriendlyName(const api::PlayerCatalogRow& p,Language language=DefaultLocale) {
    for(const auto& family:OfficialCharacterFamilies())
        if(std::find(family.player_configs.begin(),family.player_configs.end(),p.character_config)!=family.player_configs.end()) {
            const auto* name=LocalizedSourceName(family.key,language);
            if(*name)return std::string(name)+" / "+ResourceName(p.character_config);
        }
    return ResourceName(p.character_config);
}
inline std::string FriendlyName(const api::NpcCatalogRow& p,Language language=DefaultLocale) {
    for(const auto& family:OfficialCharacterFamilies())for(const auto& n:family.npcs)
        if(n.config==p.character_config && n.model==p.model_config) {
            const auto* name=LocalizedSourceName(family.key,language);
            if(*name)return std::string(name)+" / "+ResourceName(p.character_config);
        }
    return ResourceName(p.character_config);
}
inline bool PureNpc(const CharacterPackage& p) {return p.source==SourceKind::Official && !p.appearance;}
template<class Row> std::string SearchNames(const Row& row) {
    std::string names;
    for(const auto& language:AvailableLanguages())names+=" "+FriendlyName(row,language.value);
    return names;
}
inline const char* SourceMode(const CharacterPackage& p,Language language=DefaultLocale) {
    return Translate(p.source==SourceKind::External?UiText::ExternalPackage:PureNpc(p)?UiText::OfficialNpc:UiText::PlayerMode,language);
}
inline std::string ActorName(const CharacterPackage& p,const api::CharacterCatalog* rows,Language language=DefaultLocale) {
    if(p.source==SourceKind::External)return p.display_name.empty()?p.id:p.display_name;
    if(rows) {
        if(p.appearance)for(const auto& row:rows->players.rows)
            if(row.character_config==p.appearance->character_config && row.prefab==p.appearance->template_prefab)return FriendlyName(row,language);
        if(!p.appearance)for(const auto& row:rows->npcs.rows)if(row.id==p.npc_id)return FriendlyName(row,language);
    }
    return p.appearance?ResourceName(p.appearance->character_config):Translate(UiText::OfficialNpc,language);
}
}
