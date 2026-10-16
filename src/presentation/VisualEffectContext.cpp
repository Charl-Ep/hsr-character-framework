#include <hksr/framework/presentation/VisualEffectContext.hpp>
#include <stdexcept>

namespace hksr::framework {
namespace {
struct Operation {unsigned& count;explicit Operation(unsigned& c):count(c){++count;}~Operation(){--count;}};
}
VisualEffectContext::VisualEffectContext(std::shared_ptr<const api::PresentationEffects> services):services_(std::move(services)) {
    if(!services_)throw std::invalid_argument("visual effect services required");
}
VisualEffectId VisualEffectContext::Start(const std::string& key,void* entity,void* path) {
    if(closing_)throw std::logic_error("visual context closing");
    if(key.empty() || !entity || !path)throw std::invalid_argument("visual effect input absent");
    Operation operation(operations_);
    const auto id=++next_;
    auto request=services_->CreateRequest("visual effect request="+std::to_string(id)+" key="+key+" ");
    auto* live=request.get();
    requests_.emplace(id,Entry{key,std::move(request)});
    // Register before a potentially synchronous load/completion. End called
    // from a callback cancels this same request, not the next action's output.
    live->Start(entity,path);
    return id;
}
void VisualEffectContext::Remove(const std::string& key,bool fade) {
    for(auto& item:requests_)if(item.second.key==key)item.second.request->Cancel(fade);
}
void VisualEffectContext::End(bool fade) {
    closing_=true;
    for(auto& item:requests_)item.second.request->Cancel(fade);
}
void VisualEffectContext::Service() {
    if(servicing_ || operations_)return;
    servicing_=true;struct Exit{bool& flag;~Exit(){flag=false;}} exit{servicing_};
    // New requests created reentrantly are serviced at the next boundary.
    const auto last=next_;
    for(auto it=requests_.begin();it!=requests_.end() && it->first<=last;) {
        it->second.request->Service();
        if(it->second.request->Status().closed)it=requests_.erase(it);else ++it;
    }
}
const api::EffectRequestStatus* VisualEffectContext::Status(VisualEffectId id) const {
    const auto it=requests_.find(id);return it==requests_.end()?nullptr:&it->second.request->Status();
}
std::vector<VisualEffectId> VisualEffectContext::Matching(const std::string& key) const {
    std::vector<VisualEffectId> out;if(closing_)return out;
    for(const auto& item:requests_) {
        const auto& status=item.second.request->Status();
        if(item.second.key==key && status.submitted && !status.request_ended && !status.closed)out.push_back(item.first);
    }
    return out;
}
std::vector<VisualEffectResult> VisualEffectContext::SetProgress(const std::string& key,float value,float duration) {
    Operation operation(operations_);std::vector<VisualEffectResult> out;
    for(const auto id:Matching(key)) {
        if(closing_)break;
        out.push_back({id,services_->SetProgress(requests_.at(id).request->Status().reference,value,duration)});
    }
    return out;
}
std::vector<VisualEffectResult> VisualEffectContext::SetAnimatorState(const std::string& key,int hash,float duration,bool restart) {
    Operation operation(operations_);std::vector<VisualEffectResult> out;
    for(const auto id:Matching(key)) {
        if(closing_)break;
        out.push_back({id,services_->SetAnimatorState(requests_.at(id).request->Status().reference,hash,duration,restart)});
    }
    return out;
}
}
