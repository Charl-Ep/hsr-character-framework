#include "../../src/ui/Localization.hpp"
#include "../../src/ui/UiPreferences.hpp"
#include "../../src/ui/Menu.hpp"
#include "../../src/ui/ControlMessages.hpp"
#include "../../src/ui/CharacterStatus.hpp"
#include "../../src/ui/MenuLabels.hpp"
#include "../../src/ui/UiFonts.hpp"
#include <imgui_internal.h>
#include <Windows.h>
#include <set>
#include <stdexcept>
#include <iostream>

namespace {
void Check(bool value) {
    if (!value)
        throw std::runtime_error("localization test failed");
}
}
void TestLocalization() {
    namespace api = hksr::api;
    using namespace hksr::framework::ui;
    Check(OfficialNameText("银狼LV.<ub>999</ub>") == "银狼LV.999");
    Check(OfficialNameText("<ub> 12  34 </ub>") == " 12  34 ");
    Check(OfficialNameText("<ub>A<ub>99</ub>B</ub>") == "A99B");
    Check(OfficialNameText("<ub>A</ub> · <ub>2</ub>") == "A · 2");
    Check(OfficialNameText("1 < 2 > 0") == "1 < 2 > 0");
    Check(OfficialNameText("<unknown>999</unknown>") == "<unknown>999</unknown>");
    Check(OfficialNameText("<ub>999") == "<ub>999");
    Check(OfficialNameText("999</ub>") == "999</ub>");
    Check(OfficialNameText("<ub") == "<ub");
    Check(OfficialNameText("<ub>999</u") == "<ub>999</u");
    Check(OfficialNameText("").empty());
    {
        using namespace hksr::framework;
        api::CharacterCatalog catalog;
        api::PlayerCatalogRow boy{8001, 8001, 0, 0, "Player/Boy", "Boy.prefab"};
        boy.role = 8001;
        boy.name = "Official name";
        boy.path_name = "Preservation";
        boy.gender = api::CharacterGender::Male;
        auto girl = boy;
        girl.id = girl.avatar = 8002;
        girl.gender = api::CharacterGender::Female;
        auto enhanced = boy;
        enhanced.enhanced = 1;
        catalog.players.rows = {boy, girl, enhanced};
        Check(PlayerInstanceName(boy, catalog, Language::English) == "Official name · Preservation · Male");
        Check(PlayerInstanceName(girl, catalog, Language::English) == "Official name · Preservation · Female");
        Check(PlayerInstanceName(enhanced, catalog, Language::English) ==
              "Official name · Preservation · Male · Enhanced");
        api::CharacterSkinRow skin{999, 8001, "Official skin"};
        Check(SkinName(skin, catalog, Language::English) == "Official name · Preservation · Male · Official skin");
        CharacterPackage package;
        package.id = "official-player:8001:1:0";
        package.avatar_id = 8001;
        package.appearance = Appearance{boy.prefab, {}, boy.character_config, boy.character_config, ShapeData::None};
        Check(ActorName(package, &catalog, Language::English) ==
              PlayerInstanceName(enhanced, catalog, Language::English));
        api::NpcCatalogRow npc{5, "NPC/technical.json", "Model", "Official NPC"};
        Check(FriendlyName(npc, Language::Chinese) == "Official NPC");
        catalog.players.rows.front().name = "银狼LV.<ub>999</ub>";
        catalog.players.rows.front().path_name = "<ub>Preservation</ub>";
        auto& named = catalog.players.rows.front();
        Check(FriendlyName(named, Language::Chinese) == "银狼LV.999");
        Check(PlayerGenderDistinct(named, catalog));
        Check(PlayerFormName(named, Language::English, true) == "银狼LV.999 · Preservation · Male");
        Check(SearchNames(named).find("<ub>") == std::string::npos);
        Check(hksr::framework::SourceMatches(SearchNames(named), "银狼LV.999"));
        skin.name = "<ub>Official skin 2</ub>";
        Check(SkinName(skin, catalog, Language::English) == "银狼LV.999 · Preservation · Male · Official skin 2");
        npc.name = "<ub>Official NPC 3</ub>";
        Check(FriendlyName(npc, Language::English) == "Official NPC 3");
        Check(named.name == "银狼LV.<ub>999</ub>" && named.path_name == "<ub>Preservation</ub>");
        Check(skin.name == "<ub>Official skin 2</ub>" &&
              npc.name == "<ub>Official NPC 3</ub>"); // SDK metadata is untouched
    }
    std::set<ImGuiID> ids;
    for (std::size_t i = 0; i < static_cast<std::size_t>(UiText::Count); ++i) {
        const auto key = static_cast<UiText>(i);
        const auto a = ImHashStr(LocalizedLabel(key, DefaultLocale));
        Check(ids.insert(a).second);
        for (const auto& language : AvailableLanguages()) {
            Check(*Translate(key, language.value));
            Check(a == ImHashStr(LocalizedLabel(key, language.value)));
            Check(ParseLanguage(language.code) == language.value);
        }
    }
    Check(ParseLanguage("zh-CN") == Language::Chinese && ParseLanguage("en") == Language::English &&
          !ParseLanguage("invalid"));
    Check(ParseLanguage("ZH_cn") == Language::Chinese && ParseLanguage("en-GB") == Language::English);
    Check(GameLanguage("en") == Language::English && GameLanguage("zh-CN") == Language::Chinese);
    Check(GameLanguage("cn") == Language::Chinese && GameLanguage("CN") == Language::Chinese);
    Check(GameLanguage("ZH_cn") == Language::Chinese && GameLanguage("zh-Hans") == Language::Chinese);
    for (const auto* tag : {"", "unknown", "ja", "jp", "ko", "kr", "ru", "cht", "zh-tw", "zh-Hant"})
        Check(GameLanguage(tag) == Language::English);
    Check(std::string(Translate(UiText::Title, Language::Count)) == Translate(UiText::Title, DefaultLocale));
    Check(FormatItemCount(Language::English, 12) == "12 entries");
    Check(FormatItemCount(Language::Chinese, 12) == "12 项");
    Check(FormatInstanceCaption(Language::English, "100% {name}", 7) == "100% {name} · Instance 7");
    Check(FormatCreationCount(Language::English, 2, 3) == "Creations 2 · Retiring 3");
    Check(FormatCreationCount(Language::Count, 2, 3) == FormatCreationCount(DefaultLocale, 2, 3));
    hksr::framework::ControlStatus status;
    status.gaze_phase = hksr::api::GazePhase::OutOfRange;
    Check(GazeStatusText(status, Language::English) == Translate(UiText::GazeOutOfRange, Language::English));
    status.gaze_phase = hksr::api::GazePhase::Failed;
    status.gaze_error = "Some_Official_State 100%";
    Check(GazeStatusText(status, Language::Chinese) == status.gaze_error);
    Menu menu;
    Check(menu.GetLanguage() == Language::English);
    menu.FollowGameLanguage("zh-CN");
    Check(menu.GetLanguage() == Language::Chinese);
    menu.FollowGameLanguage("en");
    Check(menu.GetLanguage() == Language::English);
    menu.FollowGameLanguage("ko");
    Check(menu.GetLanguage() == Language::English);
    const auto folder = std::filesystem::absolute(".build/ui-previews");
    std::filesystem::create_directories(folder);
    const auto file = folder / L"language-test-语言.cfg";
    Check(WritePrivateProfileStringW(L"unrelated", L"preserve", L"yes", file.c_str()) != FALSE);
    Check(WritePrivateProfileStringW(L"ui", L"language", L"zh-CN", file.c_str()) != FALSE);
    Check(WriteCameraPresentations(file, true) && ReadCameraPresentations(file));
    Check(WriteCameraPresentations(file, false) && !ReadCameraPresentations(file));
    Check(!ReadCameraPresentations(folder / L"missing-presentation-preferences.cfg"));
    wchar_t value[16]{};
    GetPrivateProfileStringW(L"unrelated", L"preserve", L"", value, 16, file.c_str());
    Check(std::wstring(value) == L"yes");
    Check(!WriteCameraPresentations(folder, true)); // an existing directory is not a writable settings file
    {
        auto* prior = ImGui::GetCurrentContext();
        auto* context = ImGui::CreateContext();
        auto ranges = BuildUiGlyphRanges("눈 เบ ไทย Дạ");
        auto contains = [&](ImWchar character) {
            for (int i = 0; ranges[i]; i += 2)
                if (character >= ranges[i] && character <= ranges[i + 1])
                    return true;
            return false;
        };
        Check(contains(0xb208) && contains(0x0e44) && contains(0x0414) && contains(0x1ea1));
        Check(!contains(0xac00)); // Do not request the entire Hangul block.
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1000, 800};
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        hksr::framework::ApplicationService application;
        for (const auto* tag : {"cn", "zh-CN", "en", "kr", ""}) {
            auto catalog = std::make_shared<hksr::framework::CatalogSnapshot>();
            catalog->text_language = tag;
            hksr::framework::ApplicationSnapshot snapshot;
            snapshot.catalog = std::move(catalog);
            application.Publish(std::move(snapshot));
            ImGui::NewFrame();
            menu.Draw(application);
            Check(menu.GetLanguage() == GameLanguage(tag));
            ImGui::Render();
        }
        ImGui::DestroyContext(context);
        ImGui::SetCurrentContext(prior);
    }
    {
        using namespace hksr::framework;
        ApplicationSnapshot snapshot;
        snapshot.scene = SceneAddress{"map", 1};
        SceneCharacterStatus actor;
        actor.id = 1;
        actor.scene = "map";
        actor.active = actor.creation_ready = true;
        actor.handoff = HandoffStatus{};
        actor.handoff->ready_domain = ActionDomain::Battle;
        actor.playback = ActionPlaybackStatus{};
        ActionReceipt receipt;
        receipt.serial = 4;
        receipt.request = FreeStyleState{"Skill01"};
        receipt.outcome = ActionOutcome::Returned;
        actor.playback->last = receipt;
        auto tasks = std::make_shared<CharacterTaskStatus>();
        actor.tasks = tasks;
        TaskContextView bookkeeping;
        bookkeeping.name = "actor";
        bookkeeping.token = {1};
        bookkeeping.values = {{"selected", std::string("Skill01")}, {"performance", std::string("playing")}};
        tasks->control.contexts.push_back(bookkeeping);
        Check(CharacterStatus(snapshot, actor, Language::English).text.empty());
        actor.branch_state.phase = "ignored actor data";
        actor.branch_state.values = {{"actor", "selected", std::string("Skill01")}};
        Check(CharacterStatus(snapshot, actor, Language::English).text.empty());
        TaskContextView phase;
        phase.name = "phase-owner";
        phase.token = {2};
        phase.phase = "ultra";
        phase.values = {{"item", std::string("missile")}};
        TaskContextView shooting;
        shooting.name = "shooting";
        shooting.token = {3};
        shooting.values = {{"remaining", std::int64_t(3)}};
        TaskContextView reaction;
        reaction.name = "visual-reaction";
        reaction.token = {4};
        reaction.visual_reaction = TaskVisualReaction{"Body", 115};
        tasks->control.contexts.insert(tasks->control.contexts.end(), {phase, shooting, reaction});
        auto catalog = std::make_shared<CatalogSnapshot>();
        auto actions = std::make_shared<CharacterActionCatalog>();
        VisualState state;
        state.character_config = "Body";
        state.value.flag = {true, 115, {}};
        state.value.occur.resolved_state = {true, "Stagger", {}};
        actions->actions.visual_states.push_back(state);
        catalog->characters.emplace(1, actions);
        snapshot.catalog = catalog;
        Check(CharacterStatus(snapshot, actor, Language::English).text ==
              "Phase: ultra\nphase-owner.item = missile\nshooting.remaining = 3\nState: Stagger");
        TaskContextView receiver;
        receiver.name = "receiver";
        receiver.resilience = 4;
        receiver.hit_effects.resize(2);
        tasks->control.contexts.push_back(receiver);
        Check(CharacterStatus(snapshot, actor, Language::English).text.find("Resilience: 4\nHit effect overrides: 2") !=
              std::string::npos);
        Check(CharacterStatus(snapshot, actor, Language::Chinese).text.find("韧性: 4\n受击效果覆盖: 2") !=
              std::string::npos);
        tasks->control.contexts.pop_back();
        actor.playback->successor = AutomaticSuccessor{4, 0, "StandBy_Transfer", false, true};
        Check(CharacterStatus(snapshot, actor, Language::Chinese).text ==
              "阶段: ultra\nphase-owner.item = missile\nshooting.remaining = 3\n状态: Stagger");
        actor.playback->pending = true;
        Check(CharacterStatus(snapshot, actor, Language::English).text.find("Skill01") == std::string::npos);
        actor.playback->pending = false;
        actor.handoff->phase = HandoffPhase::Loading;
        Check(CharacterStatus(snapshot, actor, Language::English).text.find("remaining = 3") != std::string::npos);
        actor.handoff->phase = HandoffPhase::Ready;
        actor.creation_ready = false;
        tasks->control.contexts = {bookkeeping};
        Check(CharacterStatus(snapshot, actor, Language::Chinese).text.empty());
        actor.creation_ready = true;
        actor.playback->last->outcome = ActionOutcome::CallFailed;
        actor.playback->last->detail = "failed 100%";
        auto failed = CharacterStatus(snapshot, actor, Language::English);
        Check(failed.text.empty() && failed.error == "failed 100%");
        actor.action = ActionIntentStatus{5, 1, ActionIntentPhase::Waiting, {}, "waiting for new action"};
        actor.handoff->error = "old model failure";
        Check(CharacterStatus(snapshot, actor, Language::English).error.empty());
        actor.action->phase = ActionIntentPhase::Returned;
        actor.action->outcome = ActionOutcome::Returned;
        Check(CharacterStatus(snapshot, actor, Language::English).error.empty());
        actor.action->phase = ActionIntentPhase::Failed;
        actor.action->outcome = ActionOutcome::StateAbsent;
        Check(CharacterStatus(snapshot, actor, Language::English).error ==
              Translate(UiText::StateMissing, Language::English));
        actor.action->outcome = ActionOutcome::CallFailed;
        actor.action->detail = "new call failed";
        Check(CharacterStatus(snapshot, actor, Language::English).error == "new call failed");
        actor.action.reset();
        actor.playback.reset();
        actor.scene = "other";
        Check(CharacterStatus(snapshot, actor, Language::English).text.empty());
        actor.tasks.reset();
        actor.scene = "map";
        actor.branch_state.phase = "legacy-phase";
        actor.branch_state.values = {{"legacy", "counter", 2.0f}};
        Check(CharacterStatus(snapshot, actor, Language::English).text == "Phase: legacy-phase\nlegacy.counter = 2");
    }
    std::cout
        << "Localization: " << ids.size()
        << " keys; game-language selection, fallback, glyphs, stable widget IDs and typed status passed.\n";
}
