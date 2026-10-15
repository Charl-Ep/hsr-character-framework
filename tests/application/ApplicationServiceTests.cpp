#include <hksr/framework/application/ApplicationService.hpp>
#include <atomic>
#include <thread>
#include <stdexcept>

namespace {
void Check(bool value) {if(!value)throw std::runtime_error("application service test failed");}
}
void TestApplicationService() {
    using namespace hksr::framework;
    ApplicationService service;
    // Producers share ordering, but a callback consumer never consumes menu
    // replies. The service does not need a RuntimeHost or engine bindings.
    std::atomic<unsigned> callbacks{0};
    std::thread first([&]{for(unsigned i=0;i<20;++i)service.Post(RequestCatalog{});});
    std::thread second([&]{for(unsigned i=0;i<20;++i)service.Post(RequestCatalog{},[&](const auto& reply){
        Check(reply.accepted);++callbacks;(void)service.Snapshot();
    });});
    first.join();second.join();
    auto batch=service.TakeCommands(false);Check(batch.size()==40);
    std::uint64_t previous=0;
    for(const auto& pending:batch) {
        Check(pending.serial==++previous);
        service.Deliver(pending,{pending.serial,0,true,{}});
    }
    Check(callbacks==20 && service.TakeReplies().size()==20 && service.TakeReplies().empty());
    Check(service.TakeCommands(false).empty());

    ApplicationSnapshot current;current.prepared=true;current.characters.resize(1);current.characters[0].id=7;
    service.Publish(current);const auto old=service.Snapshot();
    current.characters[0].id=8;service.Publish(current);
    Check(old.characters[0].id==7 && service.Snapshot().characters[0].id==8);
    // Callback posts belong to the next batch, not the batch being delivered.
    service.Post(RequestCatalog{},[&](const auto&){service.Post(RequestCatalog{});});
    batch=service.TakeCommands(false);Check(batch.size()==1);
    service.Deliver(batch.front(),{batch.front().serial,0,true,{}});
    auto next=service.TakeCommands(false);Check(next.size()==1 && next.front().serial==batch.front().serial+1);
    service.Deliver(next.front(),{next.front().serial,0,true,{}});service.TakeReplies();

    service.Post(RequestCatalog{},[](const auto&){throw std::runtime_error("reply consumer fault");});
    service.Post(RequestCatalog{});batch=service.TakeCommands(false);
    bool threw=false;try{service.Deliver(batch.front(),{batch.front().serial,0,true,{}});}catch(const std::runtime_error&){threw=true;}
    Check(threw);service.Deliver(batch.back(),{batch.back().serial,0,true,{}});
    Check(service.TakeReplies().size()==1 && service.TakeCommands(false).empty()); // no implicit retry

    const auto pending=service.Post(RequestCatalog{});
    batch=service.TakeCommands(true);Check(batch.size()==1 && batch.front().serial==pending);
    Check(service.Snapshot().closing);
    bool rejected=false;try{service.Post(RequestCatalog{});}catch(const std::logic_error&){rejected=true;}
    Check(rejected);
    service.Deliver(batch.front(),{pending,0,false,"shutdown"});Check(!service.TakeReplies().front().accepted);
    // A stale publication cannot reopen the queue after shutdown.
    service.Publish(current);Check(service.Snapshot().closing);
    rejected=false;try{service.Post(RequestCatalog{});}catch(const std::logic_error&){rejected=true;}
    Check(rejected && service.TakeCommands(false).empty());
}
