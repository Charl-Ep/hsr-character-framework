#include "../../src/ui/Overlay.hpp"
#include "../../src/ui/PresentHook.hpp"
#include <Windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <future>
#include <atomic>
using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
namespace {
void Check(bool value,const char* why) {if(!value)throw std::runtime_error(why);}
unsigned forwarded=0;
D3D_DRIVER_TYPE driver=D3D_DRIVER_TYPE_WARP;
LRESULT CALLBACK Window(HWND w,UINT m,WPARAM a,LPARAM b) {if(m==WM_APP+1)++forwarded;return DefWindowProcW(w,m,a,b);}
struct Device {
    ComPtr<IDXGISwapChain> chain;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
};
Device Create(decltype(&D3D11CreateDeviceAndSwapChain) create,HWND window,DXGI_SWAP_EFFECT effect=DXGI_SWAP_EFFECT_DISCARD) {
    DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=900;desc.BufferDesc.Height=750;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.OutputWindow=window;desc.Windowed=TRUE;
    desc.SwapEffect=effect;
    Device out;
    Check(SUCCEEDED(create(nullptr,driver,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&out.chain,&out.device,nullptr,&out.context)),"create D3D11 chain");
    return out;
}
std::size_t Colored(IDXGISwapChain* chain) {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<ID3D11Texture2D> back,read;
    Check(SUCCEEDED(chain->GetDevice(IID_PPV_ARGS(&device))),"readback device");device->GetImmediateContext(&context);
    Check(SUCCEEDED(chain->GetBuffer(0,IID_PPV_ARGS(&back))),"readback back buffer");
    D3D11_TEXTURE2D_DESC desc{};back->GetDesc(&desc);desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    Check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&read)),"readback texture");context->CopyResource(read.Get(),back.Get());
    D3D11_MAPPED_SUBRESOURCE map{};Check(SUCCEEDED(context->Map(read.Get(),0,D3D11_MAP_READ,0,&map)),"map readback");
    std::size_t n=0;
    for(UINT y=0;y<desc.Height;++y)for(UINT x=0;x<desc.Width;++x) {
        auto* p=static_cast<const unsigned char*>(map.pData)+y*map.RowPitch+4*x;if(p[0]||p[1]||p[2])++n;
    }
    context->Unmap(read.Get(),0);return n;
}
void Clear(Device& d) {
    ComPtr<ID3D11Texture2D> back;ComPtr<ID3D11RenderTargetView> view;
    Check(SUCCEEDED(d.chain->GetBuffer(0,IID_PPV_ARGS(&back))),"clear back buffer");
    Check(SUCCEEDED(d.device->CreateRenderTargetView(back.Get(),nullptr,&view)),"clear target");
    const float black[4]{};d.context->ClearRenderTargetView(view.Get(),black);
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
        // Optional exact official package DLL. The test generates a separate
        // .build config; it never edits or starts the developer/game runtime.
        auto create=&D3D11CreateDeviceAndSwapChain;
        const bool stock=argc>1 && std::wstring(argv[1])!=L"--hardware";
        const bool concurrent=argc>3 && std::wstring(argv[3])==L"--startup-race";
        const bool after_frame=argc>3 && std::wstring(argv[3])==L"--after-host-frame";
        if((argc>1 && std::wstring(argv[1])==L"--hardware") || (argc>2 && std::wstring(argv[2])==L"--hardware"))driver=D3D_DRIVER_TYPE_HARDWARE;
        if(stock) {
            const auto root=fs::absolute(L".build/stock-xxmi-menu-test");fs::create_directories(root);
            fs::copy_file(argv[1],root/L"d3d11.dll",fs::copy_options::overwrite_existing);
            std::ofstream ini(root/L"d3dx.ini");
            ini<<"[Loader]\ntarget=CharacterPresentTests.exe\n[System]\nconfig_initialization_delay=0\nallow_platform_update=1\n"
                "[Logging]\ncalls=0\n[Hunting]\nhunting=0\n[Rendering]\ncache_shaders=0\n"
                "[Stereo]\nautomatic_mode=0\nforce_no_nvapi=1\n[Present]\n";ini.close();
            const auto dll=LoadLibraryW((root/L"d3d11.dll").c_str());Check(dll!=nullptr,"stock XXMI load");
            create=reinterpret_cast<decltype(create)>(GetProcAddress(dll,"D3D11CreateDeviceAndSwapChain"));Check(create!=nullptr,"stock XXMI export");
        }
        WNDCLASSW wc{};wc.lpfnWndProc=Window;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"HKSRPresentTest";
        Check(RegisterClassW(&wc)!=0,"window class");
        auto window=[&](){const auto w=CreateWindowW(wc.lpszClassName,L"hidden",WS_OVERLAPPEDWINDOW,0,0,900,750,nullptr,nullptr,wc.hInstance,nullptr);Check(w!=nullptr,"window");return w;};
        const auto main=window(),aux=window();
        if(concurrent || after_frame) {
            // Explicit diagnostic mode: race the old worker bootstrap against
            // a host's first hardware chain. Never used by the normal suite.
            std::promise<void> ready,go;auto begin=go.get_future().share();
            std::exception_ptr error;std::atomic<unsigned> observed{0};
            std::thread bootstrap([&]{
                ready.set_value();begin.wait();
                try {hksr::framework::ui::StartPresentHook([&](IDXGISwapChain*){++observed;},[](const auto&){});}
                catch(...) {error=std::current_exception();}
            });
            ready.get_future().wait();if(concurrent)go.set_value();
            Device first;
            try {
                first=Create(create,main);
                if(after_frame)Check(SUCCEEDED(first.chain->Present(0,0)),"host's first Present");
            }catch(...) {if(after_frame)go.set_value();bootstrap.join();throw;}
            if(after_frame)go.set_value();
            bootstrap.join();if(error)std::rethrow_exception(error);
            Check(SUCCEEDED(first.chain->Present(0,0)),"startup-race Present");
            Check(observed==1,"startup-race callback missing");
            hksr::framework::ui::StopPresentHook();
            std::cout<<(concurrent?"Concurrent first-chain/bootstrap attempt passed; does not disprove race.\n":
                "Worker bootstrap after host first frame passed; callback reached on next Present.\n");
            return 0;
        }
        auto before=Create(create,main); // injection after existing swap chain
        hksr::framework::FrameworkHost host;hksr::framework::ui::Overlay menu;
        unsigned calls=0,reports=0;bool nested=false,fail=false;std::size_t colored=0;std::string callback_error;
        hksr::framework::ui::StartPresentHook([&](IDXGISwapChain* chain){
            ++calls;
            if(fail)throw std::runtime_error("injected UI failure");
            if(nested) {nested=false;chain->Present(0,DXGI_PRESENT_TEST);}
            try {menu.Draw(chain,host);if(!menu.Error().empty())throw std::runtime_error(menu.Error());colored=Colored(chain);}
            catch(const std::exception& e){callback_error=e.what();}
        },[&](const std::string& text){if(text!="first independent DXGI frame observed")++reports;});
        Clear(before);nested=true;Check(SUCCEEDED(before.chain->Present(0,0)),"Present failed");
        Check(callback_error.empty(),callback_error.c_str());Check(calls==1,"Present delivered multiple callbacks");
        Check(colored>1000,"menu not actually drawn through real Present");
        before.chain->Present(0,DXGI_PRESENT_TEST);Check(calls==1,"TEST frame drew UI");
        ComPtr<IDXGISwapChain1> chain1;Check(SUCCEEDED(before.chain.As(&chain1)),"Present1 interface");
        DXGI_PRESENT_PARAMETERS full{};Check(SUCCEEDED(chain1->Present1(0,0,&full)),"Present1 return failed");Check(calls==2,"Present1 missing or double callback");
        RECT dirty{0,0,1,1};DXGI_PRESENT_PARAMETERS partial{1,&dirty,nullptr,nullptr};
        chain1->Present1(0,0,&partial);Check(calls==2 && reports==1,"partial region incorrectly drawn or not reported");
        Check(SUCCEEDED(before.chain->ResizeBuffers(2,800,600,DXGI_FORMAT_R8G8B8A8_UNORM,0)),"menu holds backbuffer over resize");
        before.chain->Present(0,0);Check(calls==3 && callback_error.empty(),"resized frame");
        auto second=Create(create,aux); // creation after injection
        Clear(second);second.chain->Present(0,0);Check(calls==4 && colored==0,"menu drew on auxiliary window");
        chain1.Reset();before.context.Reset();before.chain.Reset();before.device.Reset();
        before=Create(create,main);Clear(before);before.chain->Present(0,0);
        Check(calls==5 && colored>1000 && callback_error.empty(),"same-window swapchain/device recreation");
        DestroyWindow(main);
        before.context.Reset();before.chain.Reset();before.device.Reset();
        const auto recreated=window();before=Create(create,recreated);Clear(before);before.chain->Present(0,0);
        Check(calls==6 && colored>1000 && callback_error.empty(),"window recreation");
        fail=true;Check(SUCCEEDED(before.chain->Present(0,0)),"callback exception changed original Present");
        Check(calls==7 && reports==2,"callback exception not reported");
        fail=false;before.context.Reset();before.chain.Reset();before.device.Reset();
        before=Create(create,recreated,DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL);Clear(before);
        Check(SUCCEEDED(before.chain.As(&chain1)),"flip Present1 interface");
        Check(SUCCEEDED(chain1->Present1(0,0,&full)),"flip Present1 failed");
        Check(calls==8 && colored>1000 && callback_error.empty(),"flip Present1 menu");
        hksr::framework::ui::StopPresentHook();menu.Stop();
        before.chain->Present(0,0);Check(calls==8,"stopped callback still running");
        SendMessageW(recreated,WM_APP+1,0,0);Check(forwarded==1,"stopped input not forwarded");
        Check(!GetModuleHandleW(L"GameAssembly.dll") && !GetModuleHandleW(L"UnityPlayer.dll"),"unexpected game modules");
        DestroyWindow(recreated);DestroyWindow(aux);
        std::cout<<"Independent Present tests passed: real UI pixels, pre/post injection chains, Present/Present1, nested/TEST, partial damage diagnosis, resize/device/window recreation, auxiliary window, exception and stop; "<<(stock?"stock XXMI":"system D3D11")<<"; "<<(driver==D3D_DRIVER_TYPE_WARP?"WARP":"hardware")<<"; no game.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
