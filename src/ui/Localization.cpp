#include "Localization.hpp"
#include <array>

namespace hksr::framework::ui {
namespace {
constexpr auto count=static_cast<std::size_t>(UiText::Count);
struct TextRow {const char* key;std::array<const char*,LocaleCount> text;};
struct Alias {const char* tag;Language language;};
#include "LocaleData.inc"
static_assert(sizeof(entries)/sizeof(entries[0])==count);
std::size_t Index(Language language) {
    const auto index=static_cast<std::size_t>(language);
    return index<LocaleCount?index:static_cast<std::size_t>(DefaultLocale);
}
}
const std::vector<LanguageInfo>& AvailableLanguages(){return languages;}
const LanguageInfo& DescribeLanguage(Language language){return languages[Index(language)];}
const char* Translate(UiText key,Language language) {
    const auto index=static_cast<std::size_t>(key);
    return index<count?entries[index].text[Index(language)]:"[missing UI text]";
}
const char* LocalizedLabel(UiText key,Language language) {
    static const auto labels=[] {
        std::array<std::array<std::string,LocaleCount>,count> out;
        for(std::size_t i=0;i<count;++i)for(std::size_t j=0;j<LocaleCount;++j)
            out[i][j]=std::string(entries[i].text[j])+"###"+entries[i].key;
        return out;
    }();
    const auto index=static_cast<std::size_t>(key);
    return index<count?labels[index][Index(language)].c_str():"[missing UI text]";
}
const char* LanguageCode(Language language){return DescribeLanguage(language).code;}
std::optional<Language> ParseLanguage(std::string_view value) {
    std::string tag(value);
    for(auto& c:tag){if(c>='A' && c<='Z')c=static_cast<char>(c-'A'+'a');else if(c=='_')c='-';}
    for(;;) {
        for(const auto& alias:aliases)if(tag==alias.tag)return alias.language;
        const auto end=tag.rfind('-');if(end==std::string::npos)return {};
        tag.resize(end);
    }
}
const char* LocalizedSourceName(std::string_view key,Language language) {
    for(const auto& row:source_names)if(key==row.key)return row.text[Index(language)];
    return "";
}
const std::vector<const char*>& UiFontFiles(){return font_files;}
const char* UiGlyphText(){
    static const auto text=[] {std::string out;for(const auto* chunk:glyph_chunks)out+=chunk;return out;}();
    return text.c_str();
}
#include "LocaleFormats.inc"
}
