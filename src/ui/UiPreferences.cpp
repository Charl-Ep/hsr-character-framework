#include "UiPreferences.hpp"
#include <Windows.h>

namespace hksr::framework::ui {
namespace {
std::optional<Language> ReadTag(const wchar_t* value) {
    std::string tag;for(;*value;++value){if(*value>127)return {};tag+=static_cast<char>(*value);}
    return ParseLanguage(tag);
}
}
Language DefaultLanguage() {
    wchar_t value[LOCALE_NAME_MAX_LENGTH]{};
    if(LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(),SORT_DEFAULT),value,LOCALE_NAME_MAX_LENGTH,0))
        return ReadTag(value).value_or(DefaultLocale);
    return DefaultLocale;
}
Language ReadLanguagePreference(const std::filesystem::path& file,Language fallback) {
    wchar_t value[LOCALE_NAME_MAX_LENGTH]{};
    GetPrivateProfileStringW(L"ui",L"language",L"",value,LOCALE_NAME_MAX_LENGTH,file.c_str());
    return ReadTag(value).value_or(fallback);
}
bool WriteLanguagePreference(const std::filesystem::path& file,Language language) {
    const std::string code=LanguageCode(language);const std::wstring wide(code.begin(),code.end());
    return WritePrivateProfileStringW(L"ui",L"language",wide.c_str(),file.c_str())!=FALSE;
}
}
