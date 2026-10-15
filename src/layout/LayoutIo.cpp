#include <hksr/framework/layout/LayoutIo.hpp>
#include <stdexcept>

namespace hksr::framework {
std::uint64_t LayoutIo::Submit(LayoutIoRequest request) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(status_.sealed)throw std::logic_error("layout producer already sealed");
    // Only trailing automatic writes coalesce. An explicit save/load in between
    // is an ordering boundary and retains the exact snapshot it accepted.
    if(request.kind==LayoutIoKind::WriteCurrent) {
        for(auto it=requests_.rbegin();it!=requests_.rend();++it) {
            if(it->request.kind!=LayoutIoKind::WriteCurrent)break;
            if(it->request.scene==request.scene) {it->request=std::move(request);return it->id;}
        }
    }
    const auto id=++next_;requests_.push_back({id,std::move(request)});return id;
}
std::vector<LayoutIoResult> LayoutIo::Take() {
    std::lock_guard<std::mutex> lock(mutex_);std::vector<LayoutIoResult> out;out.swap(results_);return out;
}
void LayoutIo::Pump() {
    std::deque<Work> work;
    {std::lock_guard<std::mutex> lock(mutex_);work.swap(requests_);status_.active+=work.size();}
    std::vector<LayoutIoResult> completed;
    for(auto& item:work) {
        LayoutIoResult result;result.id=item.id;result.request=std::move(item.request);
        const auto& r=result.request;
        try {
            switch(r.kind) {
            case LayoutIoKind::ReadCurrent:result.layout=storage_.Current(r.scene);break;
            case LayoutIoKind::WriteCurrent:
                if(!r.layout || r.layout->scene!=r.scene)throw std::invalid_argument("current layout/map mismatch");
                storage_.SaveCurrent(*r.layout);break;
            case LayoutIoKind::List:result.names=storage_.Names(r.scene);break;
            case LayoutIoKind::Save:
                if(!r.layout || r.layout->scene!=r.scene)throw std::invalid_argument("named layout/map mismatch");
                storage_.SaveNamed(r.name,*r.layout,r.overwrite);result.names=storage_.Names(r.scene);break;
            case LayoutIoKind::Load:result.layout=storage_.Named(r.scene,r.name);break;
            case LayoutIoKind::Delete:storage_.DeleteNamed(r.scene,r.name);result.names=storage_.Names(r.scene);break;
            }
        }catch(const std::exception& e){result.error=e.what();}
        catch(...){result.error="unknown layout storage failure";}
        completed.push_back(std::move(result));
    }
    {std::lock_guard<std::mutex> lock(mutex_);
        for(auto& result:completed){++status_.completed;if(!result.error.empty())++status_.failed;results_.push_back(std::move(result));}
        status_.active-=work.size();}
}
void LayoutIo::Seal(){std::lock_guard<std::mutex> lock(mutex_);status_.sealed=true;}
LayoutIoStatus LayoutIo::Status() const {
    std::lock_guard<std::mutex> lock(mutex_);auto out=status_;out.pending=requests_.size();return out;
}
}
