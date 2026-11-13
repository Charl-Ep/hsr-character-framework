#include "UiFonts.hpp"
#include "Localization.hpp"
#include <imgui.h>
#include <Windows.h>
#include <filesystem>
#include <algorithm>

namespace hksr::framework::ui {
ImVector<ImWchar> BuildUiGlyphRanges(std::string_view official_names) {
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(ImGui::GetIO().Fonts->GetGlyphRangesChineseFull());
    static const ImWchar symbols[] = {0x2100, 0x26ff, 0};
    builder.AddRanges(symbols);
    builder.AddText(UiGlyphText());
    if (!official_names.empty())
        builder.AddText(official_names.data(), official_names.data() + official_names.size());
    ImVector<ImWchar> out;
    builder.BuildRanges(&out);
    return out;
}
void AddUiFonts(const ImWchar* ranges) {
    auto& io = ImGui::GetIO();
    static const auto default_ranges = BuildUiGlyphRanges();
    if (!ranges)
        ranges = default_ranges.Data;
    wchar_t windows[MAX_PATH]{};
    GetWindowsDirectoryW(windows, MAX_PATH);
    auto files = UiFontFiles();
    // Official names may use a different script from the available UI locale.
    // Keep configured fonts first, then supplement missing Korean/Thai glyphs.
    for (const auto* fallback : {"malgun.ttf", "leelawui.ttf"})
        if (std::none_of(files.begin(), files.end(), [&](const auto* file) {
                return std::string_view(file) == fallback;
            }))
            files.push_back(fallback);
    for (const auto* name : files) {
        const auto file = std::filesystem::path(windows) / L"Fonts" / name;
        if (!std::filesystem::is_regular_file(file))
            continue;
        ImFontConfig config;
        config.MergeMode = !io.Fonts->Fonts.empty();
        io.Fonts->AddFontFromFileTTF(file.u8string().c_str(), 18, &config, ranges);
    }
    if (io.Fonts->Fonts.empty())
        io.Fonts->AddFontDefault();
}
}
