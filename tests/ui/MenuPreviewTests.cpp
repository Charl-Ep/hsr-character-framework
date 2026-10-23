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
#include <stdexcept>
#include <iostream>
using Microsoft::WRL::ComPtr;
namespace {
void RequireAt(bool ok,int line){if(!ok)throw std::runtime_error("menu preview check failed at line "+std::to_string(line));}
#define Require(...) RequireAt((__VA_ARGS__),__LINE__)
void Png(const std::filesystem::path& file,UINT width,UINT height,const D3D11_MAPPED_SUBRESOURCE& map) {
    ComPtr<IWICImagingFactory> factory;
    Require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))));
    ComPtr<IWICStream> stream;Require(SUCCEEDED(factory->CreateStream(&stream)));
    Require(SUCCEEDED(stream->InitializeFromFilename(file.c_str(),GENERIC_WRITE)));
    ComPtr<IWICBitmapEncoder> encoder;Require(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder)));
    Require(SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache)));
    ComPtr<IWICBitmapFrameEncode> frame;Require(SUCCEEDED(encoder->CreateNewFrame(&frame,nullptr)));
    Require(SUCCEEDED(frame->Initialize(nullptr)));Require(SUCCEEDED(frame->SetSize(width,height)));
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;Require(SUCCEEDED(frame->SetPixelFormat(&format)));
    Require(IsEqualGUID(format,GUID_WICPixelFormat32bppBGRA));
    std::vector<BYTE> bgra(static_cast<std::size_t>(width)*height*4);
    for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x) {
        const auto* p=static_cast<const BYTE*>(map.pData)+y*map.RowPitch+x*4;
        auto* q=bgra.data()+(static_cast<std::size_t>(y)*width+x)*4;
        q[0]=p[2];q[1]=p[1];q[2]=p[0];q[3]=p[3];
    }
    Require(SUCCEEDED(frame->WritePixels(height,width*4,width*height*4,bgra.data())));
    Require(SUCCEEDED(frame->Commit()));Require(SUCCEEDED(encoder->Commit()));
}
hksr::framework::ApplicationSnapshot Fixture() {
    using namespace hksr::framework;namespace api=hksr::api;
    ApplicationSnapshot out;out.prepared=true;out.scene=SceneAddress{"preview-map",1};
    auto c=std::make_shared<CatalogSnapshot>();auto rows=std::make_shared<api::CharacterCatalog>();
    rows->players.rows={{1217,1217,0,0,"Config/ConfigCharacter/LocalPlayer/LocalPlayer_Huohuo_00_Config.json","Huohuo.prefab"},
        {1217,1217,1,0,"Config/ConfigCharacter/LocalPlayer/LocalPlayer_Huohuo_00_Config.json","Huohuo.prefab"},
        {1002,1002,0,0,"Config/ConfigCharacter/LocalPlayer/LocalPlayer_DanHeng_00_Config.json","DanHeng.prefab"}};
    rows->npcs.rows={{1218,"Config/ConfigCharacter/NPC/Avatar/NPC_Avatar_Huohuo_01_Config.json","Config/ConfigEntity/NPC/Avatar/NPC_Avatar_Girl_Huohuo_01_Entity.json"}};
    c->rows=rows;auto source=std::make_shared<SourceRequestStatus>();source->id=1;
    source->input=BrowsePlayer{{1217,0,0},{}};source->phase=SourceRequestPhase::Ready;source->result.finished=true;
    source->result.character=SourceCatalog::PlayerPackage(rows->players.rows.front(),rows->npcs.rows.front());c->sources[1]=source;
    auto actions=std::make_shared<CharacterActionCatalog>();actions->revision=1;actions->actions.finished=true;
    const char* states[]={"StandBy","Idle_Show_01","Idle_Show_02","Common_IdleShow","LookatPhoneBS","LookatPhoneLoop","LookatPhoneAS",
        "Walk","Run","TurnLeft","TurnRight","Very_Long_Animator_Name_With_A_Deeply_Nested_Path_To_Check_Clipping"};
    for(const auto* name:states)actions->actions.proposals.push_back({ActionDomain::Exploration,name,{}});
    actions->actions.proposals.push_back({ActionDomain::Alternate,"ConfiguredRest",{}});
    actions->actions.proposals.push_back({ActionDomain::Alternate,"Skill01",{}});
    auto ready=std::make_shared<ReadyActions>();ready->source="fixture/battle";
    ReadyPose pose;pose.state="Skill01Ready";pose.label="Skill01Ready";ready->poses.push_back(pose);
    actions->actions.ready_actions.push_back(ready);
    VisualState visual;visual.character_config="fixture/battle";visual.source="global";visual.value.flag={true,28,{}};
    visual.value.occur.resolved_state={true,"Stagger",{}};visual.value.loop.resolved_state={true,"Stagger_Loop",{}};
    visual.value.recover.resolved_state={true,"Stagger_Recover",{}};
    visual.value.occur.transition=visual.value.loop.transition=visual.value.recover.transition={true,0.15f,{}};
    visual.expression={true,"Emo_Stun",{}};visual.expression_transition={true,0.2f,{}};
    actions->actions.visual_states.push_back(visual);
    c->characters[1]=actions;
    out.catalog=c;
    SceneCharacterStatus actor;actor.id=1;actor.creations=1;actor.active=actor.creation_ready=true;actor.package=*source->result.character;actor.scene="preview-map";
    actor.handoff=HandoffStatus{};actor.playback=ActionPlaybackStatus{};
    ActionReceipt displayed;displayed.serial=1;displayed.request=FreeStyleState{"StandBy"};displayed.outcome=ActionOutcome::Returned;
    actor.playback->last=displayed;
    actor.placement={{6.575f,-15.015f,60.552f},{0,0,0,1}};actor.controls=ControlStatus{};
    actor.controls->live.ready=true;actor.controls->live.position=actor.placement.position;actor.controls->live.rotation=actor.placement.rotation;
    actor.battle_idle_state="ConfiguredRest";
    out.characters.push_back(actor);
    out.layouts.available=true;out.layouts.sequence=1;
    auto& layout=out.layouts.maps["preview-map"];layout.initialized=true;layout.phase="ready";layout.revision=layout.saved_revision=3;
    layout.names={"合影","舞台布局","Long named snapshot for layout selection"};
    actor.id=2;actor.package=SourceCatalog::NpcPackage(rows->npcs.rows.front());out.characters.push_back(actor);
    return out;
}
}
void TestMenuPreview(ID3D11Device* device,ID3D11DeviceContext* context) {
    using namespace hksr::framework;
    Require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)));
    const auto folder=std::filesystem::path(".build/ui-previews");std::filesystem::create_directories(folder);
    struct Scenario {const char* name;ui::MenuPage page;bool create;UINT width,height;ui::Language language=ui::Language::Chinese;bool switch_language=false;};
    for(const auto& scenario:std::vector<Scenario>{{"actions",ui::MenuPage::Actions,false,1200,840},
        {"controls",ui::MenuPage::Controls,false,1200,840},{"creation",ui::MenuPage::Actions,true,1200,840},
        {"small-actions",ui::MenuPage::Actions,false,800,600},{"small-creation",ui::MenuPage::Actions,true,800,600},
        {"actions-en",ui::MenuPage::Actions,false,1200,840,ui::Language::English},
        {"controls-en",ui::MenuPage::Controls,false,1200,840,ui::Language::English},
        {"creation-en",ui::MenuPage::Actions,true,1200,840,ui::Language::English},
        {"small-actions-en",ui::MenuPage::Actions,false,800,600,ui::Language::English},
        {"small-controls-en",ui::MenuPage::Controls,false,800,600,ui::Language::English},
        {"small-creation-en",ui::MenuPage::Actions,true,800,600,ui::Language::English},
        {"switch-language",ui::MenuPage::Controls,true,1200,840,ui::Language::Chinese,true},
        {"layouts",ui::MenuPage::Actions,false,800,600},
        {"layouts-en",ui::MenuPage::Actions,false,800,600,ui::Language::English},
        {"battle",ui::MenuPage::Actions,false,800,600},
        {"battle-en",ui::MenuPage::Actions,false,800,600,ui::Language::English},
        {"battle-states",ui::MenuPage::Actions,false,800,600},
        {"battle-states-en",ui::MenuPage::Actions,false,800,600,ui::Language::English},
        {"tasks",ui::MenuPage::Actions,false,800,600},
        {"tasks-en",ui::MenuPage::Actions,false,800,600,ui::Language::English},
        {"tasks-inputs",ui::MenuPage::Actions,false,800,600},
        {"tasks-inputs-en",ui::MenuPage::Actions,false,800,600,ui::Language::English},
        {"control-paths",ui::MenuPage::Actions,false,800,600},
        {"control-paths-en",ui::MenuPage::Actions,false,800,600,ui::Language::English},
        {"status-values",ui::MenuPage::Actions,false,800,600,ui::Language::Chinese},
        {"status-long",ui::MenuPage::Actions,false,800,600,ui::Language::English}}) {
        auto* previous=ImGui::GetCurrentContext();auto* ctx=ImGui::CreateContext();
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={float(scenario.width),float(scenario.height)};io.DeltaTime=1.0f/60;
        ui::AddUiFonts();Require(!io.Fonts->Fonts.empty());
        Require(ImGui_ImplDX11_Init(device,context));
        D3D11_TEXTURE2D_DESC desc{};desc.Width=scenario.width;desc.Height=scenario.height;desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> image,staging;ComPtr<ID3D11RenderTargetView> target;
        Require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&image)));
        Require(SUCCEEDED(device->CreateRenderTargetView(image.Get(),nullptr,&target)));
        ui::Menu menu(scenario.page,scenario.language);if(scenario.create)menu.OpenCreation(1);
        if(std::string(scenario.name).find("layouts")==0)menu.OpenLayouts();
        ApplicationService host;auto snapshot=Fixture();
        const bool task_menu=std::string(scenario.name).find("tasks")==0;
        if(task_menu) {
            auto tasks=std::make_shared<CharacterTaskStatus>();tasks->domain=ActionDomain::Alternate;
            TaskDefinition task;task.key={"preview","enter_phase"};task.entry="entry";
            tasks->definitions.emplace(task.key.name,task);tasks->control.catalog.emplace(task.key.name,TaskQueryResult{});
            task.key.name="unmapped";task.entry.clear();tasks->definitions.emplace(task.key.name,task);
            tasks->control.catalog[task.key.name].availability=TaskAvailability::Unknown;
            task.key.name="needs_target";task.entry="entry";
            task.inputs={{"power",TaskValueType::Integer,std::int64_t(2)}};
            task.requirements={TaskNeedsTarget{"Target"}};
            task.bindings.choices={{"mode",{"ordinary","enhanced"}}};
            tasks->definitions.emplace(task.key.name,task);
            tasks->control.catalog[task.key.name]={TaskAvailability::Unavailable,{},{{TaskReasonKind::MissingInput,"Target","owned target is unavailable"}}};
            tasks->control.contexts.push_back({{1},"phase-owner",{{"charged",true},{"remaining",std::int64_t(3)}},"enhanced",{}, {}});
            snapshot.characters[0].task_catalog="preview";snapshot.characters[0].task_catalog_revision=2;
            snapshot.characters[0].tasks=std::move(tasks);snapshot.characters[0].handoff->ready_domain=ActionDomain::Alternate;
        }
        const bool control_menu=std::string(scenario.name).find("control-paths")==0;
        if(control_menu) {
            auto tasks=std::make_shared<CharacterTaskStatus>();tasks->domain=ActionDomain::Alternate;
            auto relation=std::make_shared<ControlRelation>();relation->label="Skill selection";relation->sources={{"fixture","consumer"}};
            TaskDefinition task;task.key={"control-preview","select"};task.entry="entry";task.control=relation;
            tasks->definitions.emplace("select",task);tasks->control.catalog["select"]={};
            auto automatic=std::make_shared<ControlRelation>(*relation);automatic->trigger=ControlTrigger::Automatic;
            task.key.name="automatic_reset";task.control=automatic;tasks->definitions.emplace(task.key.name,task);tasks->control.catalog[task.key.name]={};
            snapshot.characters[0].task_catalog="control-preview";snapshot.characters[0].tasks=tasks;
            snapshot.characters[0].handoff->ready_domain=ActionDomain::Alternate;
        }
        if(std::string(scenario.name).find("status-")==0) {
            snapshot.characters[0].branch_state.phase="enhanced";
            snapshot.characters[0].branch_state.values={{"","item",std::string("missile")},
                {"long_named_context","remaining_shots_in_current_sequence",3.0f}};
        }
        const bool battle=std::string(scenario.name).find("battle")==0;
        if(battle){snapshot.characters[0].handoff=HandoffStatus{};snapshot.characters[0].handoff->ready_domain=ActionDomain::Alternate;}
        ImGuiWindow* original_window=nullptr;ImGuiWindow* original_creation=nullptr;
        for(int f=0;f<3;++f) {
            if(f==2 && scenario.switch_language)menu.SetLanguage(ui::Language::English);
            const bool states=std::string(scenario.name).find("battle-states")==0;
            if(f==1 && states)menu.OpenBattleStates();
            ImGui_ImplDX11_NewFrame();ImGui::NewFrame();
            if(f==1 && std::string(scenario.name).find("tasks-inputs")==0)
                for(const auto& entry:CharacterTasks(snapshot,1))if(entry.label=="needs_target")menu.OpenTaskInputs(entry);
            if(f==2)ImGui::LogToBuffer();
            menu.Render(host,snapshot);
            if(f==2) {
                const std::string text=ImGui::GetCurrentContext()->LogBuffer.c_str();ImGui::LogFinish();
                Require(text.find("#1217")==std::string::npos && text.find("#1218")==std::string::npos);
                Require(text.find(ui::Translate(ui::UiText::BattleIdle,menu.GetLanguage()))==std::string::npos);
                Require(text.find(ui::Translate(ui::UiText::RefreshActions,menu.GetLanguage()))==std::string::npos);
                Require(text.find(ui::Translate(ui::UiText::Rebuild,menu.GetLanguage()))==std::string::npos);
                if(task_menu) {
                    Require(text.find("enter_phase")!=std::string::npos && text.find("unmapped")==std::string::npos);
                    Require(text.find("charged: true")!=std::string::npos && text.find("remaining: 3")!=std::string::npos);
                    Require(text.find("needs_target")!=std::string::npos);
                }
                if(control_menu) {
                    Require(text.find("Skill selection")!=std::string::npos && text.find("automatic_reset")==std::string::npos);
                    Require(text.find("Stagger")==std::string::npos && text.find("Skill01Ready")==std::string::npos);
                    Require(text.find(ui::Translate(ui::UiText::FinishTask,menu.GetLanguage()))==std::string::npos);
                }
                if(states) {
#if HKSR_DEVELOPMENT_UI
                    Require(text.find(ui::Translate(ui::UiText::BattleStates,menu.GetLanguage()))!=std::string::npos);
#endif
                    Require(text.find(ui::Translate(ui::UiText::IdleState,menu.GetLanguage()))!=std::string::npos);
                    Require(text.find("Skill01Ready")!=std::string::npos);
                    Require(text.find("Stagger")!=std::string::npos && text.find("Emo_Stun")!=std::string::npos);
                    Require(text.find("技能准备态")==std::string::npos && text.find("Skill ready poses")==std::string::npos);
                } else if(battle) {
                    Require(text.find("ConfiguredRest")!=std::string::npos);
                    Require(text.find("Stagger")!=std::string::npos);
#if HKSR_DEVELOPMENT_UI
                    Require(text.find(ui::Translate(ui::UiText::ConfiguredActions,menu.GetLanguage()))<
                        text.find(ui::Translate(ui::UiText::Exploration,menu.GetLanguage())));
#endif
                }
#if !HKSR_DEVELOPMENT_UI
                Require(text.find(ui::Translate(ui::UiText::ActionCategory,menu.GetLanguage()))==std::string::npos);
                Require(text.find(ui::Translate(ui::UiText::Diagnostics,menu.GetLanguage()))==std::string::npos);
#endif
                Require(text.find(ui::Translate(ui::UiText::DeleteInstance,menu.GetLanguage()))==std::string::npos);
                Require(text.find(ui::Translate(ui::UiText::ApplyGaze,menu.GetLanguage()))==std::string::npos);
            }
            ImGui::Render();
            auto* window=ImGui::FindWindowByID(ImHashStr(ui::LocalizedLabel(ui::UiText::WindowTitle,menu.GetLanguage())));
            auto* creation=ImGui::FindWindowByID(ImHashStr(ui::LocalizedLabel(ui::UiText::CreationTitle,menu.GetLanguage())));
            if(f==1){original_window=window;original_creation=creation;}
            if(f==2 && scenario.switch_language)Require(window && window==original_window && creation && creation==original_creation);
            const float background[]={0.055f,0.07f,0.09f,1};context->ClearRenderTargetView(target.Get(),background);
            auto* rtv=target.Get();context->OMSetRenderTargets(1,&rtv,nullptr);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
        for(const auto* window:ImGui::GetCurrentContext()->Windows)if(window->Active && !window->Hidden && !(window->Flags&ImGuiWindowFlags_Tooltip)) {
            if(window->Pos.x<0 || window->Pos.y<0 || window->Pos.x+window->Size.x>scenario.width+1 || window->Pos.y+window->Size.y>scenario.height+1 || window->ScrollMax.x!=0)
                std::cerr<<scenario.name<<" window="<<window->Name<<" pos="<<window->Pos.x<<","<<window->Pos.y<<" size="<<window->Size.x<<","<<window->Size.y<<" scrollX="<<window->ScrollMax.x<<"\n";
            Require(window->Pos.x>=0 && window->Pos.y>=0 && window->Pos.x+window->Size.x<=scenario.width+1 && window->Pos.y+window->Size.y<=scenario.height+1);
            Require(window->ScrollMax.x==0);
        }
        desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)));context->CopyResource(staging.Get(),image.Get());
        D3D11_MAPPED_SUBRESOURCE data{};Require(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&data)));
        Png(folder/(std::string(scenario.name)+".png"),scenario.width,scenario.height,data);context->Unmap(staging.Get(),0);
        context->OMSetRenderTargets(0,nullptr,nullptr);ImGui_ImplDX11_Shutdown();ImGui::DestroyContext(ctx);ImGui::SetCurrentContext(previous);
    }
    CoUninitialize();std::cout<<"Menu preview: production view with synthetic values, large/small layouts and PNG readback; no game.\n";
}
