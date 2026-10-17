#include <hksr/framework/presentation/ActionPlayback.hpp>
#include <cmath>
#include <stdexcept>

namespace hksr::framework {
std::optional<VisualState> ResolveVisualState(const api::VisualConfigSources& sources,std::int32_t flag,bool recover) {
    std::optional<VisualState> result;
    if(!sources.context_available)return result;
    for(const auto* config:{&sources.defaults,&sources.character})for(const auto& row:config->behaviors.rows)
        if(row.flag.available && row.flag.value==flag) {
            VisualState value;value.source=config->source;value.value=row;value.recover=recover;
            value.expression_source=sources.defaults.source;value.expression_transition=sources.expression_transition;
            value.expression={true,"",{}};
            if(!sources.defaults.loaded || !sources.defaults.behaviors.enumerated)value.expression={false,"","global expression table unread"};
            else for(const auto& global:sources.defaults.behaviors.rows)
                if(global.flag.available && global.flag.value==flag)value.expression=global.emotion;
            if(sources.blink_source.available && sources.blink_suppressed_expression.available && value.expression.available &&
                value.expression.value==sources.blink_suppressed_expression.value)value.blink_source=sources.blink_source.value;
            result=std::move(value);
        }
    return result;
}
namespace {
bool Matches(const api::AnimatorStateObservation& state,int hash) {
    return state.read.returned && (state.short_name_hash==hash || state.full_path_hash==hash);
}
const api::VisualPhaseInput& Phase(const api::VisualBehaviorInput& row,int phase) {
    return phase==0?row.occur:phase==1?row.loop:row.recover;
}
}
int ActionPlayback::HashName(const std::string& name,api::ManagedObjects& objects) {
    if(name.empty() || name.find('\0')!=std::string::npos)throw std::runtime_error("invalid visual state name");
    const auto hash=actions_.StateHash(resources_.Text(name,objects));
    if(!hash.read.returned)throw std::runtime_error("visual state hash unavailable");
    return hash.value;
}
void ActionPlayback::ReleaseVisual(void* animator) {
    if(!visual_)return;
    const auto old=*visual_;visual_.reset();visual_cancelled_=false;
    auto live=[&]{return !Ended() && !status_.closing;};
    // Independent sources: failure to release blink must not skip expression.
    if(old.blink)try{status_.blink_note=old.blink->Release(live).detail;}
        catch(const std::exception& e){status_.blink_note=e.what();}
    if(old.expression && animator==old.animator)try{status_.visual_note=old.expression->Release(live).detail;}
        catch(const std::exception& e){status_.visual_note=e.what();}
}
void ActionPlayback::StartVisual(const VisualState& input,void* animator,void* style,
    const api::WorldFrame& frame,api::ManagedObjects& objects,ActionReceipt& receipt) {
    VisualState request=input;
    if(presentation_enabled_ && program_)if(const auto phase=program_->CurrentPhase()) {
        if(!phase->visuals.character.behaviors.enumerated || !input.value.flag.available)
            throw std::runtime_error("active phase visual table or requested flag unread");
        const auto selected=ResolveVisualState(phase->visuals,input.value.flag.value,input.recover);
        if(!selected)throw std::runtime_error("requested visual flag absent from active phase");
        request=*selected;
    }
    const auto revision=intent_revision_;
    auto live=[&]{return !Ended() && !status_.closing && revision==intent_revision_;};
    VisualPlayback next;next.request=request;next.animator=animator;next.phase=request.recover?2:0;next.boundary=frame.sequence;
    if(!request.recover && !request.expression.available)throw std::runtime_error("configured expression unread: "+request.expression.error);
    const int begin=request.recover?2:0,end=request.recover?3:2;
    for(int i=begin;i<end;++i) {
        const auto& phase=Phase(request.value,i);
        if(!phase.resolved_state.available)throw std::runtime_error("visual phase unresolved: "+phase.resolved_state.error);
        if(phase.resolved_state.value.empty())continue;
        if(!phase.transition.available || !std::isfinite(phase.transition.value) || phase.transition.value<0)
            throw std::runtime_error("visual phase transition unavailable/invalid");
        next.body[i]=HashName(phase.resolved_state.value,objects);
        const auto present=actions_.HasState(animator,0,next.body[i]);
        if(!live()){receipt.outcome=ActionOutcome::Cancelled;return;}
        if(!present.read.returned)throw std::runtime_error("visual body state query failed");
        if(!present.value){receipt.outcome=ActionOutcome::StateAbsent;receipt.detail=phase.resolved_state.value;return;}
    }
    if(!next.body[next.phase]) {
        if(!request.recover && next.body[1])next.phase=1;
        else {receipt.outcome=ActionOutcome::Unavailable;receipt.detail="no body phase in selected visual row; non-animation effects are not implemented";return;}
    }
    // Recover exits this visual effect. Official CFF schedules recovery and
    // reevaluates the expression in the same update, not at clip completion.
    // Release our expression below; do not apply the outgoing one again.
    if(!request.recover && !request.expression.value.empty()) {
        if(!request.expression_transition.available || !std::isfinite(request.expression_transition.value) || request.expression_transition.value<0)
            throw std::runtime_error("expression transition unavailable/invalid");
        const auto face=HashName(request.expression.value,objects);
        next.expression=std::make_shared<api::ExpressionOverride>(OverrideTarget(),animator,face,
            request.expression_transition.value,live,visual_?visual_->expression.get():nullptr);
    }
    if(!live()){receipt.outcome=ActionOutcome::Cancelled;return;}
    ReleaseVisual(animator);
    if(!live()){receipt.outcome=ActionOutcome::Cancelled;return;}
    visual_=next;visual_cancelled_=false;
    const auto& phase=Phase(request.value,next.phase);
    receipt.hash={next.body[next.phase],{true,0}};
    receipt.call=actions_.PlayFreeStyleState(style,next.body[next.phase],phase.transition.value,0);
    receipt.outcome=receipt.call.returned?ActionOutcome::Returned:ActionOutcome::CallFailed;
    if(!live() || !receipt.call.returned){visual_cancelled_=true;return;}
    if(next.expression) {
        const auto applied=next.expression->Apply(live);
        if(!applied.call.returned){visual_cancelled_=true;receipt.outcome=live()?ActionOutcome::CallFailed:ActionOutcome::Cancelled;receipt.detail=applied.detail;return;}
    }
    if(!request.recover && next.expression && request.blink_source>=0 && live())try {
        auto blink=std::make_shared<api::BlinkOverride>(OverrideTarget(),request.blink_source);
        visual_->blink=blink; // retain partial-write cleanup before the call
        const auto applied=blink->Apply(objects,live);
        status_.blink_note=applied.detail;
        if(applied.attempted && !applied.call.returned) {
            blink->Release(live);
            status_.blink_note=applied.detail;
        }
    }catch(const std::exception& e){status_.blink_note=e.what();}
    status_.visual_note=request.source+" flag="+std::to_string(request.value.flag.value)+
        (request.recover?" recovery requested":" occurrence/hold requested")+"; body/expression only";
}
void ActionPlayback::TickVisual(void* animator,void* style,const api::WorldFrame& frame) {
    if(!visual_ || frame.sequence<=visual_->boundary)return;
    const auto tracking=*visual_;const auto revision=intent_revision_;
    const auto current=actions_.CurrentState(animator,0);
    const auto transition=actions_.InTransition(animator,0);
    if(Ended() || revision!=intent_revision_)return;
    if(!current.read.returned || !transition.read.returned)throw std::runtime_error("visual phase observation failed");
    if(transition.value)return;
    if(!Matches(current,tracking.body[tracking.phase])) {
        if(tracking.phase==0 && tracking.body[1] && Matches(current,tracking.body[1])) {
            visual_->phase=1;visual_->body_seen=true;status_.visual_note="official occurrence -> loop observed";return;
        }
        if(tracking.body_seen){ReleaseVisual(animator);status_.visual_note="official body successor retained";}
        return;
    }
    visual_->body_seen=true;
    if(current.loop || !std::isfinite(current.normalized_time) || current.normalized_time<1)return;
    if(tracking.phase==0 && tracking.body[1]) {
        const auto call=actions_.PlayFreeStyleState(style,tracking.body[1],tracking.request.value.loop.transition.value,0);
        if(Ended() || revision!=intent_revision_)return;
        if(!call.returned)throw std::runtime_error("visual loop transition failed");
        visual_->phase=1;visual_->body_seen=false;visual_->boundary=frame.sequence;
        status_.visual_note="occurrence completed; configured loop requested";
    } else if(tracking.phase==2) {
        ReleaseVisual(animator);
        if(!Ended() && !status_.closing && revision==intent_revision_ && !CompletionState().empty())Queue(FreeStyleState{CompletionState()});
    }
    // No inferred duration: without a loop clip, hold the occurrence's final
    // pose until the next user intent rather than apply gameplay modifiers.
}
}
