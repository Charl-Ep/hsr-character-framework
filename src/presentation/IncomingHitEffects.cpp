#include <hksr/framework/presentation/ActionPlayback.hpp>
#include <algorithm>
#include <stdexcept>
#include <sstream>

namespace hksr::framework {
void ActionPlayback::CancelHitEffects() {
    ++hit_effect_revision_;
    for(const auto& entry:hit_effects_)entry->request->Cancel();
}
void ActionPlayback::ServiceHitEffects(const api::WorldFrame& frame) {
    if(hit_effects_.empty())return;
    if(Ended() || status_.closing || !OwnFrame(frame))CancelHitEffects();
    if(!frame.world || !frame.disposing.read.returned || frame.disposing.value)return;
    const auto snapshot=hit_effects_;
    for(const auto& entry:snapshot) {
        entry->request->Service();const auto& value=entry->request->Status();
        if(value.hit_completion && value.completions!=entry->reported_completion) {
            entry->reported_completion=value.completions;
            if(effect_services_)
                effect_services_->Trace("incoming hit completion owner="+std::to_string(owner_.generation)+
                    " request="+std::to_string(entry->serial)+" state="+std::to_string(static_cast<int>(value.hit_completion->state))+" "+value.hit_completion->detail);
        }
        if(value.closed && effect_services_)effect_services_->Trace("incoming hit closed owner="+std::to_string(owner_.generation)+
            " request="+std::to_string(entry->serial)+" id="+std::to_string(value.reference.id));
    }
    hit_effects_.erase(std::remove_if(hit_effects_.begin(),hit_effects_.end(),[](const auto& e){return e->request->Status().closed;}),hit_effects_.end());
}
HitEffectAdmission ActionPlayback::ReceiveHitEffects(const HitInput& input,const api::WorldFrame& frame,api::ManagedObjects& objects) {
    if(!input.effect || (input.effect->path.empty() && input.animation.empty()))return {true,true,{}};
    if(!presentation_enabled_ || !hit_catalog_ || !hit_control_)throw std::runtime_error("receiver hit-effect policy unavailable");
    const auto catalog=hit_catalog_(false);
    if(!catalog)return {};
    const auto control=hit_control_(false);
    const auto plan=SelectHitEffects(input.effect->path,input.animation,control.effects);
    if(plan.effects.empty())return {true,true,{}};
    if(!catalog->character || !catalog->character->shape.available || !catalog->character->hit_box_attach_point.available)
        throw std::runtime_error("receiver hit-effect geometry unread");
    if(!effect_services_ || !input.origin)throw std::runtime_error("hit effect services or captured source absent");
    const auto source=input.origin->caster;const auto epoch=hit_effect_revision_;
    const auto live=[&] {
        return epoch==hit_effect_revision_ && !Ended() && !status_.closing && OwnFrame(frame) &&
            source.world==world_ && source.token.entity && source.token.generation &&
            !api::TrackedEntities().IsDisposed(source.token) && !api::TrackedEntities().IsRetiring(source.token) &&
            api::TrackedEntities().Current(source.token.entity).generation==source.token.generation;
    };
    if(!live())throw std::runtime_error("hit effect endpoint ended before admission");
    resources_.RetainOwner();
    HitEffectAdmission out{true,false,{}};
    for(const auto& choice:plan.effects)try {
        if(!live())throw std::runtime_error("hit effect endpoint changed during admission");
        const auto& parameters=*input.effect;
        const auto tag="incoming hit owner="+std::to_string(owner_.generation)+" caster="+std::to_string(source.token.generation)+" path="+choice.path+" ";
        const auto placement=engine_->ReadHitEffectPlacement(input.origin->position,catalog->character->shape.value,
            catalog->character->hit_box_attach_point.value,parameters.height,parameters.angle,choice.offset,
            parameters.offset_by_scale,parameters.delay,objects);
        if(!placement.read.returned)throw std::runtime_error("hit effect placement unavailable");
        std::ostringstream geometry;const auto& p=placement.value;
        geometry<<"overrides="<<plan.has_overrides<<" offset="<<p.offset.x<<","<<p.offset.y<<","<<p.offset.z
            <<" direction="<<p.direction.x<<","<<p.direction.y<<","<<p.direction.z<<" delay="<<p.delay;
        effect_services_->Trace(tag+geometry.str());
        auto* path=resources_.Text(choice.path,objects);
        if(!live())throw std::runtime_error("hit effect endpoint changed during preparation");
        const auto candidates=[&] {
            std::vector<api::EffectReference> refs;
            for(const auto& entry:hit_effects_) {
                const auto& value=entry->request->Status();
                if(value.submitted && !value.cancelled && !value.closed && !value.request_ended)refs.push_back(value.reference);
            }
            return refs;
        };
        const auto mutex=[&](void* caster,const api::EffectMutexRule& rule,float delay,bool replay) {
            const auto result=effect_services_->HitMutex(caster,owner_.entity,path,rule,delay,replay?candidates():std::vector<api::EffectReference>{});
            if(!result.kind.read.returned || !result.error.empty())throw std::runtime_error("hit effect mutex: "+result.error);
            if(!live())throw std::runtime_error("hit effect endpoint changed during mutex query");
            effect_services_->Trace(tag+"mutex="+std::to_string(static_cast<int>(result.kind.value))+" stage="+(caster?"source":"public")+
                " interval="+std::to_string(rule.interval)+" maximum="+std::to_string(rule.maximum)+
                " scope="+std::to_string(static_cast<int>(rule.scope))+" replay="+std::to_string(result.replay?result.replay->id:0));
            if(result.kind.value==api::EffectMutexKind::None)return false;
            if(result.replay) {
                const auto snapshot=hit_effects_;
                for(const auto& entry:snapshot) {
                    const auto ref=entry->request->Status().reference;
                    if(ref.id!=result.replay->id || ref.parameters!=result.replay->parameters)continue;
                    const auto applied=entry->request->ReplayHit(result.kind.value);
                    if(applied.state!=api::EffectControlState::Applied && applied.state!=api::EffectControlState::NoMatch && applied.state!=api::EffectControlState::Ended)
                        throw std::runtime_error("hit effect mutex replay: "+applied.detail);
                    break;
                }
            }
            return true; // blocked even if no owned request can be replayed
        };
        if(mutex(source.token.entity,{parameters.minimum_interval,parameters.maximum_count,api::EffectMutexScope::Entity},parameters.delay,choice.replay_on_mutex))continue;
        const auto rules=effect_services_->ReadHitRules(choice.path);
        if(!live())throw std::runtime_error("hit effect endpoint changed during config query");
        if(rules.force_entity) {
            if(mutex(nullptr,{0,0,api::EffectMutexScope::Entity},0,true))continue;
        }else if(rules.configured && mutex(nullptr,*rules.configured,0,true))continue;
        bool updated=false;
        if(plan.has_overrides) {
            const auto snapshot=hit_effects_;
            for(const auto& entry:snapshot) {
                const auto& value=entry->request->Status();
                if(value.cancelled || value.closed || value.request_ended)continue;
                const auto matches=entry->request->MatchesHitPath(source.token.entity,path);
                if(!matches.read.returned)throw std::runtime_error("hit effect path comparison failed");
                if(!live())throw std::runtime_error("hit effect endpoint changed during path comparison");
                if(!matches.value)continue;
                const auto result=entry->request->UpdateHit(placement.value);
                if(result.state==api::EffectControlState::NotLoaded || result.state==api::EffectControlState::Ended)continue;
                if(result.state!=api::EffectControlState::Applied)throw std::runtime_error("hit effect update: "+result.detail);
                effect_services_->Trace(tag+"updated id="+std::to_string(value.reference.id));
                updated=true;break;
            }
        }
        if(updated)continue;
        if(!live())throw std::runtime_error("hit effect endpoint changed before submission");
        const auto serial=++effect_serial_;
        auto request=effect_services_->CreateRequest(tag+"request="+std::to_string(serial)+" ");
        auto entry=std::make_shared<EffectEntry>(EffectEntry{serial,choice.path,std::move(request)});
        hit_effects_.push_back(entry); // retained before synchronous native callbacks
        entry->request->StartHit(source.token.entity,owner_.entity,path,placement.value);
        if(!live())entry->request->Cancel();
        const auto& value=entry->request->Status();
        if(!value.submit.returned)throw std::runtime_error("hit effect submission: "+value.detail);
        if(value.hit_completion && value.hit_completion->state!=api::EffectControlState::Applied)
            throw std::runtime_error("hit effect completion: "+value.hit_completion->detail);
    }catch(const std::exception& e) {
        out.errors.push_back(choice.path+": "+e.what());
        effect_services_->Trace("incoming hit error owner="+std::to_string(owner_.generation)+" "+out.errors.back());
    }
    return out;
}
}
