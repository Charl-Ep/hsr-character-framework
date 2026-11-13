#include "UiPreferences.hpp"
#include <Windows.h>

namespace hksr::framework::ui {
bool ReadCameraPresentations(const std::filesystem::path& file) {
    return GetPrivateProfileIntW(L"presentation", L"camera_presentations", 0, file.c_str()) != 0;
}
bool WriteCameraPresentations(const std::filesystem::path& file, bool enabled) {
    return WritePrivateProfileStringW(L"presentation", L"camera_presentations", enabled ? L"1" : L"0", file.c_str()) !=
           FALSE;
}
}
