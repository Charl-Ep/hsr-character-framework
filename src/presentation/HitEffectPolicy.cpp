#include <hksr/framework/presentation/HitEffectPolicy.hpp>
#include <algorithm>
#include <limits>

namespace hksr::framework {
HitEffectPlan SelectHitEffects(const std::string& path,const std::string& animation,
    const std::vector<HitEffectOverride>& overrides) {
    HitEffectPlan out;
    if(path.empty() && animation.empty())return out;
    const HitEffectOverride* highest=nullptr;
    std::vector<const HitEffectOverride*> multiple;
    for(const auto& item:overrides) {
        if(!item.path)continue;
        if(item.stacking==HitEffectStacking::HighestPriority) {
            if(!item.path->empty() && item.priority<std::numeric_limits<int>::max() && (!highest || item.priority<highest->priority))highest=&item;
        }else multiple.push_back(&item);
    }
    bool keep=false;
    auto add=[&](const HitEffectOverride& item,const std::string& selected){
        out.effects.push_back({selected,item.offset,item.replay_on_mutex});
    };
    if(highest){add(*highest,*highest->path);keep=highest->keep_original;}
    // Official multiple entries are sorted by unsigned operation version,
    // newest first. Equal versions retain the supplied order here.
    std::stable_sort(multiple.begin(),multiple.end(),[](auto a,auto b){return a->version>b->version;});
    for(const auto* item:multiple) {
        for(int n=item->death_count;n<item->count;++n)add(*item,*item->path);
        if(!item->death_path.empty())for(int n=0;n<item->death_count;++n)add(*item,item->death_path);
        keep=keep || item->keep_original;
    }
    out.has_overrides=!out.effects.empty();
    // DMOC tests the selected primary path before processing any effect. With
    // keep-original=true, an empty original still ends this whole branch.
    const auto& primary=out.has_overrides && !keep?out.effects.front().path:path;
    if(primary.empty()){out.effects.clear();return out;}
    if(keep || !out.has_overrides)out.effects.push_back({path,{},false});
    out.effects.erase(std::remove_if(out.effects.begin(),out.effects.end(),
        [](const auto& item){return item.path.empty();}),out.effects.end());
    return out;
}
}
