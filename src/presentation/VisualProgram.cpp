#include <hksr/framework/presentation/VisualProgram.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace hksr::framework {
VisualProgramPlayer::VisualProgramPlayer(std::shared_ptr<const VisualProgram> program,VisualProgramServices services)
    :program_(std::move(program)),services_(std::move(services)) {
    if(!program_ || !services_.effects || !services_.create)throw std::invalid_argument("visual program inputs missing");
    if(services_.timeline && (!services_.cancel_timelines || !services_.timelines_drained))
        throw std::invalid_argument("Timeline submission requires cancellation and drain observations");
}
std::uint64_t VisualProgramPlayer::Select(const std::string& action,std::map<std::string,std::string> choices) {
    if(status_.closing)throw std::logic_error("visual program closing");
    if(!program_->actions.count(action))throw std::invalid_argument("unknown visual program action: "+action);
    DiscardActionSignals();pending_=Selection{++next_,action,std::move(choices)};return next_;
}
std::uint64_t VisualProgramPlayer::Signal(std::uint64_t intent,const std::string& name) {
    if(!program_->signals.count(name))throw std::invalid_argument("unknown visual signal: "+name);
    if(!intent || status_.closing || end_pending_)return 0;
    Execution execution{intent,{}};
    const auto& definition=program_->signals.at(name);
    if(!definition.context.empty()) {
        const auto found=contexts_.find(definition.context);
        if(found==contexts_.end() || found->second->token!=intent || found->second->phase!=ContextPhase::Active)return 0;
        execution.lifetime=found->second;
    } else if(intent!=next_ || intent!=status_.intent || pending_ || !context_ ||
        !status_.prepared || status_.completed)return 0;
    const auto serial=++next_signal_;
    signals_.push_back(std::make_shared<SignalExecution>(SignalExecution{serial,std::move(execution),name,{}}));
    status_.last_signal=serial;status_.signal=name;return serial;
}
void VisualProgramPlayer::Retire(bool fade) {
    if(context_){context_->End(fade);retired_.push_back(std::move(context_));}
    // An interruption also cancels tails retained by earlier natural finishes.
    for(auto& context:retired_)if(!context->Closing())context->End(fade);
    try {if(services_.detach)services_.detach();}catch(const std::exception& e){status_.issues.push_back(e.what());}
    try {if(services_.cancel_timelines)services_.cancel_timelines(0);}catch(const std::exception& e){status_.issues.push_back(e.what());}
    variables_.clear();
}
void VisualProgramPlayer::Finish() {
    DiscardActionSignals();
    if(context_){context_->Finish();retired_.push_back(std::move(context_));}
    try {if(services_.detach)services_.detach();}catch(const std::exception& e){status_.issues.push_back(e.what());}
    status_.playing=false;status_.completing=false;status_.completed=true;
    variables_.clear();
}
void VisualProgramPlayer::End(bool fade) {
    if(status_.closing)return;
    Leave(fade);status_.closing=true;
}
void VisualProgramPlayer::Leave(bool fade) {
    if(status_.closing)return;
    ++next_;pending_.reset();signals_.clear();status_.prepared=false;status_.playing=false;status_.completing=false;
    end_pending_=true;end_fade_=fade;
}
void VisualProgramPlayer::InterruptAction(bool fade) {
    if(status_.closing)return;
    ++next_;pending_.reset();DiscardActionSignals();status_.prepared=false;status_.playing=false;status_.completing=false;
    interrupt_pending_=true;end_fade_=fade;
}
const VisualProgramAction& VisualProgramPlayer::ResolveAction(const std::string& program,const std::string& action) const {
    if(program!=program_->id)throw std::invalid_argument("visual program does not belong to this actor: "+program);
    const auto found=program_->actions.find(action);
    if(found==program_->actions.end())throw std::invalid_argument("unknown visual program action: "+action);
    return found->second;
}
bool VisualProgramPlayer::Drained() const {return status_.closing && !end_pending_ && !context_ && retired_.empty() &&
    contexts_.empty() && retired_contexts_.empty() && !timelines_pending_;}
