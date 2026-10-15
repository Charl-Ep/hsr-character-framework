#include "../../src/ui/Localization.hpp"
#include "../../src/ui/UiPreferences.hpp"
#include "../../src/ui/Menu.hpp"
#include "../../src/ui/ControlMessages.hpp"
#include <imgui_internal.h>
#include <Windows.h>
#include <set>
#include <stdexcept>
#include <iostream>

namespace {
void Check(bool value){if(!value)throw std::runtime_error("localization test failed");}
}
void TestLocalization() {
    using namespace hksr::framework::ui;
    std::set<ImGuiID> ids;
    for(std::size_t i=0;i<static_cast<std::size_t>(UiText::Count);++i) {
        const auto key=static_cast<UiText>(i);
        const auto a=ImHashStr(LocalizedLabel(key,DefaultLocale));Check(ids.insert(a).second);
        for(const auto& language:AvailableLanguages()) {
            Check(*Translate(key,language.value));
            Check(a==ImHashStr(LocalizedLabel(key,language.value)));
            Check(ParseLanguage(language.code)==language.value);
        }
    }
    Check(ParseLanguage("zh-CN")==Language::Chinese && ParseLanguage("en")==Language::English && !ParseLanguage("invalid"));
    Check(ParseLanguage("ZH_cn")==Language::Chinese && ParseLanguage("en-GB")==Language::English);
    Check(std::string(Translate(UiText::Title,Language::Count))==Translate(UiText::Title,DefaultLocale));
    Check(FormatItemCount(Language::English,12)=="12 entries");
    Check(FormatItemCount(Language::Chinese,12)=="12 项");
    Check(FormatInstanceCaption(Language::English,"100% {name}",7)=="100% {name} · Instance 7");
    Check(FormatCreationCount(Language::English,2,3)=="Creations 2 · Retiring 3");
    Check(FormatCreationCount(Language::Count,2,3)==FormatCreationCount(DefaultLocale,2,3));
    hksr::framework::ControlStatus status;status.gaze_phase=hksr::api::GazePhase::OutOfRange;
    Check(GazeStatusText(status,Language::English)==Translate(UiText::GazeOutOfRange,Language::English));
    status.gaze_phase=hksr::api::GazePhase::Failed;status.gaze_error="Some_Official_State 100%";
    Check(GazeStatusText(status,Language::Chinese)==status.gaze_error);
    Menu menu;menu.SetLanguage(Language::English);
    Check(menu.GetLanguage()==Language::English && menu.TakeLanguageChange()==Language::English && !menu.TakeLanguageChange());
    menu.SetLanguage(Language::Chinese,false);Check(!menu.TakeLanguageChange());
    const auto folder=std::filesystem::absolute(".build/ui-previews");std::filesystem::create_directories(folder);
    const auto file=folder/L"language-test-语言.cfg";
    Check(WritePrivateProfileStringW(L"unrelated",L"preserve",L"yes",file.c_str())!=FALSE);
    Check(WriteLanguagePreference(file,Language::English) && ReadLanguagePreference(file,Language::Chinese)==Language::English);
    Check(WriteLanguagePreference(file,Language::Chinese) && ReadLanguagePreference(file,Language::English)==Language::Chinese);
    Check(WritePrivateProfileStringW(L"ui",L"language",L"unknown",file.c_str())!=FALSE);
    Check(ReadLanguagePreference(file,Language::English)==Language::English);
    wchar_t value[16]{};GetPrivateProfileStringW(L"unrelated",L"preserve",L"",value,16,file.c_str());Check(std::wstring(value)==L"yes");
    Check(!WriteLanguagePreference(folder,Language::English)); // an existing directory is not a writable settings file
    std::cout<<"Localization: "<<ids.size()<<" keys; registered languages, typed parameters, stable widget IDs, saved tags and typed status passed.\n";
}
