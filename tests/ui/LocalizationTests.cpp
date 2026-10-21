#include "../../src/ui/Localization.hpp"
#include "../../src/ui/UiPreferences.hpp"
#include "../../src/ui/Menu.hpp"
#include "../../src/ui/ControlMessages.hpp"
#include "../../src/ui/CharacterStatus.hpp"
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
    Check(WriteCameraPresentations(file,true) && ReadCameraPresentations(file));
    Check(WriteCameraPresentations(file,false) && !ReadCameraPresentations(file));
    Check(!ReadCameraPresentations(folder/L"missing-presentation-preferences.cfg"));
    wchar_t value[16]{};GetPrivateProfileStringW(L"unrelated",L"preserve",L"",value,16,file.c_str());Check(std::wstring(value)==L"yes");
    Check(!WriteLanguagePreference(folder,Language::English)); // an existing directory is not a writable settings file
    {
        using namespace hksr::framework;
        ApplicationSnapshot snapshot;snapshot.scene=SceneAddress{"map",1};
        SceneCharacterStatus actor;actor.scene="map";actor.active=actor.creation_ready=true;
        actor.handoff=HandoffStatus{};actor.handoff->ready_domain=ActionDomain::Alternate;
        actor.playback=ActionPlaybackStatus{};
        ActionReceipt receipt;receipt.serial=4;receipt.request=FreeStyleState{"Skill01"};receipt.outcome=ActionOutcome::Returned;
        actor.playback->last=receipt;
        Check(CharacterStatus(snapshot,actor,Language::English).text.empty());
        actor.branch_state.phase="ultra";
        actor.branch_state.values={{"","item",std::string("missile")},{"shooting","remaining",3.0f}};
        Check(CharacterStatus(snapshot,actor,Language::English).text=="Phase: ultra\nitem = missile\nshooting.remaining = 3");
        actor.playback->successor=AutomaticSuccessor{4,0,"StandBy_Transfer",false,true};
        Check(CharacterStatus(snapshot,actor,Language::Chinese).text=="阶段: ultra\nitem = missile\nshooting.remaining = 3");
        actor.playback->pending=true;
        Check(CharacterStatus(snapshot,actor,Language::English).text.find("Skill01")==std::string::npos);
        actor.playback->pending=false;actor.handoff->phase=HandoffPhase::Loading;
        Check(CharacterStatus(snapshot,actor,Language::English).text.find("remaining = 3")!=std::string::npos);
        actor.handoff->phase=HandoffPhase::Ready;actor.creation_ready=false;
        actor.branch_state={};Check(CharacterStatus(snapshot,actor,Language::Chinese).text.empty());
        actor.creation_ready=true;actor.playback->last->outcome=ActionOutcome::CallFailed;actor.playback->last->detail="failed 100%";
        auto failed=CharacterStatus(snapshot,actor,Language::English);Check(failed.text.empty() && failed.error=="failed 100%");
        actor.playback.reset();actor.scene="other";
        Check(CharacterStatus(snapshot,actor,Language::English).text.empty());
    }
    std::cout<<"Localization: "<<ids.size()<<" keys; registered languages, typed parameters, stable widget IDs, saved tags and typed status passed.\n";
}
