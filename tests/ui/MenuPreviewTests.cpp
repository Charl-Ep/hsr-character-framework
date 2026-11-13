#include "../../src/ui/Menu.hpp"
#include <hksr/framework/catalog/SourceCatalog.hpp>
#include "../../src/ui/UiFonts.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_dx11.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <stdexcept>
#include <iostream>
using Microsoft::WRL::ComPtr;
namespace {
void RequireAt(bool ok, int line) {
    if (!ok)
        throw std::runtime_error("menu preview check failed at line " + std::to_string(line));
}
#define Require(...) RequireAt((__VA_ARGS__), __LINE__)
void Png(const std::filesystem::path& file, UINT width, UINT height, const D3D11_MAPPED_SUBRESOURCE& map) {
    ComPtr<IWICImagingFactory> factory;
    Require(
        SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))));
    ComPtr<IWICStream> stream;
    Require(SUCCEEDED(factory->CreateStream(&stream)));
    Require(SUCCEEDED(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE)));
    ComPtr<IWICBitmapEncoder> encoder;
    Require(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)));
    Require(SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)));
    ComPtr<IWICBitmapFrameEncode> frame;
    Require(SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)));
    Require(SUCCEEDED(frame->Initialize(nullptr)));
    Require(SUCCEEDED(frame->SetSize(width, height)));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    Require(SUCCEEDED(frame->SetPixelFormat(&format)));
    Require(IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA));
    std::vector<BYTE> bgra(static_cast<std::size_t>(width) * height * 4);
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x) {
            const auto* p = static_cast<const BYTE*>(map.pData) + y * map.RowPitch + x * 4;
            auto* q = bgra.data() + (static_cast<std::size_t>(y) * width + x) * 4;
            q[0] = p[2];
            q[1] = p[1];
            q[2] = p[0];
            q[3] = p[3];
        }
    Require(SUCCEEDED(frame->WritePixels(height, width * 4, width * height * 4, bgra.data())));
    Require(SUCCEEDED(frame->Commit()));
    Require(SUCCEEDED(encoder->Commit()));
}
hksr::framework::ApplicationSnapshot Fixture() {
    using namespace hksr::framework;
    namespace api = hksr::api;
    ApplicationSnapshot out;
    out.prepared = true;
    out.scene = SceneAddress{"preview-map", 1};
    auto c = std::make_shared<CatalogSnapshot>();
    auto rows = std::make_shared<api::CharacterCatalog>();
    rows->players.rows = {
        {1217, 1217, 0, 0, "Config/ConfigCharacter/LocalPlayer/LocalPlayer_Huohuo_00_Config.json", "Huohuo.prefab"},
        {1217, 1217, 1, 0, "Config/ConfigCharacter/LocalPlayer/LocalPlayer_Huohuo_00_Config.json", "Huohuo.prefab"},
        {1002, 1002, 0, 0, "Config/ConfigCharacter/LocalPlayer/LocalPlayer_DanHeng_00_Config.json", "DanHeng.prefab"}};
    rows->npcs.rows = {{1218, "Config/ConfigCharacter/NPC/Avatar/NPC_Avatar_Huohuo_01_Config.json",
                        "Config/ConfigEntity/NPC/Avatar/NPC_Avatar_Girl_Huohuo_01_Entity.json"}};
    rows->players.rows.push_back(
        {1001, 1001, 0, 0, "Config/ConfigCharacter/LocalPlayer/LocalPlayer_Mar_7th_00_Config.json", "Mar_7th.prefab"});
    for (auto& row : rows->players.rows) {
        row.role = row.avatar;
        row.name = row.avatar == 1217 ? "藿藿" : row.avatar == 1002 ? "丹恒" : "三月七";
        row.path_name = row.avatar == 1217 ? "丰饶" : row.avatar == 1002 ? "巡猎" : "存护";
        row.default_skin_name = "默认外观";
    }
    auto skins = std::make_shared<api::CharacterSkinCatalog>();
    skins->complete = true;
    skins->rows.push_back({1100101, 1001, "冬去煦至"});
    c->skins = skins;
    c->rows = rows;
    auto source = std::make_shared<SourceRequestStatus>();
    source->id = 1;
    source->input = BrowsePlayer{{1217, 0, 0}};
    source->phase = SourceRequestPhase::Ready;
    source->result.finished = true;
    source->result.character = SourceCatalog::PlayerPackage(rows->players.rows.front());
    c->sources[1] = source;
    auto skin_source = std::make_shared<SourceRequestStatus>();
    skin_source->id = 2;
    skin_source->input = BrowseSkin{1100101};
    skin_source->phase = SourceRequestPhase::Ready;
    skin_source->result.finished = true;
    auto skin_package = SourceCatalog::PlayerPackage(rows->players.rows.back());
    skin_package.id = "official-skin:1100101";
    skin_package.appearance->template_prefab = "Avatar_Mar_7th_01.prefab";
    skin_source->result.character = skin_package;
    c->sources[2] = skin_source;
    auto actions = std::make_shared<CharacterActionCatalog>();
    actions->revision = 1;
    actions->actions.finished = true;
    const char* states[] = {
        "StandBy",         "Idle_Show_01",  "Idle_Show_02",
        "Common_IdleShow", "LookatPhoneBS", "LookatPhoneLoop",
        "LookatPhoneAS",   "Walk",          "Run",
        "TurnLeft",        "TurnRight",     "Very_Long_Animator_Name_With_A_Deeply_Nested_Path_To_Check_Clipping"};
    for (const auto* name : states)
        actions->actions.proposals.push_back({ActionDomain::Exploration, name, {}});
    actions->actions.proposals.push_back({ActionDomain::Battle, "ConfiguredRest", {}});
    actions->actions.proposals.push_back({ActionDomain::Battle, "Skill01", {}});
    auto ready = std::make_shared<ReadyActions>();
    ready->source = "fixture/battle";
    ReadyPose pose;
    pose.state = "Skill01Ready";
    pose.label = "Skill01Ready";
    ready->poses.push_back(pose);
    actions->actions.ready_actions.push_back(ready);
    VisualState visual;
    visual.character_config = "fixture/battle";
    visual.source = "global";
    visual.value.flag = {true, 28, {}};
    visual.value.occur.resolved_state = {true, "Stagger", {}};
    visual.value.loop.resolved_state = {true, "Stagger_Loop", {}};
    visual.value.recover.resolved_state = {true, "Stagger_Recover", {}};
    visual.value.occur.transition = visual.value.loop.transition = visual.value.recover.transition = {true, 0.15f, {}};
    visual.expression = {true, "Emo_Stun", {}};
    visual.expression_transition = {true, 0.2f, {}};
    actions->actions.visual_states.push_back(visual);
    c->characters[1] = actions;
    out.catalog = c;
    SceneCharacterStatus actor;
    actor.id = 1;
    actor.creations = 1;
    actor.active = actor.creation_ready = true;
    actor.package = *source->result.character;
    actor.scene = "preview-map";
    actor.handoff = HandoffStatus{};
    actor.playback = ActionPlaybackStatus{};
    ActionReceipt displayed;
    displayed.serial = 1;
    displayed.request = FreeStyleState{"StandBy"};
    displayed.outcome = ActionOutcome::Returned;
    actor.playback->last = displayed;
    actor.placement = {{6.575f, -15.015f, 60.552f}, {0, 0, 0, 1}};
    actor.controls = ControlStatus{};
    actor.controls->live.ready = true;
    actor.controls->live.position = actor.placement.position;
    actor.controls->live.rotation = actor.placement.rotation;
    actor.battle_idle_state = "ConfiguredRest";
    out.characters.push_back(actor);
    out.layouts.available = true;
    out.layouts.sequence = 1;
    auto& layout = out.layouts.maps["preview-map"];
    layout.initialized = true;
    layout.phase = LayoutPhase::Ready;
    layout.revision = layout.saved_revision = 3;
    layout.names = {"合影", "舞台布局", "Long named snapshot for layout selection"};
    actor.id = 2;
    actor.package = SourceCatalog::NpcPackage(rows->npcs.rows.front());
    out.characters.push_back(actor);
    actor.id = 3;
    actor.package = skin_package;
    actor.skin_id = 1100101;
    out.characters.push_back(actor);
    return out;
}
struct MenuFrameResult {
    std::string text;
    std::vector<std::uint64_t> observed, browsed;
    bool settings_open = false;
};
class ValueMenuContext {
  public:
    ValueMenuContext() : previous_(ImGui::GetCurrentContext()), context_(ImGui::CreateContext()) {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1100, 850};
        io.DeltaTime = 1.0f / 60;
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ~ValueMenuContext() {
        ImGui::DestroyContext(context_);
        ImGui::SetCurrentContext(previous_);
    }
    MenuFrameResult Frame(hksr::framework::ui::Menu& menu, hksr::framework::ApplicationService& host,
                          const hksr::framework::ApplicationSnapshot& snapshot) {
        using namespace hksr::framework;
        ImGui::NewFrame();
        ImGui::LogToBuffer();
        menu.Render(host, snapshot);
        MenuFrameResult out;
        out.text = ImGui::GetCurrentContext()->LogBuffer.c_str();
        ImGui::LogFinish();
        const auto* settings = ImGui::FindWindowByID(
            ImHashStr(ui::LocalizedLabel(ui::UiText::VisualSettings, menu.GetLanguage())));
        out.settings_open = settings && settings->Active && !settings->Hidden;
        ImGui::Render();
        for (const auto& pending : host.TakeCommands(false)) {
            if (const auto* observe = std::get_if<ObserveCharacter>(&pending.command))
                out.observed.push_back(observe->id);
            if (const auto* browse = std::get_if<BrowseCharacterActions>(&pending.command))
                out.browsed.push_back(browse->id);
            Require(!std::holds_alternative<DeleteCharacter>(pending.command));
            Require(!std::holds_alternative<EnableCharacter>(pending.command));
        }
        return out;
    }
    void Collapse(bool collapsed) const {
        auto* window = ImGui::FindWindowByID(ImHashStr(hksr::framework::ui::LocalizedLabel(
            hksr::framework::ui::UiText::WindowTitle, hksr::framework::ui::Language::English)));
        Require(window != nullptr);
        window->Collapsed = collapsed;
    }

