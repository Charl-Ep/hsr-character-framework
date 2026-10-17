#include <hksr/framework/host/FrameworkHost.hpp>
#include <hksr/framework/presentation/PresentationProfileFiles.hpp>
#include <hksr/runtime/RuntimeAvailability.hpp>
#include <hksr/framework/http/HttpControl.hpp>
#include "../ui/Overlay.hpp"
#include "../ui/PresentHook.hpp"
#include "DiagnosticQueue.hpp"
#include "StopDrain.hpp"
#ifdef HKSR_RESEARCH
#include "../development/ObservationBridge.hpp"
#include "../../tools/DevelopmentControl.hpp"
#include "../../tools/VisualProgramInputs.hpp"
#endif
#include <Windows.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <share.h>
#include <cstring>

namespace {
struct Extension {
    hksr::framework::detail::DiagnosticQueue diagnostics;
    std::uint64_t reported_log_loss=0;
    FILE* log=nullptr;
    HANDLE wake=nullptr;
    std::atomic<bool> stop{false},done{false};
    std::shared_ptr<hksr::framework::FrameworkHost> framework;
    hksr::api::RuntimeSubscription runtime;
    std::unique_ptr<hksr::framework::HttpControl> http;
    std::shared_ptr<hksr::framework::LayoutIo> layouts;
    std::filesystem::path http_settings;
    std::optional<std::filesystem::file_time_type> http_modified;
    bool http_checked=false;
    std::string http_error;
    void ReadHttpSettings() {
        if(!framework)return;
        std::error_code error;const auto time=std::filesystem::last_write_time(http_settings,error);
        const auto modified=error?std::optional<std::filesystem::file_time_type>{}:time;
        if(!http_checked || http_modified!=modified) {
            http_checked=true;http_modified=modified;
            // Revoking/changing access stops old connections first. Invalid
            // configuration leaves HTTP disabled, never stale credentials live.
            if(http)http->Stop();
            try {
                const auto settings=hksr::framework::ReadHttpSettings(http_settings);
                if(!http)http=std::make_unique<hksr::framework::HttpControl>(framework->Application());
                http->Start(settings);http_error.clear();
                if(settings.enabled)Log("HTTP listening address="+settings.address+" port="+std::to_string(settings.port));
                else Log("HTTP disabled");
            } catch(const std::exception& e) {http_error=e.what();Log("HTTP: "+http_error);}
        }
        if(http) {
            const auto status=http->Status();
            if(!status.error.empty() && status.error!=http_error){http_error=status.error;Log("HTTP: "+http_error);}
        }
    }
#ifdef HKSR_RESEARCH
    std::unique_ptr<hksr::research::DevelopmentControl> control;
    std::shared_ptr<hksr::research::VisualProgramOverrides> visual_inputs=std::make_shared<hksr::research::VisualProgramOverrides>();
    std::filesystem::path control_requests;
    std::filesystem::file_time_type control_modified{};
    void ReadControlRequests() {
        if(!framework)return;
        std::error_code timing_error;
        framework->SetOperationTracing(std::filesystem::exists(http_settings.parent_path()/L"ENABLE_OPERATION_TIMING",timing_error) && !timing_error);
        if(!control)control=std::make_unique<hksr::research::DevelopmentControl>(GetCurrentProcessId(),
            hksr::research::DevelopmentControl::Services{
                [this](auto command,auto reply){return framework->Application().Post(std::move(command),std::move(reply));},
                [this]{return framework->Application().Snapshot();},
                [this](auto character,auto inputs){visual_inputs->Set(character,std::move(inputs));},
                [this](auto character,auto signal){visual_inputs->Push(character,std::move(signal));}});
        std::error_code error;const auto modified=std::filesystem::last_write_time(control_requests,error);
        if(!error && modified!=control_modified) {
            std::ifstream input(control_requests,std::ios::binary);
            if(input){control->Submit(std::string((std::istreambuf_iterator<char>(input)),{}));control_modified=modified;}
        }
        for(const auto& reply:control->Drain())Log("development-control "+reply);
    }
#endif
    hksr::framework::ui::Overlay menu;
    void Log(const std::string& text) {
        diagnostics.Push(GetTickCount64(),GetCurrentThreadId(),text);
    }
    void FlushLog() {
        diagnostics.Pump([this](const auto& batch) {
            for(const auto& record:batch) {
                if(log) {
                    if(fprintf(log,"[%llu thread=%lu] %s\n",record.time,record.thread,record.text.c_str())<0)
                        throw std::runtime_error("diagnostic write failed");
                }else OutputDebugStringA(record.text.c_str());
            }
            if(log && fflush(log)!=0)throw std::runtime_error("diagnostic flush failed");
        });
        const auto lost=diagnostics.Lost();
        if(lost!=reported_log_loss) {
            reported_log_loss=lost;
            const auto text="Framework diagnostic delivery uncertain/lost records="+std::to_string(lost)+"\n";
            OutputDebugStringA(text.c_str()); // worker only; never silently claim delivery
            if(log){fprintf(log,"%s",text.c_str());fflush(log);}
        }
    }
};
// Pinned lifetime: no worker joins, UI destruction or hook removal in detach.
Extension& State(){static auto* value=new Extension;return *value;}
void Run() {
    auto& s=State();using namespace hksr::api;
    bool started=false,menu_attempted=false;std::string last_frame_error,preferences_error;
    auto availability=RuntimeAvailability::WaitingModules;bool reported=false;
    auto phase=RuntimeHostPhase::Starting;
    while(!s.stop) {
        const auto preference=s.menu.FlushPreferences();
        if(preference!=preferences_error){preferences_error=preference;if(!preference.empty())s.Log(preference);}
#ifdef HKSR_RESEARCH
        s.ReadControlRequests();
#endif
        if(!started) {
            const auto observed=ObserveRuntimeAvailability();
            if(!reported || availability!=observed.state) {
                reported=true;availability=observed.state;
                s.Log("bootstrap observation="+std::to_string(static_cast<int>(observed.state))+" "+observed.error);
            }
            if(observed.state==RuntimeAvailability::Unsupported) return;
            if(observed.state==RuntimeAvailability::Available) {
                s.Log("official initialization critical section completed; starting SDK worker");
                s.framework=std::make_shared<hksr::framework::FrameworkHost>([&s](const std::string& text){s.Log(text);});
                s.framework->SetLayoutIo(s.layouts);
                auto profiles=std::make_shared<hksr::framework::PresentationProfiles>(hksr::framework::ReadPresentationProfiles(s.http_settings.parent_path()/L"PresentationProfiles"));
                for(const auto& error:profiles->errors)s.Log("presentation profile: "+error);
                s.Log("presentation profiles loaded="+std::to_string(profiles->sources.size()));
                s.framework->SetPresentationProfiles(std::move(profiles));
                std::shared_ptr<RuntimeClient> client=s.framework;
#ifdef HKSR_RESEARCH
                try {
                    auto programs=hksr::research::ReadVisualProgramInputs(s.http_settings.parent_path()/L"VISUAL_PROGRAMS.json");
                    s.Log("visual programs loaded="+std::to_string(programs.size()));
                    s.framework->SetVisualPrograms(hksr::research::BindVisualProgramOverrides(std::move(programs),s.visual_inputs));
                } catch(const std::exception& e){s.Log(std::string("visual programs unavailable: ")+e.what());}
                s.framework->SetModelObserver([](auto id,auto generation,auto domain,auto* entity,void* model,void* animator) {
                    hksr::framework::development::ObserveModel(id,generation,static_cast<int>(domain),entity,model,animator);
                    });
#endif
                s.runtime=RegisterRuntimeClient(std::move(client));started=true;
                s.Log("menu waiting for first official world frame; no bootstrap graphics device created");
            }
        }
        if(started) {
            s.layouts->Pump();
            s.ReadHttpSettings(); // bootstrap worker only; no network/file IO on official frame
            const auto status=s.runtime.Status();
            // Runtime availability and a prepared subscription can precede
            // Unity's first graphics device. Do not create our probe swapchain
            // concurrently with that initialization (including proxy hooks).
            // A delivered world frame is the host lifecycle boundary, not a
            // guessed startup delay. SDK/character service never waits for UI.
            if(!menu_attempted && !s.stop && status.phase==RuntimeHostPhase::Running && status.frames!=0) {
                menu_attempted=true;
                s.Log("menu bootstrap after official world frames="+std::to_string(status.frames));
                try {
                    hksr::framework::ui::StartPresentHook([&s,last=std::string()](IDXGISwapChain* chain) mutable {
                        s.menu.Draw(chain,*s.framework);
                        const auto error=s.menu.Error();
                        if(error!=last){last=error;s.Log("menu: "+error);}
                    },[&s](const std::string& text){s.Log(text);});
                    s.Log("independent DXGI menu callback installed");
                } catch(const std::exception& e) {
                    hksr::framework::ui::StopPresentHook();
                    s.Log(std::string("menu unavailable: ")+e.what());
                }
            }
            if(status.phase!=phase) {
                phase=status.phase;s.Log("SDK host phase="+std::to_string(static_cast<int>(phase)));
                if(!status.preparation_error.empty())s.Log(status.preparation_error);
                for(const auto& error:ReadRuntimeHostStatus().startup_errors)s.Log(error);
                if(phase==RuntimeHostPhase::Running)
                    for(const auto& error:s.framework->Application().Snapshot().binding_errors)s.Log("binding: "+error);
            }
            if(status.last_frame_error!=last_frame_error) {
                last_frame_error=status.last_frame_error;if(!last_frame_error.empty())s.Log("frame: "+last_frame_error);
            }
            if(phase==RuntimeHostPhase::Stopped || phase==RuntimeHostPhase::Failed) return;
        }
        s.FlushLog();WaitForSingleObject(s.wake,250);
    }
}
DWORD WINAPI Bootstrap(void* module) noexcept {
    auto& s=State();
    try {
        HMODULE self=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&Bootstrap),&self)) throw std::runtime_error("cannot pin Framework module");
        std::wstring path(32768,L'\0');
        const auto length=GetModuleFileNameW(static_cast<HMODULE>(module),path.data(),static_cast<DWORD>(path.size()));
        if(!length || length>=path.size()) throw std::runtime_error("cannot resolve Framework path");
        path.resize(length);
        // Use .cfg so SRMI's recursive mod-INI loader cannot interpret UI settings.
        s.menu.ConfigurePreferences(std::filesystem::path(path).parent_path()/L"ui-settings.cfg");
        s.http_settings=std::filesystem::path(path).parent_path()/L"http-settings.cfg";
        s.layouts=std::make_shared<hksr::framework::LayoutIo>(std::filesystem::path(path).parent_path()/L"Layouts");
