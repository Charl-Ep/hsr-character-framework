#include "../../src/ui/Overlay.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include "../../src/ui/Widgets.hpp"
#include "../../src/ui/MouseRouting.hpp"
#include <Windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <iostream>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
void TestMouseMode();
void TestMenuPreview(ID3D11Device*,ID3D11DeviceContext*);
void TestLocalization();
namespace {
void Check(bool value) {if(!value) throw std::runtime_error("overlay isolation test failed");}
int forwarded=0,mouse_forwarded=0,focus_forwarded=0;
LRESULT CALLBACK Original(HWND window,UINT msg,WPARAM w,LPARAM l) {
    if(msg==WM_APP+1) ++forwarded;
    if((msg>=WM_MOUSEFIRST && msg<=WM_MOUSELAST) || msg==WM_INPUT)++mouse_forwarded;
    if(msg==WM_SETFOCUS || msg==WM_KILLFOCUS)++focus_forwarded;
    return DefWindowProcW(window,msg,w,l);
}
void Layout() {
    auto* old=ImGui::GetCurrentContext();auto* ctx=ImGui::CreateContext();
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1000,800};io.DeltaTime=1.0f/60;
    unsigned char* pixels=nullptr;int w=0,h=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    for(int frame=0;frame<3;++frame) {
        ImGui::NewFrame();ImGui::Begin("layout");
        hksr::framework::ui::BeginHelp();
        ImGui::TextWrapped("Config/ConfigCharacter/Avatar/VeryLongCharacterDirectory/SomeLongCharacterConfigurationWithoutSpaces.json");
        const auto width=ImGui::GetCurrentWindow()->Size.x;
        hksr::framework::ui::EndHelp();
        if(frame==2)Check(width>300 && width<800);
        ImGui::OpenPopupEx(ImHashStr("##ComboPopup",0,ImGui::GetID("choices")),ImGuiPopupFlags_None);
        if(hksr::framework::ui::BeginChoices("choices","large list")) {
            for(int i=0;i<40;++i)ImGui::Selectable(std::to_string(i).c_str());
            if(frame==2) {
                auto* popup=ImGui::GetCurrentWindow();Check(popup->Size.y>250);
                ImGuiWindow* hovered=nullptr;
                ImGui::FindHoveredWindowEx(popup->OuterRectClipped.GetCenter(),true,&hovered,nullptr);
                Check(hovered==popup);
            }
            ImGui::EndCombo();
        } else throw std::runtime_error("list popup failed to open");
        ImGui::End();ImGui::Render();
    }
    ImGui::DestroyContext(ctx);ImGui::SetCurrentContext(old);
}
void Routing() {
    hksr::framework::ui::MouseRouting route;
    Check(!route.Motion(false) && route.Motion(true));
    Check(route.Button(0,true,true));Check(route.Motion(false));
    Check(route.Button(0,false,false));Check(!route.Motion(false));
    Check(!route.Button(1,true,false));Check(!route.Motion(true));
    Check(!route.Button(1,false,true));Check(route.Motion(true));
    Check(route.Button(3,true,true));route.Reset();Check(!route.Motion(false));
}
void PlacementButtons() {
    auto* old=ImGui::GetCurrentContext();auto* ctx=ImGui::CreateContext();
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={800,600};io.DeltaTime=0.05f;
    unsigned char* pixels=nullptr;int w=0,h=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    std::vector<std::pair<int,float>> submitted;
    auto draw=[&] {
        ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({400,400});
        ImGui::Begin("steps");ImGui::SetCursorPos({10,30});
        hksr::framework::ui::DrawPlacementSteps(0.25f,15,[&](int axis,float amount){submitted.emplace_back(axis,amount);});
        ImGui::End();ImGui::Render();
    };
    draw();draw();
    const float row=30+ImGui::GetStyle().ItemSpacing.y;
    struct Key{float x,y;int axis;float direction;};
    const Key keys[]={{36,45+row,0,-1},{156,45+row,0,1},{232,45+2*row,1,-1},{232,45,1,1},
        {96,45+2*row,2,-1},{96,45,2,1},{51,45+3*row,3,-1},{141,45+3*row,3,1},{232,45+row,4,0}};
    for(const auto& key:keys) {
        const auto before=submitted.size();io.AddMousePosEvent(key.x,key.y);draw();
        io.AddMouseButtonEvent(0,true);draw();io.AddMouseButtonEvent(0,false);draw();
        Check(submitted.size()==before+1 && submitted.back().first==key.axis &&
            submitted.back().second==key.direction*(key.axis==3?15.0f:0.25f));
    }
    const auto before=submitted.size();io.AddMousePosEvent(156,45+row);draw();io.AddMouseButtonEvent(0,true);
    for(int frame=0;frame<20;++frame)draw();
    io.AddMouseButtonEvent(0,false);draw();Check(submitted.size()>before+1);
    ImGui::DestroyContext(ctx);ImGui::SetCurrentContext(old);
}
}
int main() {
    try {
        TestLocalization();TestMouseMode();Layout();PlacementButtons();Routing();
        WNDCLASSW klass{};klass.lpfnWndProc=Original;klass.hInstance=GetModuleHandleW(nullptr);klass.lpszClassName=L"HksrHiddenUiTest";
        Check(RegisterClassW(&klass)!=0);
        HWND window=CreateWindowW(klass.lpszClassName,L"hidden",WS_OVERLAPPEDWINDOW,0,0,900,750,nullptr,nullptr,klass.hInstance,nullptr);
        Check(window!=nullptr); // never shown, no game started
        DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=900;desc.BufferDesc.Height=750;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.OutputWindow=window;desc.Windowed=true;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<IDXGISwapChain> chain;
        Check(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&chain,&device,nullptr,&context)));
        TestMenuPreview(device.Get(),context.Get());
        ComPtr<ID3DBlob> bytecode,error;const char shader[]="[numthreads(1,1,1)] void main() {}";
        Check(SUCCEEDED(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&bytecode,&error)));
        ComPtr<ID3D11ComputeShader> cs;Check(SUCCEEDED(device->CreateComputeShader(bytecode->GetBufferPointer(),bytecode->GetBufferSize(),nullptr,&cs)));
        context->CSSetShader(cs.Get(),nullptr,0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        const D3D11_VIEWPORT viewport{3,5,333,222,0.2f,0.7f};context->RSSetViewports(1,&viewport);
        hksr::framework::FrameworkHost host;hksr::framework::ui::Overlay menu;
        {
            ComPtr<ID3D11Texture2D> back;ComPtr<ID3D11RenderTargetView> target;
            Check(SUCCEEDED(chain->GetBuffer(0,IID_PPV_ARGS(&back))));Check(SUCCEEDED(device->CreateRenderTargetView(back.Get(),nullptr,&target)));
            const float black[4]{};context->ClearRenderTargetView(target.Get(),black);
        }
        menu.Draw(chain.Get(),host);menu.Draw(chain.Get(),host);
        if(!menu.Error().empty()) throw std::runtime_error(menu.Error());
        Check(!host.Application().Snapshot().prepared && host.Application().TakeReplies().empty());
        ComPtr<ID3D11ComputeShader> observed;context->CSGetShader(&observed,nullptr,nullptr);Check(observed.Get()==cs.Get());
        D3D11_PRIMITIVE_TOPOLOGY topology;context->IAGetPrimitiveTopology(&topology);Check(topology==D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        D3D11_VIEWPORT after{};UINT count=1;context->RSGetViewports(&count,&after);Check(count==1 && after.TopLeftX==3 && after.Width==333 && after.MinDepth==0.2f);
        {
            ComPtr<ID3D11Texture2D> back,readback;Check(SUCCEEDED(chain->GetBuffer(0,IID_PPV_ARGS(&back))));
            D3D11_TEXTURE2D_DESC input{};back->GetDesc(&input);input.BindFlags=0;input.Usage=D3D11_USAGE_STAGING;input.CPUAccessFlags=D3D11_CPU_ACCESS_READ;input.MiscFlags=0;
            Check(SUCCEEDED(device->CreateTexture2D(&input,nullptr,&readback)));context->CopyResource(readback.Get(),back.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};Check(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)));
            std::size_t colored=0;
            for(UINT y=0;y<input.Height;++y) for(UINT x=0;x<input.Width;++x) {
                const auto* pixel=static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch+4*x;
                if(pixel[0] || pixel[1] || pixel[2]) ++colored;
            }
            context->Unmap(readback.Get(),0);Check(colored>1000); // actually drew UI, not a successful no-op
        }
        // UI cannot retain a back buffer through its own inactive context state.
        Check(SUCCEEDED(chain->ResizeBuffers(2,800,600,DXGI_FORMAT_R8G8B8A8_UNORM,0)));
        menu.Draw(chain.Get(),host);Check(menu.Error().empty());
        SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(200,180));menu.Draw(chain.Get(),host);menu.Draw(chain.Get(),host);
        SendMessageW(window,WM_KILLFOCUS,0,0);SendMessageW(window,WM_ACTIVATEAPP,FALSE,0);
        SendMessageW(window,WM_ACTIVATEAPP,TRUE,0);SendMessageW(window,WM_SETFOCUS,0,0);
        const auto before_mouse=mouse_forwarded;
        SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(200,180));
        SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(899,749)); // outside menu goes to game
        // An invalid/non-mouse raw packet must not be indiscriminately eaten.
        Check(mouse_forwarded==before_mouse+1 && focus_forwarded>=2);
        auto sent=mouse_forwarded;
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(200,180));
        SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(899,749));
        SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(899,749));
        Check(mouse_forwarded==sent); // menu drag and its release stay in menu
        SendMessageW(window,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(899,749));
        SendMessageW(window,WM_MOUSEMOVE,MK_RBUTTON,MAKELPARAM(200,180));
        SendMessageW(window,WM_RBUTTONUP,0,MAKELPARAM(200,180));
        Check(mouse_forwarded==sent+3); // game drag can cross menu intact
        POINT inside{200,180},outside{899,749};ClientToScreen(window,&inside);ClientToScreen(window,&outside);
        SendMessageW(window,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),MAKELPARAM(inside.x,inside.y));
        SendMessageW(window,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),MAKELPARAM(outside.x,outside.y));
        Check(mouse_forwarded==sent+4);
        SendMessageW(window,WM_SYSKEYDOWN,'H',(1LL<<29));menu.Draw(chain.Get(),host);
        SendMessageW(window,WM_SYSKEYDOWN,'H',(1LL<<29)); // duplicate down cannot toggle back
        Check(menu.InputState().find("menu=0 ")==0);
        SendMessageW(window,WM_SYSKEYUP,'H',(1LL<<29));
        SendMessageW(window,WM_SYSKEYDOWN,'H',(1LL<<29));menu.Draw(chain.Get(),host);
        SendMessageW(window,WM_SYSKEYUP,'H',(1LL<<29));
        menu.Stop();SendMessageW(window,WM_APP+1,0,0);Check(forwarded==1);
        Check(SUCCEEDED(chain->ResizeBuffers(2,640,480,DXGI_FORMAT_R8G8B8A8_UNORM,0)));
        DestroyWindow(window);UnregisterClassW(klass.lpszClassName,klass.hInstance);
        std::cout<<"Overlay tests passed on hidden WARP swap chain: own context, compute/topology/viewport preservation, resize, input toggling and stopped forwarding; no game.\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
