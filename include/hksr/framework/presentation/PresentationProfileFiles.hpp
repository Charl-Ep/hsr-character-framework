#pragma once
#include "PresentationProfile.hpp"
#include <filesystem>

namespace hksr::framework {
// Startup worker only. Absent directory means no authored overrides. One bad
// file does not suppress other profiles or prevent ordinary character creation.
PresentationProfiles ReadPresentationProfiles(const std::filesystem::path& directory);
}