void VisualProgramPlayer::Issue(const VisualInstruction& input,const std::string& detail) {
    status_.issues.push_back(input.source+": "+detail);
}
VisualStepResult VisualProgramPlayer::Execute(const VisualInstruction& input,Cursor& cursor,const Execution& execution) {
    return std::visit([&](const auto& op)->VisualStepResult {
        using T=std::decay_t<decltype(op)>;
        auto& variables=execution.lifetime?execution.lifetime->variables:variables_;
        if constexpr(std::is_same_v<T,VisualContextOperation>) {
            return ChangeContext(op);
        } else if constexpr(std::is_same_v<T,VisualAssign>) {
            variables[op.variable]=op.value;return {VisualStepState::Done,{}};
        } else if constexpr(std::is_same_v<T,VisualTimeline>) {
            auto scope=execution.lifetime?execution.lifetime->token:std::uint64_t{0};
            if(!input.context.empty()) {
                scope=ContextToken(input.context);
                if(!scope)return {VisualStepState::Failed,"Timeline context is absent: "+input.context};
            }
            if(services_.timeline && op.play)timelines_pending_=true;
            return services_.timeline?services_.timeline(scope,op):VisualStepResult{VisualStepState::Failed,"Timeline service unavailable"};
        } else {
        if(!cursor.context)cursor.context=ExecutionContext(input,execution);
        auto& context=cursor.context;
        if(!context || context->Closing())return {VisualStepState::Failed,"effect context is absent or closing"};
        if constexpr(std::is_same_v<T,VisualCreate>) {
            // Every independent request is attempted, even if another asset or
            // target failed. Retain each receipt before a synchronous callback.
            while(cursor.created<op.effects.size()) {
                const auto key=op.effects[cursor.created++];
                try {
                    const auto id=services_.create(*context,key,program_->effects.at(key));
                    if(!Current(execution))return {VisualStepState::Pending,{}};
                    const auto* submitted=context->Status(id);
                    if(!submitted || !submitted->submitted || !submitted->submit.returned || submitted->cancelled)
                        Issue(input,key+": "+(submitted?submitted->detail:"creation returned no owned receipt"));
                }
                catch(const std::exception& e){Issue(input,key+": "+e.what());}
                if(!Current(execution))return {VisualStepState::Pending,{}};
            }
            return {VisualStepState::Done,{}};
        } else if constexpr(std::is_same_v<T,VisualProgress> || std::is_same_v<T,VisualTrigger> || std::is_same_v<T,VisualFollow> || std::is_same_v<T,VisualEffectState>) {
            float number=0;
            if constexpr(std::is_same_v<T,VisualProgress>) {
                if(const auto* literal=std::get_if<float>(&op.value))number=*literal;
                else {
                    const auto found=variables.find(std::get<std::string>(op.value));
                    if(found==variables.end())return {VisualStepState::Failed,"unassigned variable: "+std::get<std::string>(op.value)};
                    number=found->second;
                }
            }
            if(!cursor.receipts) {
                cursor.receipts.emplace();
                if constexpr(!std::is_same_v<T,VisualTrigger>)*cursor.receipts=context->Requests(op.effect);
                else for(const auto& key:op.effects) {
                    const auto rows=context->Requests(key);cursor.receipts->insert(cursor.receipts->end(),rows.begin(),rows.end());
                    if(rows.empty())Issue(input,"no owned effect receipt: "+key);
                }
                if(cursor.receipts->empty())return {VisualStepState::Failed,"no owned effect receipt"};
            }
            bool pending=false;
            for(const auto id:*cursor.receipts)if(!cursor.completed.count(id)) {
                VisualStepResult result;
                if constexpr(std::is_same_v<T,VisualProgress>) {
                    const auto control=context->SetProgress(id,number,op.seconds);
                    result={control.state==api::EffectControlState::Applied?VisualStepState::Done:
                        control.state==api::EffectControlState::NotLoaded?VisualStepState::Pending:VisualStepState::Failed,control.detail};
                } else if constexpr(std::is_same_v<T,VisualFollow>) {
                    result=services_.follow?services_.follow(*context,id,op):VisualStepResult{VisualStepState::Failed,"entity motion service unavailable"};
                } else if constexpr(std::is_same_v<T,VisualEffectState>) {
                    result=services_.effect_state?services_.effect_state(*context,id,op):VisualStepResult{VisualStepState::Failed,"effect Animator service unavailable"};
                } else if(services_.trigger)result=services_.trigger(*context,id,op);
                else {
                    const auto control=context->SetAnimatorTrigger(id,op.trigger,op.reset);
                    result={control.state==api::EffectControlState::Applied?VisualStepState::Done:
                        control.state==api::EffectControlState::NotLoaded?VisualStepState::Pending:VisualStepState::Failed,control.detail};
                }
                if(!Current(execution))return {VisualStepState::Pending,{}};
                if(result.state==VisualStepState::Pending)pending=true;
                else {
                    cursor.completed.insert(id);
                    if(result.state==VisualStepState::Failed)Issue(input,"receipt="+std::to_string(id)+" "+result.detail);
                }
            }
            return {pending?VisualStepState::Pending:VisualStepState::Done,{}};
        } else if constexpr(std::is_same_v<T,VisualRemove>) {
            if(services_.remove)return services_.remove(*context,op);
            if(!cursor.receipts) {
                cursor.receipts.emplace();
                for(const auto& key:op.effects) {
                    const auto rows=context->Requests(key);
                    cursor.receipts->insert(cursor.receipts->end(),rows.begin(),rows.end());
                    const bool pending=services_.remove_in_flight && services_.remove_in_flight(context,key,{op.fade,op.immediate,false});
                    if(rows.empty() && !pending) {
                        if(op.optional.count(key))status_.notes.push_back(input.source+": optional effect absent: "+key);
                        else Issue(input,"no owned effect receipt for removal: "+key);
                    }
                    for(const auto id:rows)context->Remove(id,{op.fade,op.immediate,false});
                }
                // Service at the next engine boundary; retain every receipt
                // until the manager confirms cancellation or releases it.
                return {VisualStepState::Pending,{}};
            }
            for(const auto id:*cursor.receipts)if(!cursor.completed.count(id)) {
                const auto* status=context->Status(id);
                if(status && !status->request_ended && !status->closed) {
                    if(!status->removal_attempts)return {VisualStepState::Pending,{}};
                    if(!status->removal.read.returned || !status->removal.value)Issue(input,"receipt="+std::to_string(id)+" "+status->detail);
                }
                cursor.completed.insert(id);
            }
            return {VisualStepState::Done,{}};
        }
        }
    },input.operation);
}
bool VisualProgramPlayer::Instructions(const std::vector<VisualInstruction>& steps,Cursor& cursor,const Execution& execution) {
    while(cursor.instruction<steps.size()) {
        const auto& input=steps[cursor.instruction];
        if(!input.choice.empty()) {
            const auto& choices=execution.lifetime?execution.lifetime->choices:choices_;
            const auto value=choices.find(input.choice);
            if(value==choices.end()){Issue(input,"preview input absent: "+input.choice);cursor.Next();continue;}
            if(value->second!=input.value){cursor.Next();continue;}
        }
        VisualStepResult result;
        try {result=Execute(input,cursor,execution);}
        catch(const std::exception& e){result={VisualStepState::Failed,e.what()};}
        if(!Current(execution))return false;
        if(result.state==VisualStepState::Pending){status_.waiting.push_back(input.source);return false;}
        if(result.state==VisualStepState::Failed)Issue(input,result.detail);
        cursor.Next();
    }
    return true;
}
void VisualProgramPlayer::Service(bool advance) {
    if(servicing_)return;
    servicing_=true;struct Exit{bool& flag;~Exit(){flag=false;}} exit{servicing_};
    if(end_pending_){end_pending_=false;interrupt_pending_=false;Retire(end_fade_);EndContexts(end_fade_);action_=nullptr;}
    if(interrupt_pending_){interrupt_pending_=false;Retire(end_fade_);action_=nullptr;}
    // Lifetime cleanup continues even when a resource or a lane is waiting.
    for(auto it=retired_.begin();it!=retired_.end();) {
        (*it)->Service();if((*it)->Drained())it=retired_.erase(it);else ++it;
    }
    if(timelines_pending_ && services_.timelines_drained) {
        try {timelines_pending_=!services_.timelines_drained();}
        catch(const std::exception& e){if(status_.issues.empty() || status_.issues.back()!=e.what())status_.issues.push_back(e.what());}
    }
    ServiceContexts(false);
    if(status_.closing)return;
    if(!advance){if(context_)context_->Service();return;}
    status_.waiting.clear();
    if(pending_) {
        auto selected=std::move(*pending_);pending_.reset();
        auto inputs=services_.choices;
        try {
            if(services_.select_inputs)for(auto& [key,value]:services_.select_inputs())inputs[key]=std::move(value);
        } catch(const std::exception& e) {
            status_={};status_.intent=selected.intent;status_.action=selected.action;
            status_.issues.push_back(e.what());Retire(true);action_=nullptr;return;
        }
        if(next_!=selected.intent)return;
        for(auto& [key,value]:selected.choices)inputs[key]=std::move(value);
        action_=&program_->actions.at(selected.action);
        if(action_->restart)Retire(true);
        if(next_!=selected.intent)return;
        if(!context_) {
            if(!action_->restart) {
                status_={};status_.intent=selected.intent;status_.action=selected.action;
                status_.issues.push_back("scene has not been entered");action_=nullptr;return;
            }
            context_=std::make_shared<VisualEffectContext>(services_.effects);
            try {if(services_.attach)services_.attach(context_);}
            catch(const std::exception& e) {
                status_={};status_.intent=selected.intent;status_.action=selected.action;
                status_.issues.push_back(e.what());Retire(true);action_=nullptr;return;
            }
            if(next_!=selected.intent)return;
        }
        status_={};status_.intent=selected.intent;status_.action=selected.action;status_.state=action_->state;
        choices_=std::move(inputs);enter_={};completion_={};lanes_.assign(action_->lanes.size(),{});progress_.reset();
    }
    if(context_)context_->Service();
    ServiceContexts(true);
    ServiceSignals();
    const auto intent=status_.intent;
    if(!action_ || next_!=intent || status_.closing)return;
    if(!status_.prepared) {
        if(!Instructions(action_->enter,enter_,{intent,{}}))return;
        status_.prepared=status_.issues.empty();
        if(!status_.prepared)return;
    }
    if(next_!=intent || status_.closing)return;
    if(!status_.playing || !progress_)return;
    if(status_.completing) {
        if(*progress_>=action_->completion_at && Instructions(action_->after_completion,completion_,{intent,{}}))Finish();
        return;
    }
    bool done=true;
    for(std::size_t i=0;i<lanes_.size();++i) {
        auto& cursor=lanes_[i];const auto& lane=action_->lanes[i];
        while(cursor.cue<lane.size() && *progress_>=lane[cursor.cue].at) {
            if(!Instructions(lane[cursor.cue].instructions,cursor,{intent,{}}))break;
            ++cursor.cue;cursor.instruction=0;
        }
        if(next_!=intent || status_.closing)return;
        done=done && cursor.cue==lane.size();
    }
    status_.finished=done;
}
bool VisualProgramPlayer::Played(std::uint64_t intent,std::uint64_t playback,std::uint64_t model) {
    if(intent!=next_ || intent!=status_.intent || !status_.prepared || status_.playing || status_.completed || status_.closing || !playback || !model)return false;
    status_.playback=playback;status_.model=model;status_.playing=true;return true;
}
bool VisualProgramPlayer::SuccessorPlayed(std::uint64_t intent,std::uint64_t playback,std::uint64_t model) {
    if(intent!=next_ || intent!=status_.intent || !action_ || action_->completion_state.empty() ||
        !status_.playing || !status_.finished || status_.completing || status_.closing || !playback || !model)return false;
    status_.playback=playback;status_.model=model;status_.state=action_->completion_state;
    status_.completing=true;progress_.reset();return true;
}
void VisualProgramPlayer::Observe(std::uint64_t playback,std::uint64_t model,const std::string& state,float progress) {
    if(!status_.playing || pending_ || status_.closing || playback!=status_.playback || model!=status_.model ||
        state!=status_.state || !std::isfinite(progress) || progress<0)return;
    // Never use wall time or an old model's normalized clock. Cues run once even
    // if an official transition briefly reports the previous time again.
    if(!progress_ || progress>*progress_)progress_=progress;
}
}
