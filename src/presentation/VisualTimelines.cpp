#include <hksr/framework/presentation/VisualTimelines.hpp>
#include <algorithm>
#include <stdexcept>
#include <sstream>

namespace hksr::framework {
VisualTimelines::VisualTimelines(Factory factory,Trace trace):factory_(std::move(factory)),trace_(std::move(trace)) {
    if(!factory_)throw std::invalid_argument("Timeline factory missing");
}
VisualStepResult VisualTimelines::Execute(const VisualTimeline& input) {
    const auto op=input;
    if(op.key.empty())return {VisualStepState::Failed,"Timeline key missing"};
    if(op.type!=0 || op.release_volume)return {VisualStepState::Failed,"only Common Timeline without task-level volume release is supported"};
    const auto previous=current_.find(op.key);
    if(!op.play) {
        if(previous==current_.end())return {VisualStepState::Failed,"no scene Timeline receipt: "+op.key};
        const auto request=previous->second;request->Cancel();request->Service();
        // Stop acceptance is not a pool return. Retain until Service drains.
        const auto& status=request->Status();
        for(const auto& stop:status.stops)if(stop.exception || (!stop.returned && !status.detail.empty()))
            return {VisualStepState::Failed,status.detail};
        return {VisualStepState::Done,{}};
    }
    if(previous!=current_.end()) {
        previous->second->Cancel();retired_.push_back(previous->second);current_.erase(previous);
    }
    auto request=std::shared_ptr<api::OwnedTimeline>(factory_());
    if(!request)return {VisualStepState::Failed,"Timeline factory returned no request"};
    current_[op.key]=request; // publish before native start can reenter cancellation
    request->Start(op.path,op.wrap_mode);request->Service();
    Report(op.key,*request);
    if(!request->Status().accepted)return {VisualStepState::Failed,request->Status().detail};
    return {VisualStepState::Done,{}};
}
void VisualTimelines::Cancel() {
    for(auto& [key,request]:current_){(void)key;request->Cancel();}
    for(auto& request:retired_)request->Cancel();
}
void VisualTimelines::Service() {
    // Keep receipts stable through native callback reentry.
    std::vector<std::shared_ptr<api::OwnedTimeline>> requests=retired_;
    for(const auto& [key,request]:current_){(void)key;requests.push_back(request);}
    for(const auto& request:requests) {
        request->Service();const auto& detail=request->Status().detail;
        if(!detail.empty() && std::find(issues_.begin(),issues_.end(),detail)==issues_.end())issues_.push_back(detail);
    }
    for(const auto& [key,request]:current_)Report(key,*request);
    for(const auto& request:retired_)Report("retired",*request);
    retired_.erase(std::remove_if(retired_.begin(),retired_.end(),[this](const auto& r){
        if(!r->Status().drained)return false;
        last_trace_.erase(r.get());return true;
    }),retired_.end());
}
void VisualTimelines::Report(const std::string& key,const api::OwnedTimeline& request) {
    if(!trace_)return;
    const auto& value=request.Status();std::ostringstream text;
    text<<"timeline key="<<key<<" playback="<<value.start.playback<<" accepted="<<value.accepted
        <<" cancelled="<<value.cancelled<<" drained="<<value.drained<<" detail="<<value.detail;
    for(std::size_t i=0;i<value.start.references.size();++i) {
        const auto& ref=value.start.references[i];
        text<<" [registry="<<ref.registry<<" record="<<ref.record<<" borrow="<<ref.borrow;
        if(i<value.phases.size())text<<" phase="<<static_cast<int>(value.phases[i]);
        text<<"]";
    }
    auto& previous=last_trace_[&request];
    const auto next=text.str();if(previous!=next){previous=next;trace_(next);}
}
bool VisualTimelines::Drained() const {
    for(const auto& [key,request]:current_){(void)key;if(!request->Status().drained)return false;}
    return retired_.empty();
}
void ConnectVisualTimelines(VisualProgramServices& services,std::shared_ptr<VisualTimelines> timelines) {
    if(!timelines)throw std::invalid_argument("Timeline scene missing");
    services.timeline=[timelines](const VisualTimeline& op){return timelines->Execute(op);};
    services.cancel_timelines=[timelines]{timelines->Cancel();};
    services.timelines_drained=[timelines]{timelines->Service();return timelines->Drained();};
}
}
