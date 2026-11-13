#pragma once
#include <filesystem>

namespace hksr::framework::ui {
// Worker-only preference IO. Interface language follows the game, not this file.
bool ReadCameraPresentations(const std::filesystem::path& file);
bool WriteCameraPresentations(const std::filesystem::path& file, bool enabled);
}
