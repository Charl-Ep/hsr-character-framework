#include "Overlay.hpp"
#include "Menu.hpp"
#include "MouseRouting.hpp"
#include "UiPreferences.hpp"
#include "UiFonts.hpp"
#include <Windows.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <filesystem>
#include <stdexcept>
#include <sstream>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);

namespace hksr::framework::ui {
using Microsoft::WRL::ComPtr;
namespace {
constexpr auto property=L"HKSR.CharacterFramework.Menu";
void Require(HRESULT hr,const char* operation) {
    if(FAILED(hr)) throw std::runtime_error(std::string(operation)+" HRESULT="+std::to_string(static_cast<unsigned long>(hr)));
}
struct ContextScope {
    ImGuiContext* previous=ImGui::GetCurrentContext();
    explicit ContextScope(ImGuiContext* context) {ImGui::SetCurrentContext(context);}
    ~ContextScope() {ImGui::SetCurrentContext(previous);}
};
struct DeviceScope {
    ID3D11DeviceContext1* context;
    ComPtr<ID3DDeviceContextState> previous;
    DeviceScope(ID3D11DeviceContext1* ctx,ID3DDeviceContextState* own):context(ctx) {context->SwapDeviceContextState(own,&previous);}
    ~DeviceScope() {
        // Drop the UI state's back-buffer binding BEFORE saving it. Holding
        // an RTV in an inactive context state would prevent ResizeBuffers.
        context->ClearState();context->SwapDeviceContextState(previous.Get(),nullptr);
    }
};
}
struct Overlay::State {
    std::recursive_mutex mutex;
    HWND window=nullptr;
    WNDPROC previous=nullptr;
    ImGuiContext* imgui=nullptr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext1> context;
    ComPtr<ID3DDeviceContextState> render_state;
    bool win32=false,dx11=false,visible=true,stopped=false,failed=false;
    bool foreground=true,toggle_down=false;
    FrameworkHost* host=nullptr;
    MouseRouting mouse_routes,raw_routes;
    unsigned long long mouse_sent=0,mouse_blocked=0,raw_sent=0,raw_blocked=0;
    UINT last_mouse=0;bool last_mouse_blocked=false;
    unsigned char keys[256]{}; // 1: down forwarded to game, 2: owned by menu
    std::string error;
    Menu menu;
    std::filesystem::path preferences;
    bool Inside(POINT point) const {
        if(!visible || !foreground)return false;
        ImGuiWindow* hovered=nullptr;
        // Use this event's position, not last frame's WantCaptureMouse. This
        // includes actual popup/child bounds and ImGui's resize hit margins.
        ImGui::FindHoveredWindowEx(ImVec2(static_cast<float>(point.x),static_cast<float>(point.y)),true,&hovered,nullptr);
        return hovered!=nullptr;
    }
    static LRESULT CALLBACK Window(HWND window,UINT msg,WPARAM w,LPARAM l) {
        auto* state=static_cast<State*>(GetPropW(window,property));
        if(!state) return DefWindowProcW(window,msg,w,l);
        WNDPROC original=nullptr;bool handled=false;
        {
            std::lock_guard<std::recursive_mutex> lock(state->mutex);original=state->previous;
            // Inactive states remain valid forwarders if another mod has
            // chained above us. Never overwrite that mod's WNDPROC on stop.
            if(state->imgui && state->window==window && !state->stopped) {
                ContextScope scope(state->imgui);
                const bool toggle=(msg==WM_SYSKEYDOWN || msg==WM_KEYDOWN) && w=='H' && (l&(1LL<<29));
                if(toggle) {if(!state->toggle_down)state->visible=!state->visible;state->toggle_down=true;handled=true;}
                if((msg==WM_KEYUP || msg==WM_SYSKEYUP) && w=='H')state->toggle_down=false;
                try {
                    auto& io=ImGui::GetIO();
                    if(msg==WM_KILLFOCUS || (msg==WM_ACTIVATEAPP && !w)) {
                        state->foreground=false;state->toggle_down=false;
                        state->mouse_routes.Reset();state->raw_routes.Reset();
                        io.AddFocusEvent(false);
                    }
                    if(msg==WM_SETFOCUS || (msg==WM_ACTIVATEAPP && w)) {
                        state->foreground=true;io.AddFocusEvent(true);
                    }
                    // Continue delivering releases/focus while hidden so a
                    // captured button never stays stuck on the next opening.
                    const bool release=msg==WM_LBUTTONUP || msg==WM_RBUTTONUP || msg==WM_MBUTTONUP || msg==WM_XBUTTONUP ||
                        msg==WM_KEYUP || msg==WM_SYSKEYUP || msg==WM_KILLFOCUS || msg==WM_SETFOCUS || msg==WM_DESTROY;
                    if(state->visible || release) ImGui_ImplWin32_WndProcHandler(window,msg,w,l);
                    const bool mouse=msg>=WM_MOUSEFIRST && msg<=WM_MOUSELAST;
                    const bool keyboard=msg>=WM_KEYFIRST && msg<=WM_KEYLAST;
                    const bool cursor_mode=state->visible && state->foreground;
                    if(state->host)state->host->SetMenuMouse(cursor_mode);
                    if(state->foreground && msg==WM_INPUT) {
                        RAWINPUT packet{};UINT bytes=sizeof(packet);
                        if(GetRawInputData(reinterpret_cast<HRAWINPUT>(l),RID_INPUT,&packet,&bytes,sizeof(RAWINPUTHEADER))!=static_cast<UINT>(-1) && packet.header.dwType==RIM_TYPEMOUSE) {
                            POINT point{};GetCursorPos(&point);ScreenToClient(window,&point);
                            const bool inside=state->Inside(point);
                            bool capture=state->raw_routes.Motion(inside);
                            const auto flags=packet.data.mouse.usButtonFlags;
                            for(unsigned i=0;i<5;++i) {
                                if(flags&(1u<<(2*i)))capture=state->raw_routes.Button(i,true,inside);
                                if(flags&(2u<<(2*i)))capture=state->raw_routes.Button(i,false,inside);
                            }
                            if(capture){++state->raw_blocked;return DefWindowProcW(window,msg,w,l);}
                            ++state->raw_sent;
                        }
                    }
                    if(mouse && state->foreground) {
                        POINT point{static_cast<short>(LOWORD(l)),static_cast<short>(HIWORD(l))};
                        if(msg==WM_MOUSEWHEEL || msg==WM_MOUSEHWHEEL)ScreenToClient(window,&point);
                        const bool inside=state->Inside(point);
                        bool capture=state->mouse_routes.Motion(inside);
                        switch(msg) {
                        case WM_LBUTTONDOWN:case WM_LBUTTONDBLCLK:capture=state->mouse_routes.Button(0,true,inside);break;
                        case WM_LBUTTONUP:capture=state->mouse_routes.Button(0,false,inside);break;
                        case WM_RBUTTONDOWN:case WM_RBUTTONDBLCLK:capture=state->mouse_routes.Button(1,true,inside);break;
                        case WM_RBUTTONUP:capture=state->mouse_routes.Button(1,false,inside);break;
                        case WM_MBUTTONDOWN:case WM_MBUTTONDBLCLK:capture=state->mouse_routes.Button(2,true,inside);break;
                        case WM_MBUTTONUP:capture=state->mouse_routes.Button(2,false,inside);break;
                        case WM_XBUTTONDOWN:case WM_XBUTTONDBLCLK:capture=state->mouse_routes.Button(HIWORD(w)==XBUTTON1?3:4,true,inside);break;
                        case WM_XBUTTONUP:capture=state->mouse_routes.Button(HIWORD(w)==XBUTTON1?3:4,false,inside);break;
                        }
                        if(capture)handled=true;
                        if(capture)++state->mouse_blocked;else ++state->mouse_sent;
                        if(msg!=WM_MOUSEMOVE){state->last_mouse=msg;state->last_mouse_blocked=capture;}
                    }
                    if(cursor_mode && keyboard && io.WantCaptureKeyboard)handled=true;
                    if(keyboard && w<256) {
                        if(msg==WM_KEYDOWN || msg==WM_SYSKEYDOWN) {
                            if(!(l&(1LL<<30)))state->keys[w]=handled?2:1;
                        } else if(msg==WM_KEYUP || msg==WM_SYSKEYUP) {
                            // Pair Alt/other releases with the receiver of the
                            // corresponding down, even if focus moved meanwhile.
                            if(state->keys[w])handled=state->keys[w]==2;
                            state->keys[w]=0;
                        }
                    }
                    if(cursor_mode && msg==WM_SYSCOMMAND && (w&0xfff0)==SC_KEYMENU)handled=true;
                } catch(...) {} // never unwind through the game's window proc
            }
            if(msg==WM_NCDESTROY) {RemovePropW(window,property);state->window=nullptr;state->previous=nullptr;}
        }
        if(handled) return 0;
        return original?CallWindowProcW(original,window,msg,w,l):DefWindowProcW(window,msg,w,l);
    }
    void Release() {
        if(host)host->SetMenuMouse(false);
        // Keep a pass-through input forwarder until WM_NCDESTROY. Restoring a
        // WNDPROC then deleting its state cannot prove other threads/overlays
        // no longer hold a call into it. The module and this state are pinned.
        if(imgui) {
            auto* old=ImGui::GetCurrentContext();ImGui::SetCurrentContext(imgui);
            if(dx11) ImGui_ImplDX11_Shutdown();
            if(win32) ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext(imgui);ImGui::SetCurrentContext(old==imgui?nullptr:old);imgui=nullptr;
        }
        dx11=win32=false;render_state.Reset();context.Reset();device.Reset();
    }
    void Initialize(ID3D11Device* input,HWND target) {
        if(window && window!=target) throw std::runtime_error("UI window changed while an input chain remains installed");
        device=input;ComPtr<ID3D11Device1> device1;Require(device.As(&device1),"ID3D11Device1");
        device1->GetImmediateContext1(&context);
        const auto level=device->GetFeatureLevel();
        const auto flags=(device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0u;
        Require(device1->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device1),nullptr,&render_state),"UI context state");
        auto* old=ImGui::GetCurrentContext();imgui=ImGui::CreateContext();ImGui::SetCurrentContext(old);
        ContextScope scope(imgui);auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
        io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NoMouseCursorChange;
        AddUiFonts();
        if(!(win32=ImGui_ImplWin32_Init(target))) throw std::runtime_error("Win32 UI initialization failed");
        if(!(dx11=ImGui_ImplDX11_Init(device.Get(),context.Get()))) throw std::runtime_error("D3D UI initialization failed");
        if(!window) {
            if(GetPropW(target,property)) throw std::runtime_error("UI input owner already exists");
            window=target;
            if(!SetPropW(target,property,this)) {window=nullptr;throw std::runtime_error("UI input property failed");}
            SetLastError(0);previous=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(target,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(Window)));
            if(!previous && GetLastError()) {RemovePropW(target,property);window=nullptr;throw std::runtime_error("UI input attachment failed");}
        }
    }
};
Overlay::Overlay():state_(new State){}
void Overlay::ConfigurePreferences(std::filesystem::path file) {
    const auto language=ReadLanguagePreference(file,DefaultLanguage());
    std::lock_guard<std::recursive_mutex> lock(state_->mutex);
    state_->preferences=std::move(file);state_->menu.SetLanguage(language,false);
}
std::string Overlay::FlushPreferences() {
    std::filesystem::path file;std::optional<Language> language;
    {std::lock_guard<std::recursive_mutex> lock(state_->mutex);file=state_->preferences;language=state_->menu.TakeLanguageChange();}
    if(!language || file.empty())return {};
    const bool saved=WriteLanguagePreference(file,*language);
    {std::lock_guard<std::recursive_mutex> lock(state_->mutex);state_->menu.SettingsSaved(saved);}
    return saved?std::string{}:"UI language setting could not be saved: "+file.u8string();
}
void Overlay::Draw(IDXGISwapChain* chain,FrameworkHost& host) {
    auto& s=*state_;std::lock_guard<std::recursive_mutex> lock(s.mutex);
    s.host=&host;
    if(!chain || s.stopped || s.failed) return;
    try {
        DXGI_SWAP_CHAIN_DESC description{};Require(chain->GetDesc(&description),"swap-chain description");
        if(!description.OutputWindow || !IsWindow(description.OutputWindow)) return;
        if(s.imgui && !s.window) s.Release(); // previous HWND received WM_NCDESTROY
        // One menu on its original window, not every auxiliary swap chain.
        if(s.window && s.window!=description.OutputWindow) return;
        ComPtr<ID3D11Device> device;Require(chain->GetDevice(IID_PPV_ARGS(&device)),"swap-chain device");
        if(s.device && s.device.Get()!=device.Get()) s.Release();
        if(!s.imgui) s.Initialize(device.Get(),description.OutputWindow);
        ContextScope imgui(s.imgui);DeviceScope state(s.context.Get(),s.render_state.Get());
        const bool foreground=GetForegroundWindow()==s.window;
        s.foreground=foreground; // reconcile real OS ownership, not only received focus messages
        host.SetMenuMouse(s.visible && foreground);
        ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();
        if(s.visible && !host.Application().Snapshot().closing) s.menu.Draw(host.Application());
        ImGui::Render();
        if(!s.visible || !ImGui::GetDrawData()->CmdListsCount) return;
        ComPtr<ID3D11Texture2D> buffer;Require(chain->GetBuffer(0,IID_PPV_ARGS(&buffer)),"UI back buffer");
        ComPtr<ID3D11RenderTargetView> view;Require(s.device->CreateRenderTargetView(buffer.Get(),nullptr,&view),"UI target");
        auto* rtv=view.Get();s.context->OMSetRenderTargets(1,&rtv,nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    } catch(const std::exception& error) {s.error=error.what();s.failed=true;s.Release();}
}
void Overlay::Stop() {auto& s=*state_;std::lock_guard<std::recursive_mutex> lock(s.mutex);s.stopped=true;s.Release();}
std::string Overlay::Error() const {std::lock_guard<std::recursive_mutex> lock(state_->mutex);return state_->error;}
std::string Overlay::InputState() const {
    auto& s=*state_;std::lock_guard<std::recursive_mutex> lock(s.mutex);
    std::ostringstream out;out<<"menu="<<s.visible<<" reported_foreground="<<s.foreground;
    if(s.imgui) {ContextScope scope(s.imgui);const auto& io=ImGui::GetIO();
        out<<" imgui_mouse="<<io.WantCaptureMouse<<" imgui_keyboard="<<io.WantCaptureKeyboard;}
    CURSORINFO cursor{sizeof(cursor)};RECT clip{};
    out<<" foreground="<<(s.window && GetForegroundWindow()==s.window)<<" alt="<<((GetAsyncKeyState(VK_MENU)&0x8000)!=0);
    out<<" buttons="<<((GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0)<<','<<((GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0)
        <<" mouse_sent="<<s.mouse_sent<<" mouse_blocked="<<s.mouse_blocked<<" raw_sent="<<s.raw_sent<<" raw_blocked="<<s.raw_blocked
        <<" last_mouse="<<s.last_mouse<<" last_blocked="<<s.last_mouse_blocked;
    if(GetCursorInfo(&cursor))out<<" cursor_flags="<<cursor.flags;
    if(GetClipCursor(&clip))out<<" clip="<<clip.left<<','<<clip.top<<','<<clip.right<<','<<clip.bottom;
    return out.str();
}
}
