#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "LocaleIds.hpp"

namespace hksr::framework::ui {
struct LanguageInfo {Language value;const char* code;const char* name;};
const std::vector<LanguageInfo>& AvailableLanguages();
const LanguageInfo& DescribeLanguage(Language language);
const char* Translate(UiText key,Language language);
// Stable ###key IDs preserve windows, input focus and popup state on language changes.
const char* LocalizedLabel(UiText key,Language language);
const char* LanguageCode(Language language);
std::optional<Language> ParseLanguage(std::string_view value);
const char* LocalizedSourceName(std::string_view key,Language language);
const std::vector<const char*>& UiFontFiles();
const char* UiGlyphText();
}
