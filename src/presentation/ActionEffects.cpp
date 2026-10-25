#include <hksr/framework/presentation/ActionPlayback.hpp>
#include <sstream>

namespace hksr::framework {
void ActionPlayback::CancelReadyEffect() {
    if(ready_effect_)ready_effect_->Cancel();
    ready_effect_=nullptr;
    effect_intent_revision_.reset();
}
void ActionPlayback::ReplaceReadyEffect(const ReadyPose& pose,api::ManagedObjects& objects) {
    if(effect_intent_revision_ && *effect_intent_revision_==intent_revision_)return;
    CancelReadyEffect();
    effect_intent_revision_=intent_revision_;
    if(!pose.effect_path.available) {status_.effect_note="ready effect source unread";return;}
    if(pose.effect_path.value.empty()){status_.effect_note="ready effect path empty in official config";return;}
    if(!effect_services_){status_.effect_note="ready effect services unavailable";return;}
    const auto revision=intent_revision_;
    try {
        const auto path=objects.NewStringUtf8(pose.effect_path.value.c_str());
        if(!path.call.returned || !path.object){status_.effect_note="ready effect path allocation failed";return;}
        if(Ended() || status_.closing || revision!=intent_revision_)return;
        // Registered before Start: an official call may synchronously replace
        // the caller's action. Callback itself only writes its shared signals.
        const auto serial=++effect_serial_;
        const auto prefix="ready effect owner="+std::to_string(owner_.generation)+" request="+std::to_string(serial)+" path="+pose.effect_path.value+" ";
        auto request=effect_services_->CreateRequest(prefix);
        ready_effect_=request.get();
        effects_.push_back({serial,pose.effect_path.value,std::move(request)});
        auto* submitted=ready_effect_;
        submitted->Start(owner_.entity,path.object);
        if(Ended() || status_.closing || revision!=intent_revision_)submitted->Cancel();
    }catch(const std::exception& e){status_.effect_note=std::string("ready effect: ")+e.what();}
}
void ActionPlayback::ServiceEffects(const api::WorldFrame& frame) {
    hit_requests_.Retire(frame);
    ServiceRetirements(frame);
    if(program_ && (Ended() || OwnFrame(frame))) {
        if(Ended())program_->End();
        // A queued body replacement is resolved below. Do not submit old tail
        // instructions before that replacement has had a chance to retire it.
        const auto* layer=pending_?std::get_if<AnimatorState>(&pending_->request):nullptr;
        program_->Service(!pending_ || (layer && layer->layer!=0));
    }
    if(effects_.empty())return;
    if(!frame.world || !frame.disposing.read.returned || frame.disposing.value)return;
    const bool other_action=pending_ && !std::holds_alternative<ReadyState>(pending_->request);
    if(Ended() || status_.closing || other_action || (ready_cancelled_ && !pending_))CancelReadyEffect();
    std::ostringstream note;
    for(auto it=effects_.begin();it!=effects_.end();) {
        it->request->Service();const auto& value=it->request->Status();
        note<<" request="<<it->serial<<" path="<<it->path<<" id="<<value.reference.id
            <<" submitted="<<value.submitted<<" callbacks="<<value.completions<<" loaded="<<value.loaded_completions
            <<" foreign="<<value.foreign_completions<<" cancel="<<value.cancelled<<" remove_attempts="<<value.removal_attempts
            <<" submit_exception="<<value.submit.exception<<" remove_exception="<<value.removal.read.exception
            <<" state_read="<<value.observation.state.read.returned<<" state="<<value.observation.state.value
            <<" closed="<<value.closed<<" detail="<<value.detail<<";";
        if(value.closed) {
            if(ready_effect_==it->request.get())ready_effect_=nullptr;
            it=effects_.erase(it);
        }else ++it;
    }
    status_.effect_note=note.str();
}
void ActionPlayback::PrepareRetirements(const FreeStyleState& request) {
    if(request.retirements.empty())return;
    std::ostringstream tag;tag<<"action exit entity="<<owner_.entity<<" generation="<<owner_.generation
        <<" action="<<intent_revision_<<" state="<<request.name<<" ";
    const auto log=[&](const std::string& text){if(effect_services_)effect_services_->Trace(tag.str()+text);};
    if(!effect_services_ || !effect_services_->HasExitEvents()){log("registration service unavailable");return;}
    try {
        const auto revision=intent_revision_;
        std::vector<api::EffectRemovalSource> sources;
        for(const auto& value:request.retirements)sources.push_back(value.removal);
        action_exit_=effect_services_->RegisterExit(owner_,request.name,sources);
        if(!action_exit_){log("no registration produced");return;}
        if(revision!=intent_revision_ || status_.closing){retire_action_events_=true;return;}
        retire_action_events_=false;
        if(Ended()){action_exit_->OwnerEnded();return;}
        action_exit_->Service();
        const auto& status=action_exit_->Status();
        for(const auto& error:status.errors)log(error);
        for(const auto& file:action_exit_->Files())log("registered="+std::to_string(file.registration.call.returned)+
            " exception="+std::to_string(file.registration.call.exception)+" "+file.error);
    }catch(const std::exception& e){log(e.what());}
}
void ActionPlayback::RetireActionEvents() {
    if(action_exit_)retired_exits_.push_back(std::move(action_exit_));
    for(auto& value:retired_exits_) {
        if(Ended())value->OwnerEnded();
        else if(!value->Status().retirement_complete && !value->Status().identity_changed) {
            const bool first=!value->Status().closing;
            const auto result=value->Retire();
            if(effect_services_ && (first || !result.complete)) {
                std::ostringstream line;line<<"action exit retirement entity="<<owner_.entity<<" generation="<<owner_.generation
                    <<" complete="<<result.complete<<" registrations="<<value->Files().size();
                for(const auto& error:result.errors)line<<" error="<<error;
                effect_services_->Trace(line.str());
            }
        }
    }
    retire_action_events_=false;
}
void ActionPlayback::ServiceRetirements(const api::WorldFrame& frame) {
    if(!Ended() && !OwnFrame(frame))return;
    if(Ended() || status_.closing || retire_action_events_)RetireActionEvents();
    if(action_exit_)action_exit_->Service();
    for(auto it=retired_exits_.begin();it!=retired_exits_.end();) {
        if(Ended())(*it)->OwnerEnded();
        if((*it)->Close().complete)it=retired_exits_.erase(it);else ++it;
    }
}
}
