#pragma once
#include <imgui.h>
#include <string_view>

namespace hksr::framework::ui {
ImVector<ImWchar> BuildUiGlyphRanges(std::string_view official_names = {});
// Called with the owning ImGui context, before its atlas is built.
// A supplied range array must remain alive until the atlas is cleared.
void AddUiFonts(const ImWchar* ranges = nullptr);
}