  private:
    ImGuiContext* previous_;
    ImGuiContext* context_;
};
void TestCurrentMapMenuLists() {
    using namespace hksr::framework;
    ValueMenuContext context;
    ui::Menu menu(ui::MenuPage::Actions, ui::Language::English);
    ApplicationService host;
    ApplicationSnapshot snapshot;
    snapshot.prepared = true;
    snapshot.scene = SceneAddress{"map-a", 1};
    auto actor = [](std::uint64_t id, std::string map, std::string label) {
        SceneCharacterStatus out;
        out.id = id;
        out.scene = std::move(map);
        out.package.id = std::move(label);
        out.package.display_name = out.package.id;
        out.package.source = SourceKind::External;
        out.active = out.enabled = out.creation_ready = true;
        return out;
    };
    snapshot.characters = {actor(20, "map-b", "FOREIGN_MAP_ACTOR"), actor(10, "map-a", "CURRENT_ACTIVE"),
                           actor(11, "map-a", "CURRENT_DISABLED"), actor(12, "map-a", "CURRENT_PENDING"),
                           actor(13, "map-a", "CURRENT_DELETING"), actor(14, "", "UNASSIGNED_ACTOR")};
    snapshot.characters[2].enabled = snapshot.characters[2].active = snapshot.characters[2].creation_ready = false;
    snapshot.characters[3].active = snapshot.characters[3].creation_ready = false;
    snapshot.characters[4].deleting = true;
    auto frame = context.Frame(menu, host, snapshot);
    Require(frame.observed == std::vector<std::uint64_t>{10} &&
            frame.browsed == std::vector<std::uint64_t>{10});
    frame = context.Frame(menu, host, snapshot);
    for (const auto* name : {"CURRENT_ACTIVE", "CURRENT_DISABLED", "CURRENT_PENDING"})
        Require(frame.text.find(name) != std::string::npos);
    for (const auto* name : {"FOREIGN_MAP_ACTOR", "CURRENT_DELETING", "UNASSIGNED_ACTOR"})
        Require(frame.text.find(name) == std::string::npos);
    Require(frame.observed.empty() && frame.browsed.empty());
    snapshot.characters[1].deleting = true;
    frame = context.Frame(menu, host, snapshot);
    Require(frame.observed == std::vector<std::uint64_t>{11}); // Disabled entries remain manageable.
    Require(frame.text.find("CURRENT_ACTIVE") == std::string::npos);
    context.Collapse(true);
    snapshot.scene = SceneAddress{"map-b", 2};
    frame = context.Frame(menu, host, snapshot);
    Require(frame.observed == std::vector<std::uint64_t>{20}); // Selection updates even with a collapsed window.
    context.Collapse(false);
    frame = context.Frame(menu, host, snapshot);
    Require(frame.text.find("FOREIGN_MAP_ACTOR") != std::string::npos);
    Require(frame.text.find("CURRENT_DISABLED") == std::string::npos);
    snapshot.scene = SceneAddress{"empty-map", 3};
    frame = context.Frame(menu, host, snapshot);
    Require(frame.observed == std::vector<std::uint64_t>{0} && frame.browsed.empty());
    Require(frame.text.find(ui::Translate(ui::UiText::NoInstances, ui::Language::English)) != std::string::npos);
    Require(frame.text.find(ui::Translate(ui::UiText::SelectInstanceHint, ui::Language::English)) != std::string::npos);
    Require(frame.text.find("FOREIGN_MAP_ACTOR") == std::string::npos);
    snapshot.scene.reset();
    frame = context.Frame(menu, host, snapshot);
    Require(frame.observed.empty() && frame.browsed.empty());
    Require(frame.text.find("CURRENT_DISABLED") == std::string::npos &&
            frame.text.find("FOREIGN_MAP_ACTOR") == std::string::npos);
    snapshot.scene = SceneAddress{"", 4};
    frame = context.Frame(menu, host, snapshot);
    Require(frame.text.find("UNASSIGNED_ACTOR") == std::string::npos && frame.browsed.empty());
    snapshot.scene = SceneAddress{"map-a", 5};
    frame = context.Frame(menu, host, snapshot);
    Require(frame.observed == std::vector<std::uint64_t>{11}); // Same logical map, new incarnation.
    Require(snapshot.characters.size() == 6 && snapshot.characters.front().scene == "map-b");
}
void TestCurrentMapSettingsWindows() {
    using namespace hksr::framework;
    ValueMenuContext context;
    ui::Menu menu(ui::MenuPage::Actions, ui::Language::English);
    ApplicationService host;
    auto snapshot = Fixture();
    auto other = snapshot.characters.front();
    other.id = 20;
    other.scene = "other-map";
    snapshot.characters.push_back(other);
    auto tasks = std::make_shared<CharacterTaskStatus>();
    tasks->domain = ActionDomain::Battle;
    TaskDefinition definition;
    definition.key = {"scope-test", "configure"};
    definition.entry = "run";
    definition.inputs = {{"power", TaskValueType::Integer, std::int64_t(2)}};
    tasks->definitions.emplace(definition.key.name, definition);
    tasks->control.catalog[definition.key.name] = {};
    snapshot.characters[0].tasks = tasks;
    snapshot.characters[0].task_catalog = definition.key.catalog;
    snapshot.characters[0].task_catalog_revision = 1;
    snapshot.characters[0].handoff->ready_domain = ActionDomain::Battle;
    context.Frame(menu, host, snapshot);
    context.Frame(menu, host, snapshot);
    const auto entries = CharacterTasks(snapshot, 1);
    const auto entry = std::find_if(entries.begin(), entries.end(), [](const auto& row) {
        return row.label == "configure";
    });
    Require(entry != entries.end());
    menu.OpenTaskInputs(*entry);
    context.Frame(menu, host, snapshot);
    Require(context.Frame(menu, host, snapshot).settings_open);
    snapshot.scene = SceneAddress{"other-map", 2};
    auto frame = context.Frame(menu, host, snapshot);
    Require(frame.observed == std::vector<std::uint64_t>{20} && !frame.settings_open);
    snapshot.scene = SceneAddress{"preview-map", 3};
    context.Frame(menu, host, snapshot);
    menu.OpenTaskInputs(*entry);
    context.Frame(menu, host, snapshot);
    Require(context.Frame(menu, host, snapshot).settings_open);
    context.Collapse(true);
    snapshot.scene.reset();
    frame = context.Frame(menu, host, snapshot);
    Require(frame.observed == std::vector<std::uint64_t>{0} && !frame.settings_open);
    context.Collapse(false);
    snapshot.scene = SceneAddress{"preview-map", 4};
    auto catalog = std::make_shared<CatalogSnapshot>(*snapshot.catalog);
    auto actions = std::make_shared<CharacterActionCatalog>(*catalog->characters.at(1));
    auto alternate = actions->actions.visual_states.front();
    alternate.value.flag.value = 133;
    alternate.value.force_hit_h = {true, true, {}};
    alternate.value.reactions.present = true;
    alternate.value.reactions.hit_h = {true, "Hit_Heavy", {}};
    actions->actions.visual_states.push_back(std::move(alternate));
    catalog->characters[1] = actions;
    snapshot.catalog = catalog;
    context.Frame(menu, host, snapshot);
    auto variants = CharacterTasks(snapshot, 1);
    const auto variant = std::find_if(variants.begin(), variants.end(), HasTaskVariants);
    Require(variant != variants.end());
    menu.OpenTaskInputs(*variant);
    context.Frame(menu, host, snapshot);
    Require(context.Frame(menu, host, snapshot).settings_open);
    context.Collapse(true);
    snapshot.scene = SceneAddress{"other-map", 5};
    frame = context.Frame(menu, host, snapshot);
    Require(frame.observed == std::vector<std::uint64_t>{20} && !frame.settings_open);
    context.Collapse(false);
    context.Frame(menu, host, snapshot);
    Require(!context.Frame(menu, host, snapshot).settings_open);
}
}
void TestMenuPreview(ID3D11Device* device, ID3D11DeviceContext* context) {
    using namespace hksr::framework;
    TestCurrentMapMenuLists();
    TestCurrentMapSettingsWindows();
    Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)));
    const auto folder = std::filesystem::path(".build/ui-previews");
    std::filesystem::create_directories(folder);
    struct Scenario {
        const char* name;
        ui::MenuPage page;
        bool create;
        UINT width, height;
        ui::Language language = ui::Language::Chinese;
        bool switch_language = false;
    };
    for (const auto& scenario : std::vector<Scenario>{
             {"actions", ui::MenuPage::Actions, false, 1200, 840},
             {"controls", ui::MenuPage::Controls, false, 1200, 840},
             {"creation", ui::MenuPage::Actions, true, 1200, 840},
             {"small-actions", ui::MenuPage::Actions, false, 800, 600},
             {"small-creation", ui::MenuPage::Actions, true, 800, 600},
             {"actions-en", ui::MenuPage::Actions, false, 1200, 840, ui::Language::English},
             {"controls-en", ui::MenuPage::Controls, false, 1200, 840, ui::Language::English},
             {"creation-en", ui::MenuPage::Actions, true, 1200, 840, ui::Language::English},
             {"skin-creation", ui::MenuPage::Actions, true, 1200, 840},
             {"skin-creation-en", ui::MenuPage::Actions, true, 1200, 840, ui::Language::English},
             {"small-actions-en", ui::MenuPage::Actions, false, 800, 600, ui::Language::English},
             {"small-controls-en", ui::MenuPage::Controls, false, 800, 600, ui::Language::English},
             {"small-creation-en", ui::MenuPage::Actions, true, 800, 600, ui::Language::English},
             {"switch-language", ui::MenuPage::Controls, true, 1200, 840, ui::Language::Chinese, true},
             {"layouts", ui::MenuPage::Actions, false, 800, 600},
             {"layouts-en", ui::MenuPage::Actions, false, 800, 600, ui::Language::English},
             {"battle", ui::MenuPage::Actions, false, 800, 600},
             {"battle-en", ui::MenuPage::Actions, false, 800, 600, ui::Language::English},
             {"battle-states", ui::MenuPage::Actions, false, 800, 600},
             {"battle-states-en", ui::MenuPage::Actions, false, 800, 600, ui::Language::English},
             {"tasks", ui::MenuPage::Actions, false, 800, 600},
             {"tasks-en", ui::MenuPage::Actions, false, 800, 600, ui::Language::English},
             {"tasks-inputs", ui::MenuPage::Actions, false, 800, 600},
             {"tasks-inputs-en", ui::MenuPage::Actions, false, 800, 600, ui::Language::English},
             {"control-paths", ui::MenuPage::Actions, false, 800, 600},
             {"control-paths-en", ui::MenuPage::Actions, false, 800, 600, ui::Language::English},
             {"control-paths-script", ui::MenuPage::Actions, false, 800, 600},
             {"control-paths-script-finished", ui::MenuPage::Actions, false, 800, 600},
             {"control-paths-toolbar", ui::MenuPage::Actions, false, 800, 600},
             {"status-values", ui::MenuPage::Actions, false, 800, 600, ui::Language::Chinese},
             {"status-long", ui::MenuPage::Actions, false, 800, 600, ui::Language::English}}) {
        auto* previous = ImGui::GetCurrentContext();
        auto* ctx = ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {float(scenario.width), float(scenario.height)};
        io.DeltaTime = 1.0f / 60;
        ui::AddUiFonts();
        Require(!io.Fonts->Fonts.empty());
        Require(io.Fonts->Build());
        Require(io.Fonts->Fonts[0]->FindGlyphNoFallback(0x221e) != nullptr); // official skin name includes infinity
        Require(io.Fonts->Fonts[0]->FindGlyphNoFallback(0x2605) != nullptr); // official character names include stars
        Require(ImGui_ImplDX11_Init(device, context));
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = scenario.width;
        desc.Height = scenario.height;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> image, staging;
        ComPtr<ID3D11RenderTargetView> target;
        Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &image)));
        Require(SUCCEEDED(device->CreateRenderTargetView(image.Get(), nullptr, &target)));
        ui::Menu menu(scenario.page, scenario.language);
        if (scenario.create)
            menu.OpenCreation(std::string(scenario.name).find("skin-") == 0 ? 2 : 1);
        if (std::string(scenario.name).find("layouts") == 0)
            menu.OpenLayouts();
        ApplicationService host;
        auto snapshot = Fixture();
        const bool task_menu = std::string(scenario.name).find("tasks") == 0;
        if (task_menu) {
            auto tasks = std::make_shared<CharacterTaskStatus>();
            tasks->domain = ActionDomain::Battle;
            TaskDefinition task;
            task.key = {"preview", "enter_phase"};
            task.entry = "entry";
            tasks->definitions.emplace(task.key.name, task);
            tasks->control.catalog.emplace(task.key.name, TaskQueryResult{});
            task.key.name = "unmapped";
            task.entry.clear();
            tasks->definitions.emplace(task.key.name, task);
            tasks->control.catalog[task.key.name].availability = TaskAvailability::Unknown;
            task.key.name = "needs_target";
            task.entry = "entry";
            task.inputs = {{"power", TaskValueType::Integer, std::int64_t(2)}};
            task.requirements = {TaskNeedsTarget{"Target"}};
            task.bindings.choices = {{"mode", {"ordinary", "enhanced"}}};
            tasks->definitions.emplace(task.key.name, task);
            tasks->control.catalog[task.key.name] = {
                TaskAvailability::Unavailable,
                {},
                {{TaskReasonKind::MissingInput, "Target", "owned target is unavailable"}}};
            tasks->control.contexts.push_back(
                {{1}, "phase-owner", {{"charged", true}, {"remaining", std::int64_t(3)}}, "enhanced", {}, {}});
            TaskDefinition setting;
            setting.key = {"preview", "response-setting"};
            setting.entry = "setting";
            setting.mode = TaskExecutionMode::Input;
            setting.sources = {{"fixture", "setting"}};
            TaskPresentationInfo info;
            info.label = "response-setting-marker";
            info.setting = true;
            info.contexts = {"response"};
            setting.presentation = std::move(info);
            tasks->definitions.emplace(setting.key.name, std::move(setting));
            tasks->control.catalog["response-setting"] = {};
            snapshot.characters[0].task_catalog = "preview";
            snapshot.characters[0].task_catalog_revision = 2;
            snapshot.characters[0].tasks = std::move(tasks);
            snapshot.characters[0].handoff->ready_domain = ActionDomain::Battle;
        }
        const bool control_menu = std::string(scenario.name).find("control-paths") == 0;
        const bool toolbar = std::string(scenario.name) == "control-paths-toolbar";
        const bool manual_script = toolbar || std::string(scenario.name).find("control-paths-script") == 0;
        const bool script_finished = std::string(scenario.name).find("-finished") != std::string::npos;
        if (control_menu) {
            auto tasks = std::make_shared<CharacterTaskStatus>();
            tasks->domain = ActionDomain::Battle;
            TaskDefinition task;
            task.key = {"control-preview", "select"};
            task.entry = "entry";
            task.presentation = TaskPresentationInfo{"Skill selection", {}, true};
            task.sources = {{"fixture", "consumer"}};
            tasks->definitions.emplace("select", task);
            tasks->control.catalog["select"] = {};
            task.key.name = "automatic_reset";
            task.trigger = TaskTrigger::Internal;
            tasks->definitions.emplace(task.key.name, task);
            tasks->control.catalog[task.key.name] = {};
            if (manual_script) {
                task.key.name = "held_script";
                task.trigger = TaskTrigger::User;
                task.presentation = TaskPresentationInfo{"Held script", {}, true};
                tasks->definitions.emplace(task.key.name, task);
                tasks->control.catalog[task.key.name] = {};
                tasks->control.status.current = task.key;
                tasks->control.status.execution = TaskExecutionStatus{};
                if (script_finished)
                    tasks->control.status.execution->phase = TaskExecutionPhase::Finished;
            }
            snapshot.characters[0].task_catalog = "control-preview";
            snapshot.characters[0].tasks = tasks;
            snapshot.characters[0].handoff->ready_domain = ActionDomain::Battle;
        }
        if (std::string(scenario.name).find("status-") == 0) {
            auto states = std::make_shared<CharacterTaskStatus>();
            TaskContextView actor_context;
            actor_context.name = "long_named_context";
            actor_context.token = {1};
            actor_context.phase = "enhanced";
            actor_context.values = {{"item", std::string("missile")},
                                    {"remaining_shots_in_current_sequence", std::int64_t(3)}};
            states->control.contexts.push_back(actor_context);
            actor_context = {};
            actor_context.name = "mode";
            actor_context.token = {2};
            actor_context.values = {{"energy", 3.5}};
            states->control.contexts.push_back(actor_context);
            actor_context = {};
            actor_context.name = "actor";
            actor_context.token = {3};
            actor_context.values = {{"selected", std::string("unwanted-bookkeeping")}};
            states->control.contexts.push_back(actor_context);
            snapshot.characters[0].tasks = std::move(states);
        }
        const bool battle = std::string(scenario.name).find("battle") == 0;
        if (battle) {
            snapshot.characters[0].handoff = HandoffStatus{};
            snapshot.characters[0].handoff->ready_domain = ActionDomain::Battle;
        }
        ImGuiWindow* original_window = nullptr;
        ImGuiWindow* original_creation = nullptr;
        float original_list_y = 0;
        for (int f = 0; f < 3; ++f) {
            if (toolbar && f == 2) {
                auto tasks = std::make_shared<CharacterTaskStatus>(*snapshot.characters[0].tasks);
                tasks->control.status.execution->phase = TaskExecutionPhase::Finished;
                snapshot.characters[0].tasks = std::move(tasks);
            }
            if (f == 2 && scenario.switch_language)
                menu.SetLanguage(ui::Language::English);
            const bool states = std::string(scenario.name).find("battle-states") == 0;
            if (f == 1 && states)
                menu.OpenBattleStates();
            ImGui_ImplDX11_NewFrame();
            ImGui::NewFrame();
            if (f == 1 && std::string(scenario.name).find("tasks-inputs") == 0)
                for (const auto& entry : CharacterTasks(snapshot, 1))
                    if (entry.label == "needs_target")
                        menu.OpenTaskInputs(entry);
            if (f == 2)
                ImGui::LogToBuffer();
            menu.Render(host, snapshot);
            if (f == 2) {
                const std::string text = ImGui::GetCurrentContext()->LogBuffer.c_str();
                ImGui::LogFinish();
                Require(text.find("#1217") == std::string::npos && text.find("#1218") == std::string::npos);
                Require(text.find("unwanted-bookkeeping") == std::string::npos);
                Require(text.find(ui::Translate(ui::UiText::Language, menu.GetLanguage())) == std::string::npos);
                Require(text.find(ui::Translate(ui::UiText::BattleIdle, menu.GetLanguage())) == std::string::npos);
                Require(text.find(ui::Translate(ui::UiText::RefreshActions, menu.GetLanguage())) == std::string::npos);
                Require(text.find(ui::Translate(ui::UiText::Rebuild, menu.GetLanguage())) == std::string::npos);
                if (task_menu) {
                    Require(text.find("response-setting-marker") != std::string::npos);
                    Require(text.find("enter_phase") != std::string::npos &&
                            text.find("unmapped") == std::string::npos);
                    Require(text.find("charged: true") == std::string::npos &&
                            text.find("remaining: 3") == std::string::npos);
                    Require(text.find("needs_target") != std::string::npos);
                }
                if (control_menu) {
                    Require(text.find("Skill selection") != std::string::npos &&
                            text.find("automatic_reset") == std::string::npos);
                    Require(text.find("Stagger") != std::string::npos &&
                            text.find("Skill01Ready") != std::string::npos);
                    Require(text.find("ConfiguredRest") !=
                            std::string::npos); // a partial catalog cannot hide existing actions
                    Require((text.find(ui::Translate(ui::UiText::FinishTask, menu.GetLanguage())) !=
                             std::string::npos) == (manual_script && !script_finished && !toolbar));
                    Require((text.find(ui::Translate(ui::UiText::CancelTask, menu.GetLanguage())) !=
                             std::string::npos) == (manual_script && !script_finished && !toolbar));
                }
                if (states) {
#if HKSR_DEVELOPMENT_UI
                    Require(text.find(ui::Translate(ui::UiText::ActionPreviews, menu.GetLanguage())) !=
                            std::string::npos);
                    Require(text.find(ui::Translate(ui::UiText::IdleState, menu.GetLanguage())) == std::string::npos);
                    Require(text.find("Skill01Ready") == std::string::npos &&
                            text.find("Emo_Stun") == std::string::npos);
#else
                    Require(text.find("Skill01Ready") != std::string::npos);
                    Require(text.find("Stagger") != std::string::npos && text.find("Emo_Stun") != std::string::npos);
#endif
                    Require(text.find("技能准备态") == std::string::npos &&
                            text.find("Skill ready poses") == std::string::npos);
                } else if (battle) {
                    Require(text.find("ConfiguredRest") != std::string::npos);
                    Require(text.find("Stagger") != std::string::npos);
                }
                Require(text.find(ui::Translate(ui::UiText::ConfiguredActions, menu.GetLanguage())) ==
                        std::string::npos);
#if !HKSR_DEVELOPMENT_UI
                Require(text.find(ui::Translate(ui::UiText::ActionCategory, menu.GetLanguage())) == std::string::npos);
                Require(text.find(ui::Translate(ui::UiText::Diagnostics, menu.GetLanguage())) == std::string::npos);
                Require(text.find(ui::Translate(ui::UiText::ActionPreviews, menu.GetLanguage())) == std::string::npos);
                Require(text.find(ui::Translate(ui::UiText::ShowUnavailableTasks, menu.GetLanguage())) ==
                        std::string::npos);
                Require(text.find("remaining_shots_in_current_sequence") == std::string::npos);
#else
                if (std::string(scenario.name).find("status-") == 0) {
                    const auto marker = text.find("remaining_shots_in_current_sequence");
                    Require(marker != std::string::npos &&
                            text.find("remaining_shots_in_current_sequence", marker + 1) == std::string::npos);
                }
#endif
                Require(text.find(ui::Translate(ui::UiText::DeleteInstance, menu.GetLanguage())) == std::string::npos);
                Require(text.find(ui::Translate(ui::UiText::ApplyGaze, menu.GetLanguage())) == std::string::npos);
            }
            ImGui::Render();
            if (toolbar && f >= 1) {
                ImGuiWindow* list = nullptr;
                for (auto* candidate : ImGui::GetCurrentContext()->Windows)
                    if (candidate->Active && std::string(candidate->Name).find("/action-list") != std::string::npos)
                        list = candidate;
                Require(list != nullptr);
                if (f == 1)
                    original_list_y = list->Pos.y;
                else
                    Require(list->Pos.y == original_list_y);
            }
            auto* window =
                ImGui::FindWindowByID(ImHashStr(ui::LocalizedLabel(ui::UiText::WindowTitle, menu.GetLanguage())));
            auto* creation =
                ImGui::FindWindowByID(ImHashStr(ui::LocalizedLabel(ui::UiText::CreationTitle, menu.GetLanguage())));
            if (f == 1) {
                original_window = window;
                original_creation = creation;
            }
            if (f == 2 && scenario.switch_language)
                Require(window && window == original_window && creation && creation == original_creation);
            const float background[] = {0.055f, 0.07f, 0.09f, 1};
            context->ClearRenderTargetView(target.Get(), background);
            auto* rtv = target.Get();
            context->OMSetRenderTargets(1, &rtv, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
        for (const auto* window : ImGui::GetCurrentContext()->Windows)
            if (window->Active && !window->Hidden && !(window->Flags & ImGuiWindowFlags_Tooltip)) {
                if (window->Pos.x < 0 || window->Pos.y < 0 || window->Pos.x + window->Size.x > scenario.width + 1 ||
                    window->Pos.y + window->Size.y > scenario.height + 1 || window->ScrollMax.x != 0)
                    std::cerr << scenario.name << " window=" << window->Name << " pos=" << window->Pos.x << ","
                              << window->Pos.y << " size=" << window->Size.x << "," << window->Size.y
                              << " scrollX=" << window->ScrollMax.x << "\n";
                Require(window->Pos.x >= 0 && window->Pos.y >= 0 &&
                        window->Pos.x + window->Size.x <= scenario.width + 1 &&
                        window->Pos.y + window->Size.y <= scenario.height + 1);
                Require(window->ScrollMax.x == 0);
            }
        desc.BindFlags = 0;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)));
        context->CopyResource(staging.Get(), image.Get());
        D3D11_MAPPED_SUBRESOURCE data{};
        Require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &data)));
        Png(folder / (std::string(scenario.name) + ".png"), scenario.width, scenario.height, data);
        context->Unmap(staging.Get(), 0);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        ImGui_ImplDX11_Shutdown();
        ImGui::DestroyContext(ctx);
        ImGui::SetCurrentContext(previous);
    }
    CoUninitialize();
    std::cout
        << "Menu preview: production view with synthetic values, large/small layouts and PNG readback; no game.\n";
}
