#include <hksr/framework/presentation/HitEffectPolicy.hpp>
#include <limits>
#include <stdexcept>

namespace {
using namespace hksr::framework;
void CheckAt(bool ok,int line){if(!ok)throw std::runtime_error("hit effect policy test line "+std::to_string(line));}
#define Check(...) CheckAt((__VA_ARGS__),__LINE__)
}
void TestHitEffectPolicy() {
    auto plan=SelectHitEffects("source","",{});
    Check(plan.effects.size()==1 && plan.effects[0].path=="source" && !plan.has_overrides);
    HitEffectOverride a;a.path="a";a.priority=4;a.offset=hksr::api::Position3{1,2,3};a.replay_on_mutex=true;
    auto b=a;b.path="b";b.priority=2;
    auto tied=b;tied.path="tie";
    plan=SelectHitEffects("source","Hit",{a,b,tied});
    Check(plan.effects.size()==1 && plan.effects[0].path=="b" && plan.has_overrides);
    Check(plan.effects[0].offset->y==2 && plan.effects[0].replay_on_mutex);
    auto sentinel=a;sentinel.priority=std::numeric_limits<int>::max();
    Check(!SelectHitEffects("source","Hit",{sentinel}).has_overrides);
    Check(SelectHitEffects("","",{a}).effects.empty());
    Check(SelectHitEffects("","Hit",{a}).effects[0].path=="a");
    a.keep_original=true;
    Check(SelectHitEffects("","Hit",{a}).effects.empty());
    plan=SelectHitEffects("source","Hit",{a});
    Check(plan.effects.size()==2 && plan.effects[1].path=="source" && !plan.effects[1].offset);
    auto old=b;old.stacking=HitEffectStacking::Multiple;old.version=1;old.count=3;old.death_count=1;old.death_path="death";
    auto recent=old;recent.path="new";recent.count=1;recent.death_count=0;recent.version=9;
    plan=SelectHitEffects("source","Hit",{a,old,recent});
    Check(plan.effects.size()==6 && plan.effects[0].path=="a" && plan.effects[1].path=="new");
    Check(plan.effects[2].path=="b" && plan.effects[3].path=="b" && plan.effects[4].path=="death" && plan.effects[5].path=="source");
    old.count=old.death_count=1;old.death_path.clear();
    plan=SelectHitEffects("source","Hit",{old});Check(!plan.has_overrides && plan.effects.size()==1);
    a.count=0;a.path.reset();plan=SelectHitEffects("source","Hit",{a});Check(!plan.has_overrides);
    // Death-source projections can outnumber the remaining live count. This
    // is not a policy rejection: each source list retains its own multiplicity.
    old.count=1;old.death_count=2;old.death_path="death";
    plan=SelectHitEffects("source","Hit",{old});Check(plan.effects.size()==2 && plan.effects[0].path=="death");
}