#ifdef HKSR_RESEARCH
        s.control_requests=std::filesystem::path(path).parent_path()/L"DEVELOPMENT_COMMANDS.json";
#endif
        const auto log=std::filesystem::path(path).parent_path()/L"framework-runtime.log";
        s.log=_wfsopen(log.c_str(),L"w",_SH_DENYNO);
        if(!s.log) throw std::runtime_error("cannot open framework-runtime.log");
        s.Log("Standalone Framework process="+std::to_string(GetCurrentProcessId())+"; waiting for SDK inputs without Present");
#ifdef HKSR_RESEARCH
        s.Log("build=research; runtime inspection=independent DLL; development control enabled");
#else
        s.Log("build=runtime; runtime inspection not compiled");
#endif
        Run();
        const auto preference=s.menu.FlushPreferences();if(!preference.empty())s.Log(preference);
    } catch(const std::exception& e) {s.Log(std::string("Framework bootstrap: ")+e.what());}
    catch(...) {s.Log("Framework bootstrap: unknown C++ failure");}
    if(s.http)s.http->Stop();
    hksr::framework::ui::StopPresentHook();s.menu.Stop();
    s.runtime.RequestStop();
    // RequestStop is asynchronous. Keep the writer alive until the producer's
    // last official frame has closed, then drain all accepted layout writes.
    ResetEvent(s.wake);
    if(s.layouts) {
        s.Log("stopping: waiting for official client closure and layout drain");
        while(!hksr::framework::detail::PumpStoppingLayouts(s.runtime.Status().phase,*s.layouts)) {
            s.FlushLog();WaitForSingleObject(s.wake,250);ResetEvent(s.wake);
        }
        for(const auto& result:s.layouts->Take())if(!result.error.empty())s.Log("layout shutdown: "+result.error);
        const auto status=s.layouts->Status();
        s.Log("layout drain completed="+std::to_string(status.completed)+" failed="+std::to_string(status.failed));
    }
    s.Log("Framework worker stopped");s.FlushLog();
    s.done=true;return 0;
}
}
extern "C" __declspec(dllexport) void __cdecl HksrFramework_RequestStop() {
    auto& s=State();s.stop=true;if(s.wake)SetEvent(s.wake);
}
extern "C" __declspec(dllexport) unsigned __cdecl HksrFramework_WorkerStopped() {return State().done?1u:0u;}
#ifdef HKSR_RESEARCH
extern "C" __declspec(dllexport) std::size_t __cdecl HksrFramework_ReadDevelopmentInputV1(char* output,std::size_t capacity) {
    try {
        const auto text=State().menu.InputState();
        if(output && capacity>=text.size())std::memcpy(output,text.data(),text.size());
        return text.size();
    }catch(...){return 0;}
}
#endif
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void*) {
    if(reason!=DLL_PROCESS_ATTACH)return TRUE;
    auto& s=State();s.wake=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!s.wake)return FALSE;
    const auto worker=CreateThread(nullptr,0,Bootstrap,module,0,nullptr);
    if(!worker){CloseHandle(s.wake);s.wake=nullptr;return FALSE;}
    CloseHandle(worker);return TRUE;
}
