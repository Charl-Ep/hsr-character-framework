#include <hksr/framework/presentation/ActionPlayback.hpp>
#include <hksr/animation/ReadyPresentation.hpp>
#include <cmath>
#include <stdexcept>

namespace hksr::framework {
namespace {
bool Matches(const api::AnimatorStateObservation& state,int hash) {
    return hash && state.read.returned && (state.short_name_hash==hash || state.full_path_hash==hash);
}
const ReadyPose& Pose(const ReadyState& request) {
    if(!request.actions || request.pose>=request.actions->poses.size())throw std::runtime_error("ready pose source missing");
    return request.actions->poses[request.pose];
}
}
void ActionPlayback::ReleaseReady(void* animator,bool preserve_effect) {
    if(!preserve_effect)CancelReadyEffect();
    if(!ready_)return;
    const auto old=*ready_;ready_.reset();ready_cancelled_=false;
    if(animator==old.animator && old.speed)try {
        status_.ready_note=old.speed->Release([&]{return !Ended() && !status_.closing;}).detail;
    }catch(const std::exception& e){status_.ready_note=e.what();}
}
void ActionPlayback::StartReady(const ReadyState& request,void* animator,void* style,const api::WorldFrame& frame,
    api::ManagedObjects& objects,ActionReceipt& receipt) {
    const auto revision=intent_revision_;
    auto live=[&]{return !Ended() && !status_.closing && revision==intent_revision_;};
    const auto& target=Pose(request);
    const int hash=HashName(target.state,objects);
    receipt.hash={hash,{true,0}};receipt.has_state=actions_.HasState(animator,0,hash);
    if(!live()){receipt.outcome=ActionOutcome::Cancelled;return;}
    if(!receipt.has_state.read.returned)throw std::runtime_error("ready target membership unread");
    if(!receipt.has_state.value){receipt.outcome=ActionOutcome::StateAbsent;return;}
    const auto blending=actions_.InTransition(animator);
    if(!live()){receipt.outcome=ActionOutcome::Cancelled;return;}
    if(!blending.read.returned)throw std::runtime_error("ready transition observation unavailable");
    // Official NNMPPHHHEIK helper BCCD000(..., true) uses the NEXT state
    // during a blend, including its normalized time for the reverse window.
    const auto current=blending.value?actions_.NextState(animator):actions_.CurrentState(animator);
    if(!live()){receipt.outcome=ActionOutcome::Cancelled;return;}
    if(!current.read.returned)throw std::runtime_error("ready effective state unread");
    int from=-1;
    if(ready_ && ready_->animator==animator && ready_->request.actions->source==request.actions->source) {
        auto& old=*ready_;const auto& old_pose=Pose(old.request);
        const bool same=old_pose.skill_index==target.skill_index && old_pose.skill_name==target.skill_name &&
            old_pose.variant_index==target.variant_index && old_pose.state==target.state;
        if(same && (old.successor_seen || Matches(current,old.edge_hash) || Matches(current,old.pose_hash))) {
            const bool changing_back=old.reversing && old.pending.has_value();
            if(old.reversing && old.speed && old.speed->Owned()) {
                receipt.call=old.speed->Apply(1,live).call;
                if(!receipt.call.returned){ReleaseReady(animator);receipt.outcome=ActionOutcome::CallFailed;return;}
                if(!live()){receipt.outcome=ActionOutcome::Cancelled;return;}
                old.reversing=false;old.pending.reset();
            }else receipt.call={true,0};
            // If reversing back to a previous selection, the effect belongs
            // to that selection too. An unchanged selection does not respawn.
            if(changing_back)ReplaceReadyEffect(target,objects);
            old.serial=receipt.serial;
            ready_cancelled_=false;receipt.outcome=ActionOutcome::Returned;
            status_.ready_note="same ready pose retained; progress not restarted";return;
        }
        if(!same && Matches(current,old.edge_hash) && target.use_transition.available && target.use_transition.value &&
            std::isfinite(current.normalized_time) && current.normalized_time>0 && current.normalized_time<old.reverse_until && old.speed && old.speed->Owned()) {
            receipt.call=old.speed->Apply(-1,live).call;
            if(!receipt.call.returned){ReleaseReady(animator);receipt.outcome=ActionOutcome::CallFailed;return;}
            if(!live()){receipt.outcome=ActionOutcome::Cancelled;return;}
            old.serial=receipt.serial;old.reversing=true;old.pending=request;ready_cancelled_=false;
            ReplaceReadyEffect(target,objects);
            receipt.outcome=ActionOutcome::Returned;status_.ready_note="authored transition reversing; newest destination queued";return;
        }
        if(old.reversing)from=old.from;
        else from=old_pose.skill_index;
    }
    const auto edge=SelectReadyEdge(*request.actions,from,target);
    int edge_hash=0;
    std::string note=edge.note;
    if(!edge.state.empty()) {
        edge_hash=HashName(edge.state,objects);
        const auto present=actions_.HasState(animator,0,edge_hash);
        if(!live()){receipt.outcome=ActionOutcome::Cancelled;return;}
        if(!present.read.returned || !present.value){edge_hash=0;note+=" transition absent/unread in current controller; direct ready pose";}
    }
    ReleaseVisual(animator);ReleaseReady(animator,true);
    if(!live()){receipt.outcome=ActionOutcome::Cancelled;return;}
    const auto presentation=api::PrepareReadyPresentation(OverrideTarget(),animator,request.actions->defaults,objects,live);
    note+=presentation.detail;
    if(!presentation.current) {
        CancelReadyEffect();
        receipt.outcome=ActionOutcome::Cancelled;
        receipt.detail="ready entry changed entity/model/intent; remaining writes cancelled";return;
    }
    ReadyPlayback next;next.request=request;next.animator=animator;next.from=from;
    next.serial=receipt.serial;
    next.pose_hash=hash;next.edge_hash=edge_hash;next.reverse_until=edge.reverse_until;next.boundary=frame.sequence;
    ready_=next;ready_cancelled_=false;
    // Only the specific float parameter used by the official ready consumer.
    // Missing parameter disables reversal, not the independently valid pose.
    if(edge_hash) {
        ready_->speed=std::make_shared<api::ReadySpeedOverride>(OverrideTarget(),animator);
        const auto result=ready_->speed->Apply(1,live);
        if(result.attempted && !result.call.returned){ReleaseReady(animator);receipt.call=result.call;receipt.outcome=ActionOutcome::CallFailed;return;}
        if(!ready_->speed->Owned())note+=" ready speed unavailable; mid-transition reversal unavailable";
    }
    if(!live()){ReleaseReady(animator);receipt.outcome=ActionOutcome::Cancelled;return;}
    receipt.call=actions_.PlayFreeStyleState(style,edge_hash?edge_hash:hash,edge.blend,0);
    receipt.outcome=receipt.call.returned?ActionOutcome::Returned:ActionOutcome::CallFailed;
    status_.ready_note="base-config ready "+std::to_string(from)+" -> "+std::to_string(target.skill_index)+
        " variant="+std::to_string(target.variant_index)+" via="+(edge_hash?edge.state:"direct")+" source="+edge.source+" "+note;
    if(ready_ && ready_->speed && ready_->speed->Owned())status_.ready_note+=" owned ready speed; reverse_until="+std::to_string(ready_->reverse_until);
    if(!receipt.call.returned)ReleaseReady(animator);
    else if(live())ReplaceReadyEffect(target,objects);
}
void ActionPlayback::TickReady(void* animator,void* style,const api::WorldFrame& frame,api::ManagedObjects& objects) {
    if(!ready_ || frame.sequence<=ready_->boundary)return;
    const auto revision=intent_revision_;
    const auto transition=actions_.InTransition(animator);
    if(Ended() || revision!=intent_revision_ || !ready_)return;
    if(!transition.read.returned)throw std::runtime_error("ready transition observation unavailable");
    const auto now=transition.value?actions_.NextState(animator):actions_.CurrentState(animator);
    if(Ended() || revision!=intent_revision_ || !ready_)return;
    if(!now.read.returned)throw std::runtime_error("ready effective state unread");
    if(ready_->reversing && ready_->pending && (!Matches(now,ready_->edge_hash) || now.normalized_time<=0 || now.normalized_time>=1)) {
        auto request=*ready_->pending;ActionReceipt receipt;receipt.request=request;receipt.serial=ready_->serial;
        StartReady(request,animator,style,frame,objects,receipt);
        PoseResult(receipt,animator);
        if(receipt.outcome!=ActionOutcome::Returned){ReleaseReady(animator);status_.ready_note="queued ready destination failed: "+receipt.detail;}
        return;
    }
    if(Matches(now,ready_->pose_hash)) {
        if(!ready_->pose_seen)status_.ready_note+="; target observed";
        ObservePose(ready_->serial,!ready_->reversing);
        ready_->pose_seen=true;return;
    }
    if(Matches(now,ready_->edge_hash)) {
        ObservePose(ready_->serial);
        ready_->edge_seen=true;
        if(transition.value || ready_->reversing || !std::isfinite(now.normalized_time) || now.normalized_time<1)return;
        const auto target_hash=ready_->pose_hash;
        const auto result=actions_.PlayFreeStyleState(style,target_hash,0,0);
        if(Ended() || revision!=intent_revision_ || !ready_)return;
        if(!result.returned){ReleaseReady(animator);status_.ready_note="ready edge completed; target call failed";return;}
        ready_->edge_hash=0;ready_->edge_seen=false;ready_->boundary=frame.sequence;
        status_.ready_note+="; edge complete; configured target submitted";return;
    }
    if(transition.value)return;
    if(!ready_->successor_seen && (ready_->edge_seen || ready_->pose_seen)) {
        // Do not infer a loop's name or rebuild the Animator graph. Let the
        // controller advance while keeping the caller's logical selection,
        // as the official ready input retains its old/new skill indices.
        ready_->successor_seen=true;
        status_.ready_note+="; controller successor retained; selected skill unchanged";
    }
    if(ready_->successor_seen)ObservePose(ready_->serial,true);
}
}
