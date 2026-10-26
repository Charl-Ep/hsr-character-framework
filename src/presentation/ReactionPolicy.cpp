#include <hksr/framework/presentation/ReactionPolicy.hpp>
#include <hksr/framework/tasks/BattleControl.hpp>
#include <cmath>
#include <stdexcept>

namespace hksr::framework {
namespace {
template<class T> const T& Read(const api::ConfigValue<T>& input,const char* field) {
    if(!input.available)throw std::runtime_error(std::string("receiver input unread: ")+field+" "+input.error);
    return input.value;
}
template<class T>const T& Read(const std::optional<T>& input,const char* field) {
    if(!input)throw std::runtime_error(std::string("receiver input unread: ")+field);
    return *input;
}
const std::string& Read(const api::TextInput& input,const char* field) {
    if(!input.available)throw std::runtime_error(std::string("receiver input unread: ")+field+" "+input.error);
    if(input.value.find('\0')!=std::string::npos)throw std::runtime_error(std::string("invalid receiver name: ")+field);
    return input.value;
}
float Finite(float value,const char* field) {
    if(!std::isfinite(value))throw std::runtime_error(std::string("nonfinite receiver input: ")+field);
    return value;
}
const api::TextInput& Name(const api::VisualReactionInput& row,HitKind kind) {
    switch(kind) {
    case HitKind::Additive:return row.hit;
    case HitKind::Airborne:return row.fly_hit;
    case HitKind::Heavy:return row.hit_h;
    case HitKind::Launch:return row.hit_fly;
    default:throw std::logic_error("no animation for empty hit kind");
    }
}
std::string Name(const ReactionSources& sources,HitKind kind) {
    for(const auto* row:{sources.visual?&sources.visual->reactions:nullptr,sources.phase,&sources.defaults})
        if(row && row->present) {
            const auto& value=Read(Name(*row,kind),"reaction animation");
            if(!value.empty())return value;
        }
    return {};
}
bool Heavy(const std::string& name) {
    return (name.size()>=2 && name.compare(name.size()-2,2,"_H")==0) || name.find("_H_")!=std::string::npos;
}
}
HitControlInputs ResolveHitControl(const BattleControlSnapshot& state,bool behaviors) {
    HitControlInputs out;
    if(state.dominant_source)out.visual_flag=state.dominant_source->flag;
    for(const auto& source:state.sources)for(auto effect:source.hit_effects) {
        effect.version=source.hit_effect_version;out.effects.push_back(std::move(effect));
    }
    if(!behaviors)return out;
    for(const auto& source:state.sources) {
        if(!source.behaviors)throw std::runtime_error("receiver source behavior flags unreviewed: "+source.id);
        for(const auto flag:*source.behaviors) {
            // Native OnHit tests both Endurance and EnduranceEnemyOnly.
            if(flag==12 || flag==14)out.endurance=true;
            else if(flag==21)out.mute_heavy=true;
            else if(flag==22)out.force_heavy=true;
        }
    }
    return out;
}
HitDecision SelectHit(const std::string& animation,const HitReceiverState& receiver,
    const ReactionSources& sources,const api::HitAnimationDefaults& defaults,const std::optional<PreviousHit>& previous) {
    HitDecision out;
    // The upstream source deliberately omits OnHit when HitAnimation is empty.
    if(animation.empty())return out;
    if(animation.find('\0')!=std::string::npos)throw std::invalid_argument("invalid source hit animation");
    const auto* row=sources.visual;
    bool force=receiver.force_heavy,mute=receiver.mute_heavy;
    if(row) {
        const bool row_force=Read(row->force_hit_h,"ForecHitH");
        force=force || row_force;
        if(row_force && Read(row->flag,"Flag")==133)mute=false;
    } else if(sources.phase && sources.phase->present)force=force || Read(sources.phase->force_hit_h,"ForceHitH");
    if(Finite(receiver.height,"height")>0.1f)out.kind=HitKind::Airborne;
    else if(receiver.launch_allowed && receiver.has_launch_motion && !receiver.endurance)out.kind=HitKind::Launch;
    else out.kind=!mute && (force || (!receiver.endurance && Heavy(animation)))?HitKind::Heavy:HitKind::Additive;

    // ca16bc0 is reached by the ordinary Hit branch in OnHitEnter. The air-hit
    // branch calls the category resolver directly, not this occurrence test.
    if(out.kind==HitKind::Additive && row) {
        float threshold=Finite(Read(receiver.repeat_occur_at,"character occurrence repeat threshold"),"character occurrence repeat threshold");
        if(threshold<0)threshold=Finite(Read(row->repeat_occur_at,"visual occurrence repeat threshold"),"visual occurrence repeat threshold");
        if(threshold>=0 && Read(receiver.in_occur,"current occurrence") && Finite(Read(receiver.occur_progress,"occurrence progress"),"occurrence progress")<=threshold) {
            out.kind=HitKind::Heavy;
            // ca16ce0 returns AnimOccur itself here, not the category lookup.
            out.state=Read(row->occur.state,"repeated AnimOccur");out.repeated_occur=true;
        }
    }
    if(!out.repeated_occur)out.state=Name(sources,out.kind);
    if(out.state.empty())throw std::runtime_error("selected receiver reaction has no animation");
    if(previous && previous->frame && previous->state==out.state && out.kind!=HitKind::Launch) {
        const auto interval=Read(defaults.minimum_frame_interval,"HitFrameMiniInterval");
        if(interval<0)throw std::runtime_error("negative hit frame interval");
        out.throttled=receiver.frame>=previous->frame && receiver.frame-previous->frame<=static_cast<std::uint64_t>(interval);
        if(out.throttled)return out;
    }
    if(out.kind==HitKind::Additive || out.kind==HitKind::Airborne) {
        bool override_weight=false;
        if(sources.phase && sources.phase->present) {
            const float weight=Finite(Read(sources.phase->hit_weight,"OverrideHitAnimWeight"),"OverrideHitAnimWeight");
            if(weight>=0){out.weight=weight;override_weight=true;}
        }
        if(!override_weight) {
            const auto& weights=Read(defaults.resilience_weights,"ResilienceHitAdditiveWeights");
            const int index=receiver.endurance?0:Read(receiver.resilience,"Resilience");
            if(index<0 || static_cast<std::size_t>(index)>=weights.size())throw std::runtime_error("receiver resilience has no weight entry");
            out.weight=Finite(weights[index],"resilience weight")*Finite(Read(receiver.entity_weight,"entity hit weight"),"entity hit weight");
            Finite(out.weight,"resolved hit weight");
        }
    }
    return out;
}
HitCommand BuildHitCommand(const HitDecision& decision,const HitPlaybackDefaults& defaults,float start) {
    if(decision.kind==HitKind::None || decision.throttled)throw std::invalid_argument("hit decision does not request playback");
    HitCommand out;
    const bool additive=decision.kind==HitKind::Additive || decision.kind==HitKind::Airborne;
    const float weight=additive?decision.weight:0;
    out.layers={{defaults.additive_layer,additive?decision.state:"",weight,defaults.transition,start},
        {defaults.override_layer,additive?decision.state:"",weight,defaults.transition,start}};
    if(!additive)out.body=HitBody{defaults.visual_layer,decision.state,defaults.transition,start};
    out.expression=defaults.expression;out.expression_transition=defaults.expression_transition;out.blink_source=defaults.blink_source;
    return out;
}
IdleDecision SelectIdle(const api::TextInput& fallback,const api::TextInput& default_expression,
    const api::PhaseVisualConfigSources* phase,const api::VisualBehaviorInput* visual,const api::TextInput* visual_expression) {
    IdleDecision out;out.visual=visual!=nullptr;
    if(visual) {
        out.state=Read(visual->loop.resolved_state,"visual loop");
        if(!visual_expression)throw std::invalid_argument("active visual row requires its global expression input");
        out.expression=Read(*visual_expression,"visual expression");
    }else if(phase)out.state=Read(phase->standby,"phase standby");
    // A present visual row owns its return even when its loop is empty.
    if(!visual && out.state.empty())out.state=Read(fallback,"default standby");
    if(out.expression.empty())out.expression=Read(default_expression,"default expression");
    return out;
}
}
