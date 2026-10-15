#include <hksr/framework/character/SceneIdentity.hpp>
#include <algorithm>
#include <stdexcept>

namespace hksr::framework {
void SceneIdentity::Drain() {
    for(auto it=owned_.begin();it!=owned_.end();) {
        if(std::find(active_.begin(),active_.end(),*it)!=active_.end()){++it;continue;}
        if(roots_->Release(*it).complete){roots_->Forget(*it);it=owned_.erase(it);}else ++it;
    }
}
void SceneIdentity::ReleaseActive(){active_={};address_.reset();Drain();}
void SceneIdentity::Close(){closing_=true;ReleaseActive();}
std::optional<SceneAddress> SceneIdentity::Observe(const api::WorldFrame& frame,const api::WorldMapSnapshot& map) {
    error_.clear();Drain();
    if(closing_)return {};
    // A battle-world callback is not a departure from the exploration map.
    // Keep its rooted identity; only exploration observations replace it.
    if(!frame.world || !frame.battle.read.returned || frame.battle.value || !frame.disposing.read.returned)return {};
    if(frame.disposing.value) {
        if(active_[0]) {
            const auto old=roots_->Target(active_[0]);
            if(old.call.returned && old.object==frame.world)ReleaseActive();
        }
        return {};
    }
    // Read failure is not a null map. A confirmed missing link invalidates the
    // active identity; a transient getter failure only withholds this sample.
    if(!frame.adventure_phase.read.returned)return {};
    if(!frame.adventure_phase.value){ReleaseActive();return {};}
    if(!map.group_manager.read.returned)return {};
    if(!map.group_manager.value){ReleaseActive();return {};}
    if(!map.map.read.returned)return {};
    if(!map.map.value){ReleaseActive();return {};}
    for(const auto& part:map.identity)if(!part.read.returned){error_="map key has an unread field";return {};}
    std::string key="hsr-map-v1";
    for(const auto& part:map.identity)key+=":"+std::to_string(part.value);
    const std::array<void*,4> objects{frame.world,frame.adventure_phase.value,map.group_manager.value,map.map.value};
    bool same=address_.has_value();
    for(std::size_t i=0;same && i<active_.size();++i){
        const auto target=roots_->Target(active_[i]);
        if(!target.call.returned){error_="map identity root read failed";return {};}
        same=target.object==objects[i];
    }
    if(same && address_->key==key)return address_;
    std::array<api::ManagedRootId,4> next{};
    // Retain replacement before releasing old identities, so allocator address
    // reuse cannot masquerade as an unchanged object during this handoff.
    try {
        owned_.reserve(owned_.size()+next.size());
        for(std::size_t i=0;i<next.size();++i){
            next[i]=roots_->Keep(objects[i]);owned_.push_back(next[i]);
            if(!roots_->Status(next[i]).ready)throw std::runtime_error("map identity retain failed");
        }
    } catch(const std::exception& error){error_=error.what();Drain();return {};}
    active_=next;address_=SceneAddress{std::move(key),++sequence_};Drain();
    return address_;
}
}
