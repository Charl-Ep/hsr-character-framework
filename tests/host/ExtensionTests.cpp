#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
namespace {
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
}
int wmain(int argc,wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    try {
        Check(argc==2 || argc==3,"Expected Framework DLL path and optional runtime/research build flavor");
        const auto path=std::filesystem::absolute(argv[1]);
        const auto api=LoadLibraryW((path.parent_path()/L"Hksr.Api.dll").c_str());Check(api!=nullptr,"load SDK");
        const auto module=LoadLibraryW(path.c_str());Check(module!=nullptr,"load Framework");
        Check(!GetProcAddress(module,"XXMI_QueryExtension"),"Framework still exports XXMI ABI");
        auto stop=reinterpret_cast<void(__cdecl*)()>(GetProcAddress(module,"HksrFramework_RequestStop"));
        auto done=reinterpret_cast<unsigned(__cdecl*)()>(GetProcAddress(module,"HksrFramework_WorkerStopped"));
        Check(stop && done,"standalone lifecycle exports absent");
        bool waiting=false;const auto start=GetTickCount64();
        while(!waiting && GetTickCount64()-start<5000) {
            std::ifstream log(path.parent_path()/L"framework-runtime.log");
            const std::string text((std::istreambuf_iterator<char>(log)),{});
            waiting=text.find("process="+std::to_string(GetCurrentProcessId()))!=std::string::npos && text.find("bootstrap observation=0")!=std::string::npos;
            if(!waiting)Sleep(10);
        }
        Check(waiting && !done(),"autostart without a Present did not reach module waiting");
        if(argc==3) {
            const std::wstring flavor=argv[2];
            Check(flavor==L"runtime" || flavor==L"research","unknown expected build flavor");
            std::ifstream log(path.parent_path()/L"framework-runtime.log");
            const std::string text((std::istreambuf_iterator<char>(log)),{});
            const auto expected=flavor==L"runtime"?"build=runtime; runtime inspection not compiled":"build=research; runtime inspection=";
            Check(text.find(expected)!=std::string::npos,"loaded DLL has unexpected research boundary");
        }
        Check(!GetModuleHandleW(L"GameAssembly.dll") && !GetModuleHandleW(L"UnityPlayer.dll"),"unexpected game modules");
        const auto begin=GetTickCount64();stop();stop();Check(GetTickCount64()-begin<1000,"stop request blocked");
        while(!done() && GetTickCount64()-begin<5000)Sleep(10);
        Check(done()!=0,"worker failed to stop without a final Present");
        FreeLibrary(module);FreeLibrary(api);
        std::cout<<"Framework standalone DLL passed: ordered ordinary LoadLibrary, no XXMI ABI/Present, actual bootstrap log, repeated nonblocking stop without final render; no game.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
