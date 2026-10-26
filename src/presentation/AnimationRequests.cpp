#include <hksr/framework/presentation/AnimationRequests.hpp>
#include <hksr/animation/AnimationControl.hpp>
#include <hksr/animation/AnimationResources.hpp>
#include <hksr/model/ProfileInputs.hpp>
#include <hksr/entity/LifecycleHooks.hpp>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace hksr::framework {
struct AnimationRequests::Entry {
    AnimationCommand command;
    std::shared_ptr<AnimationRequestStatus> status;
    void* animator=nullptr;
    int hash=0;
    std::uint64_t submitted_frame=0,last_frame=0;
    bool serviced=false;
    bool held=false;
};
AnimationRequests::AnimationRequests(api::EntityToken owner,api::GameWorld* world,api::ActorAnimationSession& engine,BodyPlayer body)
    :owner_(owner),world_(world),engine_(engine),body_player_(std::move(body)) {
    if(!owner.entity || !owner.generation || !world || engine.Owner().entity!=owner.entity || engine.Owner().generation!=owner.generation)
        throw std::invalid_argument("animation requests require their owned session");
}
bool AnimationRequests::Live(const std::shared_ptr<Entry>& entry) const {
    const auto found=entries_.find(entry->status->serial);
    return found!=entries_.end() && found->second==entry && !entry->status->released &&
        !api::TrackedEntities().IsDisposed(owner_) && !api::TrackedEntities().IsRetiring(owner_) &&
        api::TrackedEntities().Current(owner_.entity).generation==owner_.generation;
}
void AnimationRequests::Finish(const std::shared_ptr<Entry>& entry,const std::string& reason) {
    auto& status=*entry->status;status.cancelled=!status.state_left && !status.advanced;status.released=true;
    if(status.cancelled && status.error.empty())status.error=reason;
}
std::shared_ptr<const AnimationRequestStatus> AnimationRequests::Submit(AnimationCommand command) {
    if(command.state.empty() || command.state.find('\0')!=std::string::npos || command.layer<0 ||
        !std::isfinite(command.transition) || command.transition<0 || !std::isfinite(command.normalized_start) ||
        (command.normalized_end && !std::isfinite(*command.normalized_end)) ||
        (command.track && !command.normalized_end))
        throw std::invalid_argument("invalid animation command");
    if(!command.track)Cancel(command.layer);
    else {
        // A tracked lane replaces legacy observations, not other records in
        // its queue. Validation precedes every ownership change.
        for(auto it=entries_.begin();it!=entries_.end();) {
            if(it->second->command.layer==command.layer && !it->second->command.track) {
                Finish(it->second,"animation track replaced direct request");it=entries_.erase(it);
            }else ++it;
        }
    }
    auto entry=std::make_shared<Entry>();entry->command=std::move(command);
    entry->status=std::make_shared<AnimationRequestStatus>();
    entry->status->serial=++next_;entry->status->entity_generation=owner_.generation;
    entries_.emplace(next_,entry);
    if(entry->command.track) {
        auto& queue=tracks_[entry->command.layer];
        const auto& track=*entry->command.track;
        if(!queue.empty() && animator_) {
            entry->animator=animator_;entry->status->model_generation=model_;
        }
        entry->held=!track.release_on.empty();
        auto match=std::find_if(queue.begin(),queue.end(),[&](const auto& previous) {
            const auto& allowed=previous->command.track->release_on;
            return std::find(allowed.begin(),allowed.end(),track.extra)!=allowed.end();
        });
        if(match==queue.end())queue.push_back(entry);
        else {(*match)->held=false;queue.insert(std::next(match),entry);}
        if(track.force) {
            while(queue.front()!=entry) {
                const auto previous=queue.front();queue.pop_front();
                previous->status->request_finished=previous->status->advanced=true;
            }
        }
    }
    return entry->status;
}
void AnimationRequests::Release(std::uint64_t serial) {
    const auto found=entries_.find(serial);if(found==entries_.end())return;
    found->second->status->released=true;entries_.erase(found);
    for(auto it=tracks_.begin();it!=tracks_.end();) {
        auto& queue=it->second;
        queue.erase(std::remove_if(queue.begin(),queue.end(),[&](const auto& value){return value->status->serial==serial;}),queue.end());
        if(queue.empty())it=tracks_.erase(it);else ++it;
    }
    // Releasing observation never resets an Animator or overwrites a newer
    // state. A task's authored successor owns the next animation request.
}
void AnimationRequests::Cancel(std::optional<int> layer,const std::string& reason) {
    if(layer)tracks_.erase(*layer);else tracks_.clear();
    if(!layer)animator_=nullptr; // explicit owner/domain handoff starts a new binding epoch
    for(auto it=entries_.begin();it!=entries_.end();) {
        if(layer && it->second->command.layer!=*layer){++it;continue;}
        Finish(it->second,reason);it=entries_.erase(it);
    }
}
bool AnimationRequests::AtHead(const std::shared_ptr<Entry>& entry) const {
    if(!entry->command.track)return true;
    const auto found=tracks_.find(entry->command.layer);
    return found!=tracks_.end() && !found->second.empty() && found->second.front()==entry;
}
void AnimationRequests::AdvanceTracks() {
    for(auto& [layer,queue]:tracks_) {
        if(queue.empty())continue;
        auto current=queue.front();auto& status=*current->status;
        if(current->held || status.released || status.cancelled)continue;
        if(!status.end_reached)continue;
        // The ordinary track delays its sole SkillFinishRecover successor to
        // the current clip's end. This is not a rule for other successors or
        // queues containing additional requests.
        if(queue.size()==2 && queue[1]->command.track->logic==1 && queue[1]->command.track->extra==6 &&
            !status.state_left && status.progress.value_or(0)<1)continue;
        status.request_finished=true;
        if(queue.size()==1)continue;
        status.advanced=true;queue.pop_front();
        // Non-playing, non-positive-End records without a hold can be passed
        // over when a later request is already queued. Preserve that receipt.
        while(queue.size()>1 && !queue.front()->status->accepted && !queue.front()->held &&
            *queue.front()->command.normalized_end<=0) {
            queue.front()->status->request_finished=queue.front()->status->advanced=true;
            queue.pop_front();
        }
    }
}
void AnimationRequests::Service(const api::WorldFrame& frame,bool ready,api::ManagedObjects& objects) {
    if(servicing_ || entries_.empty())return;
    if(api::TrackedEntities().IsDisposed(owner_) || api::TrackedEntities().IsRetiring(owner_) ||
        api::TrackedEntities().Current(owner_.entity).generation!=owner_.generation) {
        Cancel({},"animation owner ended");return;
    }
    if(frame.world!=world_ || !frame.disposing.read.returned || frame.disposing.value) {
        Cancel({},"animation owner world left");return;
    }
    if(!ready)return;
    servicing_=true;struct Guard{bool& value;~Guard(){value=false;}} guard{servicing_};
    const auto& control=engine_.Control();auto& resources=engine_.Resources();
    std::vector<std::shared_ptr<Entry>> snapshot;
    for(const auto& [id,entry]:entries_)snapshot.push_back(entry);
    for(const auto& entry:snapshot) {
        auto& status=*entry->status;
        if(!Live(entry) || status.state_left || status.advanced || !AtHead(entry) ||
            (entry->serviced && frame.sequence<=entry->last_frame))continue;
        entry->serviced=true;entry->last_frame=frame.sequence;
        try {
            resources.RetainOwner();if(!Live(entry))continue;
            const auto entity=engine_.Entities().Read(owner_.entity);if(!Live(entry))continue;
            if(!entity.owner_world.read.returned || entity.owner_world.value!=world_ || !entity.loaded.read.returned)
                throw std::runtime_error("animation entity observation unavailable");
            if(!entity.loaded.value)continue;
            const auto component=resources.Component(api::AnimationComponent::Model,objects);if(!Live(entry))continue;
            if(!component.call.returned || !component.object)throw std::runtime_error("animation model unavailable");
            const auto model=engine_.Models().Read(static_cast<api::CharacterModelComponent*>(component.object));if(!Live(entry))continue;
            if(!model.changing.read.returned || !model.animator.read.returned)throw std::runtime_error("animation model observation unread");
            if(model.changing.value || !model.animator.value) {
                animator_=nullptr;
                if(status.accepted)Cancel(entry->command.layer,"animation model changed");
                continue;
            }
            if(animator_!=model.animator.value){animator_=model.animator.value;++model_;}
            if((status.accepted || (entry->command.track && entry->animator)) &&
                (entry->animator!=animator_ || status.model_generation!=model_)) {
                Cancel(entry->command.layer,"animation model changed");continue;
            }
            if(!status.accepted) {
                const auto initialized=engine_.Inputs().AnimatorInitialized(animator_);if(!Live(entry))continue;
                if(!initialized.read.returned)throw std::runtime_error("animation initialization unread");
                if(!initialized.value)continue;
                const auto text=resources.Text(entry->command.state,objects);if(!Live(entry))continue;
                const auto hash=control.StateHash(text);if(!Live(entry))continue;
                if(!hash.read.returned)throw std::runtime_error("animation state hash unread");
                const auto has=control.HasState(animator_,entry->command.layer,hash.value);if(!Live(entry))continue;
                if(!has.read.returned || !has.value)throw std::runtime_error("animation state unavailable: "+entry->command.state);
                // Native reads may reenter model switching. Recheck before the
                // only write; never reuse a captured Animator on a new model.
                const auto before=engine_.Models().Read(static_cast<api::CharacterModelComponent*>(component.object));
                if(!Live(entry))continue;
                if(!before.changing.read.returned || !before.animator.read.returned || before.changing.value || before.animator.value!=animator_)continue;
                entry->animator=animator_;entry->hash=hash.value;entry->submitted_frame=frame.sequence;
                status.model_generation=model_;
                api::CallResult call;
                if(entry->command.layer==0 && body_player_)call=body_player_(entry->command);
                else if(entry->command.layer==0) {
                    const auto style=resources.Component(api::AnimationComponent::FreeStyle,objects);if(!Live(entry))continue;
                    if(!style.call.returned || !style.object)throw std::runtime_error("animation FreeStyle unavailable");
                    const auto final_model=engine_.Models().Read(static_cast<api::CharacterModelComponent*>(component.object));
                    if(!Live(entry))continue;
                    if(!final_model.animator.read.returned || !final_model.changing.read.returned ||
                        final_model.changing.value || final_model.animator.value!=entry->animator)continue;
                    call=control.PlayFreeStyleState(style.object,entry->hash,entry->command.transition,entry->command.normalized_start);
                } else if(entry->command.transition>0)
                    call=control.CrossFade(entry->animator,entry->hash,entry->command.transition,entry->command.layer,entry->command.normalized_start);
                else call=control.PlayAnimator(entry->animator,entry->hash,entry->command.layer,entry->command.normalized_start);
                status.accepted=call.returned;
                if(!Live(entry))continue;
                if(!call.returned)throw std::runtime_error("animation call failed");
                continue; // acceptance is not observed entry or completion
            }
            if(frame.sequence<=entry->submitted_frame)continue;
            auto state=control.CurrentState(entry->animator,entry->command.layer);if(!Live(entry))continue;
            if(!state.read.returned)throw std::runtime_error("animation progress unread");
            const auto transition=control.InTransition(entry->animator,entry->command.layer);if(!Live(entry))continue;
            if(!transition.read.returned)throw std::runtime_error("animation transition unread");
            auto matches=[&](const auto& value){return value.short_name_hash==entry->hash || value.full_path_hash==entry->hash;};
            if(!matches(state) && transition.value) {
                const auto next=control.NextState(entry->animator,entry->command.layer);if(!Live(entry))continue;
                if(!next.read.returned)throw std::runtime_error("animation next state unread");
                if(matches(next))state=next;
            }
            if(matches(state)) {
                status.entered=true;
                if(std::isfinite(state.normalized_time) && state.normalized_time>=0)
                    status.progress=std::max(status.progress.value_or(0),double(state.normalized_time));
                if(!state.loop && status.progress.value_or(0)>=1 && !transition.value)status.complete=true;
            } else if(status.entered && !transition.value)status.complete=status.state_left=true;
            if(entry->command.normalized_end && status.entered) {
                const auto end=*entry->command.normalized_end;
                if(end<=0 || status.state_left || (status.progress && *status.progress>=end))status.end_reached=true;
            }
        }catch(const std::exception& error){
            if(entry->command.track)Cancel(entry->command.layer,error.what());
            else Finish(entry,error.what());
        }
    }
    AdvanceTracks();
    for(auto it=entries_.begin();it!=entries_.end();) {
        if(it->second->status->released)it=entries_.erase(it);else ++it;
    }
    resources.Drain();
}
}
