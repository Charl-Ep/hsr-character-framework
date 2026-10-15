#pragma once
#include <hksr/animation/OfficialCatalog.hpp>
#include <algorithm>
#include <sstream>
#include <tuple>

namespace hksr::framework {
// Presentation only: retain every row/variant, never decide source validity.
inline std::string SourceFile(const std::string& path) {
    const auto at=path.find_last_of("/\\");return path.substr(at==std::string::npos?0:at+1);
}
inline std::string SourceLabel(const api::NpcCatalogRow& row) {
    return std::to_string(row.id)+" | "+SourceFile(row.character_config);
}
inline std::string SourceLabel(const api::PlayerCatalogRow& row) {
    return std::to_string(row.id)+" ["+std::to_string(row.enhanced)+" "+std::to_string(row.skin)+"] | "+SourceFile(row.character_config);
}
inline std::string SourceFold(std::string text) {
    for(auto& c:text) if(c>='A' && c<='Z') c=static_cast<char>(c-'A'+'a');
    return text;
}
inline bool SourceMatches(const std::string& text,const std::string& filter) {
    const auto haystack=SourceFold(text);std::istringstream words(SourceFold(filter));std::string word;
    while(words>>word) if(haystack.find(word)==std::string::npos) return false;
    return true;
}
inline std::vector<std::size_t> SourceIndices(const std::vector<api::NpcCatalogRow>& rows,const std::string& filter) {
    std::vector<std::size_t> result;
    for(std::size_t i=0;i<rows.size();++i) if(SourceMatches(SourceLabel(rows[i])+" "+rows[i].character_config+" "+rows[i].model_config,filter)) result.push_back(i);
    std::stable_sort(result.begin(),result.end(),[&](auto a,auto b){return rows[a].id<rows[b].id;});return result;
}
inline std::vector<std::size_t> SourceIndices(const std::vector<api::PlayerCatalogRow>& rows,const std::string& filter) {
    std::vector<std::size_t> result;
    for(std::size_t i=0;i<rows.size();++i) if(SourceMatches(SourceLabel(rows[i])+" "+rows[i].character_config+" "+rows[i].prefab,filter)) result.push_back(i);
    std::stable_sort(result.begin(),result.end(),[&](auto a,auto b){
        return std::tie(rows[a].id,rows[a].enhanced,rows[a].skin)<std::tie(rows[b].id,rows[b].enhanced,rows[b].skin);
    });return result;
}
}
