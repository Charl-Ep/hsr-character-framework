#include <hksr/framework/assets/CharacterPackage.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <limits>
#include "../assets/RelativePath.hpp"

namespace hksr::framework {
namespace {
using Json=nlohmann::json;
void Need(bool condition,const char* message){if(!condition)throw std::invalid_argument(message);}
std::string Text(const Json& row,const char* key){auto value=row.at(key).get<std::string>();Need(!value.empty(),key);return value;}
}
CharacterPackage ParseCharacterPackage(std::string_view text){
    const auto data=Json::parse(text.begin(),text.end());
    Need(data.at("format")=="hksr.character-package","character package format");
    Need(data.at("schema")==1,"unsupported package schema");
    CharacterPackage out;
    out.id=Text(data,"id");out.display_name=Text(data,"display_name");
    const auto& source=data.at("source");
    const auto kind=Text(source,"kind");
    Need(kind=="official" || kind=="external","unsupported source kind");
    out.source=kind=="official"?SourceKind::Official:SourceKind::External;
    const auto& id=source.at("npc_id");Need(id.is_number_unsigned() || id.is_number_integer(),"integer NPC id required");
    const auto number=id.get<std::int64_t>();
    Need(number>0 && number<=std::numeric_limits<std::uint32_t>::max(),"NPC id outside supported range");
    out.npc_id=static_cast<std::uint32_t>(number);
    // The descriptor selects a source. It is not a hand-written action catalog.
    Need(!data.contains("animation_states") && !data.contains("battle_actions"),"action lists belong to source discovery, not package definitions");
    if(!source.contains("appearance")){
        Need(out.source==SourceKind::Official,"external source needs appearance");return out;
    }
    const auto& input=source.at("appearance");Appearance appearance;
    appearance.template_prefab=Text(input,"template_prefab");
    appearance.character_config=Text(input,"character_config");
    appearance.weapon_config=input.value("weapon_config",std::string());
    if(out.source==SourceKind::External){
        appearance.asset_directory=Text(input,"asset_directory");detail::RelativePath(appearance.asset_directory);
        const auto shape=input.value("blendshape",std::string("none"));
        Need(shape=="none" || shape=="matching_template","unsupported external BlendShape source");
        appearance.shapes=shape=="none"?ShapeData::None:ShapeData::MatchingTemplate;
    }else{
        Need(!input.contains("asset_directory"),"official source cannot silently use external assets");
        appearance.shapes=ShapeData::MatchingTemplate;
    }
    out.appearance=std::move(appearance);return out;
}
}
