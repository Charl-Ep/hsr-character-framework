#include <hksr/framework/presentation/SpatialHitEffects.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace hksr::framework {
struct SpatialHitEffects::Entry {
    HitEffectInput input;
    float at=0;
    std::shared_ptr<SpatialHitStatus> status=std::make_shared<SpatialHitStatus>();
    std::unique_ptr<api::OwnedEffectRequest> request;
};
SpatialHitEffects::SpatialHitEffects(VisualSpatialAnchor point,SpatialHitServices services)
    :point_(point),services_(std::move(services)) {}
SpatialHitEndpoint SpatialHitEffects::Submit(const HitInput& input) {
    if(closing_)throw std::runtime_error("spatial hit scope ended");
    auto entry=std::make_shared<Entry>();
    auto result=SpatialHitEndpoint{entry->status,[entry]{entry->status->cancelled=true;if(entry->request)entry->request->Cancel();}};
    if(!input.effect || input.effect->path.empty()) {
        entry->status->admitted=entry->status->ignored=entry->status->closed=true;return result;
    }
    entry->input=*input.effect;entry->at=time_+std::max(0.f,entry->input.delay);
    entries_.push_back(entry);return result;
}
void SpatialHitEffects::Service(float delta) {
    if(servicing_)return;
    servicing_=true;struct Guard {bool& value;~Guard(){value=false;}} guard{servicing_};
    if(std::isfinite(delta) && delta>0)time_+=delta;
    const auto entries=entries_;
    for(const auto& entry:entries) {
        auto& status=*entry->status;
        if(status.closed)continue;
        if(closing_ || status.cancelled) {
            status.cancelled=true;
            if(entry->request)entry->request->Cancel();else status.closed=true;
        }
        if(!entry->request && !status.closed && time_>=entry->at)try {
            const auto& input=entry->input;
            const auto prior=last_.find(input.path);
            const auto count=std::count_if(entries_.begin(),entries_.end(),[&](const auto& other) {
                return other!=entry && other->input.path==input.path && other->status->submitted && !other->status->closed && !other->status->cancelled;
            });
            if((prior!=last_.end() && time_-prior->second<input.minimum_interval) || (input.maximum_count>0 && count>=input.maximum_count)) {
                status.admitted=status.ignored=status.closed=true;continue;
            }
            entry->request=services_.create();
            if(!entry->request)throw std::runtime_error("spatial effect request unavailable");
            api::SpatialEffectOptions options;options.position=point_.position;options.rotation=point_.rotation;
            // Height/scale offsets and receiver overrides require real victim
            // geometry. Empty-space previews use the authored world point.
            services_.start(*entry->request,input.path,options);
            if(closing_ || status.cancelled)entry->request->Cancel();
            const auto& value=entry->request->Status();
            if(!value.submit.returned || !value.submitted)throw std::runtime_error("spatial hit submission failed: "+value.detail);
            status.submitted=status.admitted=true;last_[input.path]=time_;
        }catch(const std::exception& e) {
            status.error=e.what();if(entry->request)entry->request->Cancel();else status.closed=true;
        }
        if(entry->request) {
            entry->request->Service();const auto& value=entry->request->Status();
            status.closed=value.closed;
        }
    }
    entries_.erase(std::remove_if(entries_.begin(),entries_.end(),[](const auto& entry){return entry->status->closed;}),entries_.end());
}
void SpatialHitEffects::Cancel() {
    closing_=true;
    for(const auto& entry:entries_){entry->status->cancelled=true;if(entry->request)entry->request->Cancel();}
}
bool SpatialHitEffects::Drained() const {return entries_.empty();}
}
