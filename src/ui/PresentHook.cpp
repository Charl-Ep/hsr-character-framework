#include "PresentHook.hpp"
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <mutex>
#include <stdexcept>

namespace hksr::framework::ui {
namespace {
using Microsoft::WRL::ComPtr;
using Present=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
using Present1=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*,UINT,UINT,const DXGI_PRESENT_PARAMETERS*);
struct State {
    std::mutex mutex;
    std::function<void(IDXGISwapChain*)> draw;
    std::function<void(const std::string&)> report;
    Present original=nullptr;
    Present1 original1=nullptr;
    bool attempted=false,stopped=false,reported_partial=false,reported_exception=false,reported_frame=false;
};
State& Get() {static auto* s=new State;return *s;}
thread_local unsigned depth=0;
struct Depth {Depth(){++depth;} ~Depth(){--depth;}};
void Draw(IDXGISwapChain* chain,UINT flags,bool partial) noexcept {
    // Present1 may forward to Present. Only the outer call delivers a frame.
    if(depth!=1 || (flags&(DXGI_PRESENT_TEST|DXGI_PRESENT_DO_NOT_SEQUENCE))) return;
    auto& s=Get();std::lock_guard<std::mutex> lock(s.mutex);
    if(s.stopped || !s.draw) return;
    try {
        if(partial) {
            if(!s.reported_partial) {
                s.reported_partial=true;
                s.report("UI skipped partial/scroll Present1: damage-region composition is not implemented");
            }
            return; // don't falsely advertise an undrawn full frame to DXGI
        }
        if(!s.reported_frame) {s.reported_frame=true;s.report("first independent DXGI frame observed");}
        s.draw(chain);
    } catch(...) {
        if(!s.reported_exception) {
            s.reported_exception=true;
            try {s.report("UI callback threw; original DXGI presentation preserved");} catch(...) {}
        }
    }
}
HRESULT STDMETHODCALLTYPE OnPresent(IDXGISwapChain* chain,UINT interval,UINT flags) {
    Depth scope;Draw(chain,flags,false);
    // No UI mutex across Present: DXGI may wait for the window message thread.
    return Get().original(chain,interval,flags);
}
HRESULT STDMETHODCALLTYPE OnPresent1(IDXGISwapChain1* chain,UINT interval,UINT flags,const DXGI_PRESENT_PARAMETERS* p) {
    Depth scope;
    Draw(chain,flags,!p || p->DirtyRectsCount!=0 || p->pScrollRect || p->pScrollOffset);
    return Get().original1(chain,interval,flags,p);
}
void Require(HRESULT result,const char* label) {
    if(FAILED(result)) throw std::runtime_error(std::string(label)+" HRESULT="+std::to_string(static_cast<unsigned long>(result)));
}
void Require(MH_STATUS result,const char* label) {
    if(result!=MH_OK) throw std::runtime_error(std::string(label)+": "+MH_StatusToString(result));
}
struct ProbeWindow {
    HWND window=nullptr;
    ProbeWindow() {
        window=CreateWindowExW(0,L"STATIC",L"HKSR DXGI bootstrap",WS_OVERLAPPED,
            0,0,8,8,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window) throw std::runtime_error("DXGI bootstrap window creation failed");
    }
    ~ProbeWindow(){DestroyWindow(window);}
};
}
void StartPresentHook(std::function<void(IDXGISwapChain*)> draw,std::function<void(const std::string&)> report) {
    auto& s=Get();
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        if(s.attempted || s.stopped) throw std::logic_error("Present hook already attempted or stopped");
        s.attempted=true;s.draw=std::move(draw);s.report=std::move(report);
    }
    // Resolve system D3D11, not another mod's proxy. Keep its module reference
    // with the process-resident detours. Probe HWND is never shown.
    wchar_t system[MAX_PATH]{};
    const auto length=GetSystemDirectoryW(system,MAX_PATH);
    if(!length || length>=MAX_PATH) throw std::runtime_error("cannot resolve Windows system directory");
    const auto path=std::wstring(system)+L"\\d3d11.dll";
    const auto module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module) throw std::runtime_error("system D3D11 load failed");
    const auto create=reinterpret_cast<decltype(&D3D11CreateDeviceAndSwapChain)>(GetProcAddress(module,"D3D11CreateDeviceAndSwapChain"));
    if(!create) throw std::runtime_error("system D3D11 create export missing");
    ProbeWindow probe;
    DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=8;desc.BufferDesc.Height=8;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=1;desc.OutputWindow=probe.window;desc.Windowed=TRUE;
    ComPtr<IDXGISwapChain> chain;ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    Require(create(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&chain,&device,nullptr,&context),"DXGI bootstrap device");
    ComPtr<IDXGISwapChain1> chain1;Require(chain.As(&chain1),"DXGI bootstrap Present1");
    // Windows SDK COM order, not game RVAs/hashes.
    auto* target=(*reinterpret_cast<void***>(chain.Get()))[8];
    auto* target1=(*reinterpret_cast<void***>(chain1.Get()))[22];
    if(target==target1) throw std::runtime_error("DXGI Present and Present1 unexpectedly alias");
    const auto init=MH_Initialize();
    if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED) Require(init,"Present MinHook initialize");
    Require(MH_CreateHook(target,reinterpret_cast<void*>(&OnPresent),reinterpret_cast<void**>(&s.original)),"create Present hook");
    Require(MH_CreateHook(target1,reinterpret_cast<void*>(&OnPresent1),reinterpret_cast<void**>(&s.original1)),"create Present1 hook");
    Require(MH_QueueEnableHook(target),"queue Present hook");
    Require(MH_QueueEnableHook(target1),"queue Present1 hook");
    // A partial enable must not free in-flight trampolines from this worker.
    const auto enabled=MH_ApplyQueued();
    if(enabled!=MH_OK) {StopPresentHook();Require(enabled,"enable Present hooks");}
}
void StopPresentHook() noexcept {
    auto& s=Get();std::lock_guard<std::mutex> lock(s.mutex);
    s.stopped=true;s.draw={};s.report={};
}
}
