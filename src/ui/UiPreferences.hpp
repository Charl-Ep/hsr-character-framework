#pragma once
#include "Localization.hpp"
#include <filesystem>

namespace hksr::framework::ui {
Language DefaultLanguage();
// Worker-only file IO. Missing/unknown language uses the supplied fallback.
Language ReadLanguagePreference(const std::filesystem::path& file,Language fallback);
bool WriteLanguagePreference(const std::filesystem::path& file,Language language);
bool ReadCameraPresentations(const std::filesystem::path& file);
bool WriteCameraPresentations(const std::filesystem::path& file,bool enabled);
}
