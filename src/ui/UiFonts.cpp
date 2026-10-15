#include "UiFonts.hpp"
#include "Localization.hpp"
#include <imgui.h>
#include <Windows.h>
#include <filesystem>

namespace hksr::framework::ui {
void AddUiFonts() {
    auto& io=ImGui::GetIO();
    static const auto ranges=[] {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(ImGui::GetIO().Fonts->GetGlyphRangesChineseFull());
        builder.AddText(UiGlyphText());
        ImVector<ImWchar> out;builder.BuildRanges(&out);return out;
    }(); // Atlas retains the range pointer until Build.
    wchar_t windows[MAX_PATH]{};GetWindowsDirectoryW(windows,MAX_PATH);
    for(const auto* name:UiFontFiles()) {
        const auto file=std::filesystem::path(windows)/L"Fonts"/name;
        if(!std::filesystem::is_regular_file(file))continue;
        ImFontConfig config;config.MergeMode=!io.Fonts->Fonts.empty();
        io.Fonts->AddFontFromFileTTF(file.u8string().c_str(),18,&config,ranges.Data);
    }
    if(io.Fonts->Fonts.empty())io.Fonts->AddFontDefault();
}
}
