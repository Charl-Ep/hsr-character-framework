#include "../../src/host/DiagnosticQueue.hpp"
#include "../../src/host/StopDrain.hpp"
#include <Windows.h>
#include <future>
#include <iostream>
#include <fstream>

using namespace hksr::framework;
namespace {
void Check(bool v,const char* text){if(!v)throw std::runtime_error(text);}
void Logs() {
    detail::DiagnosticQueue queue;
    queue.Push(10,1,"first");
    std::promise<void> entered,release;auto released=release.get_future();
    auto writer=std::async(std::launch::async,[&]{queue.Pump([&](const auto& batch){
        Check(batch.size()==1 && batch.front().text=="first","batch changed");
        entered.set_value();released.wait();
    });});
    entered.get_future().wait();
    // Deterministic blocked sink: the producer must finish while IO is held.
    auto producer=std::async(std::launch::async,[&]{queue.Push(20,2,"second");queue.Push(30,3,"third");});
    const auto ready=producer.wait_for(std::chrono::seconds(2));
    release.set_value();producer.get();writer.get();
    Check(ready==std::future_status::ready,"frame producer waited for file sink");
    queue.Pump([&](const auto& batch){
        Check(batch.size()==2,"accepted diagnostics lost");
        Check(batch[0].time==20 && batch[0].thread==2 && batch[0].text=="second","originating frame metadata lost");
        Check(batch[1].time==30 && batch[1].thread==3 && batch[1].text=="third","diagnostic order lost");
    });
    Check(queue.Lost()==0,"successful writes marked failed");
    queue.Push(40,4,"fault");queue.Pump([](const auto&){throw std::runtime_error("disk failed");});
    Check(queue.Lost()==1,"sink failure silently discarded");
    queue.Push(50,5,"recovered");queue.Pump([](const auto& batch){Check(batch.size()==1 && batch[0].text=="recovered","failed sink poisoned later writes");});
}
void Stop() {
    namespace fs=std::filesystem;using Phase=hksr::api::RuntimeHostPhase;
    const auto folder=fs::temp_directory_path()/("hksr-stop-drain-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    struct Cleanup {fs::path path;~Cleanup(){std::error_code e;fs::remove_all(path,e);}} cleanup{folder};
    LayoutIo io(folder);auto saved=std::make_shared<SceneLayout>();saved->scene="map";
    Check(!detail::PumpStoppingLayouts(Phase::Stopping,io),"no-frame stop claimed completion");
    Check(!io.Status().sealed,"no-frame stop sealed producer");
    io.Submit({LayoutIoKind::WriteCurrent,"map",{},saved,false,1});io.Seal();
    Check(!io.Status().Drained(),"queued write treated as drained");
    Check(detail::PumpStoppingLayouts(Phase::Stopped,io),"closed client did not drain");
    Check(LayoutStorage(folder).Current("map").has_value() && io.Status().failed==0,"final captured write missing");
    bool rejected=false;try{io.Submit({});}catch(const std::logic_error&){rejected=true;}
    Check(rejected,"sealed producer accepted late work");
    // Final frame precedes the worker; no second stop or final Present needed.
    LayoutIo early(folder);early.Submit({LayoutIoKind::WriteCurrent,"map",{},saved,false,2});early.Seal();
    Check(detail::PumpStoppingLayouts(Phase::Stopped,early) && early.Status().completed==1,"frame-first stop lost write");
    // Write failure is a terminal outcome, NOT a successful save or infinite retry.
    const auto file=folder/"not-directory";{std::ofstream output(file);output<<"file";}
    LayoutIo failing(file);failing.Submit({LayoutIoKind::WriteCurrent,"map",{},saved,false,3});
    Check(detail::PumpStoppingLayouts(Phase::Failed,failing),"failed write prevented closure");
    Check(failing.Status().failed==1 && !failing.Take().front().error.empty(),"failed write reported as saved");
    LayoutIo unprepared(folder);Check(detail::PumpStoppingLayouts(Phase::Stopped,unprepared),"unregistered client cannot stop");
}
}
int main() try {Logs();Stop();std::cout<<"Frame diagnostics and layout stop ordering passed.\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
