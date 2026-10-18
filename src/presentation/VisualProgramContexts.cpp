#include <hksr/framework/presentation/VisualProgram.hpp>
#include <algorithm>
#include <stdexcept>

namespace hksr::framework {
bool VisualProgramPlayer::Current(const Execution& execution) const {
    if(status_.closing || end_pending_)return false;
    if(!execution.lifetime)return !interrupt_pending_ && next_==execution.intent;
    const auto found=contexts_.find(execution.lifetime->name);
    return found!=contexts_.end() && found->second==execution.lifetime;
}
std::uint64_t VisualProgramPlayer::ContextToken(const std::string& name) const {
    const auto found=contexts_.find(name);
    return found==contexts_.end()?0:found->second->token;
}
std::map<std::string,std::uint64_t> VisualProgramPlayer::ActiveContexts() const {
    std::map<std::string,std::uint64_t> out;
    for(const auto& [name,context]:contexts_)out.emplace(name,context->token);
    return out;
}
std::shared_ptr<const api::PhaseVisualConfigSources> VisualProgramPlayer::CurrentPhase() const {
    if(status_.closing || end_pending_)return {};
    std::shared_ptr<const api::PhaseVisualConfigSources> result;std::uint64_t latest=0;
    for(const auto& [name,lifetime]:contexts_)if(lifetime->phase_data && lifetime->token>latest) {
        latest=lifetime->token;result=lifetime->phase_data;
    }
    return result;
}
VisualBranchState VisualProgramPlayer::BranchState() const {
    VisualBranchState out;
    if(status_.closing || end_pending_)return out;
    auto append=[&](const auto& choices,const auto& variables,const std::string& scope) {
        for(const auto& [key,value]:choices)out.values.push_back({scope,key,value});
        for(const auto& [key,value]:variables)out.values.push_back({scope,key,value});
    };
    if(context_ && !context_->Closing())append(choices_,variables_,{});
    std::uint64_t latest=0;
    for(const auto& [name,lifetime]:contexts_) {
        if(!lifetime->effects->Closing())append(lifetime->choices,lifetime->variables,name);
        // Same precedence and retirement semantics as CurrentPhase().
        if(lifetime->phase_data && lifetime->token>latest) {
            latest=lifetime->token;out.phase=program_->contexts.at(name).phase;
        }
    }
    return out;
}
std::shared_ptr<VisualEffectContext> VisualProgramPlayer::ExecutionContext(
    const VisualInstruction& input,const Execution& execution) const {
    if(input.context.empty())return execution.lifetime?execution.lifetime->effects:context_;
    const auto found=contexts_.find(input.context);
    return found==contexts_.end()?nullptr:found->second->effects;
}
void VisualProgramPlayer::DiscardActionSignals() {
    signals_.erase(std::remove_if(signals_.begin(),signals_.end(),[](const auto& signal){
        return !signal->execution.lifetime;
    }),signals_.end());
}
std::shared_ptr<VisualProgramPlayer::Lifetime> VisualProgramPlayer::StartContext(
    const std::string& name,const VisualContextSnapshot* restore) {
    auto lifetime=std::make_shared<Lifetime>();lifetime->name=name;
    lifetime->token=++next_context_;lifetime->choices=restore?restore->inputs.choices:choices_;
    if(restore){lifetime->variables=restore->variables;lifetime->restoring=true;}
    lifetime->effects=std::make_shared<VisualEffectContext>(services_.effects);
    // Register ownership before any provider may reenter Select/End.
    const auto found=contexts_.emplace(name,lifetime).first;
    try {
        const auto& phase=program_->contexts.at(name).phase;
        if(!phase.empty()) {
            if(!services_.read_phase)throw std::runtime_error("phase data reader unavailable");
            lifetime->phase_data=services_.read_phase(phase);
            if(!lifetime->phase_data || !lifetime->phase_data->visuals.context_available)
                throw std::runtime_error("phase configuration unavailable: "+phase);
        }
        if(!Current({lifetime->token,lifetime}))return lifetime;
        if(services_.prepare_inputs)services_.prepare_inputs(lifetime->effects,restore?restore->inputs:selected_inputs_,restore || selected_inputs_restored_);
        if(!Current({lifetime->token,lifetime}))return lifetime;
        if(services_.context_started)services_.context_started(name,lifetime->effects);
    }catch(...) {
        try {if(services_.context_stopped)services_.context_stopped(name);}
        catch(const std::exception& e){status_.issues.push_back(name+": detach after failed entry: "+e.what());}
        EndPartners(lifetime->effects);lifetime->effects->End();retired_contexts_.push_back(lifetime->effects);
        contexts_.erase(found);throw;
    }
    return lifetime;
}
VisualStepResult VisualProgramPlayer::ChangeContext(const VisualContextOperation& operation) {
    auto found=contexts_.find(operation.name);
    if(operation.enter) {
        if(found==contexts_.end()) {
            StartContext(operation.name);found=contexts_.find(operation.name);
        }
        auto lifetime=found->second;
        if(lifetime->phase==ContextPhase::Exiting)return {VisualStepState::Pending,{}};
        DriveContext(lifetime);
        return {lifetime->phase==ContextPhase::Active?VisualStepState::Done:VisualStepState::Pending,{}};
    }
    if(found==contexts_.end())return {VisualStepState::Done,{}};
    auto lifetime=found->second;
    lifetime->phase=ContextPhase::Exiting;lifetime->fade=operation.fade;
    DriveContext(lifetime);
    return {contexts_.count(operation.name)?VisualStepState::Pending:VisualStepState::Done,{}};
}
void VisualProgramPlayer::DriveContext(std::shared_ptr<Lifetime> lifetime) {
    const Execution execution{lifetime->token,lifetime};
    if(!Current(execution))return;
    const auto& definition=program_->contexts.at(lifetime->name);
    if(lifetime->phase==ContextPhase::Entering) {
        const auto& steps=lifetime->restoring?*definition.restore:definition.enter;
        if(Instructions(steps,lifetime->enter,execution))lifetime->phase=ContextPhase::Active;
    } else if(lifetime->phase==ContextPhase::Exiting) {
        if(!Instructions(definition.exit,lifetime->exit,execution))return;
        try {if(services_.context_stopped)services_.context_stopped(lifetime->name);}
        catch(const std::exception& e){status_.issues.push_back(e.what());}
        try {if(services_.cancel_timelines)services_.cancel_timelines(lifetime->token);}
        catch(const std::exception& e){status_.issues.push_back(e.what());}
        EndPartners(lifetime->effects);lifetime->effects->End(lifetime->fade);
        retired_contexts_.push_back(lifetime->effects);
        contexts_.erase(lifetime->name);
    }
}
void VisualProgramPlayer::ServiceContexts(bool advance) {
    if(!advance) {
        for(auto it=retired_contexts_.begin();it!=retired_contexts_.end();) {
            (*it)->Service();if((*it)->Drained())it=retired_contexts_.erase(it);else ++it;
        }
    }
    // Calls can queue a new body selection or close the whole player. Hold
    // each lifetime independently of the map throughout the native call.
    std::vector<std::shared_ptr<Lifetime>> snapshot;
    for(const auto& [name,lifetime]:contexts_)snapshot.push_back(lifetime);
    for(const auto& lifetime:snapshot) {
        if(advance)DriveContext(lifetime);
        else lifetime->effects->Service();
    }
}
void VisualProgramPlayer::EndContexts(bool fade) {
    // Domain/model destruction cancels directly. Authored exit sequences may
    // need a living model or loaded asset and must not delay disposal.
    for(const auto& [name,lifetime]:contexts_) {
        try {if(services_.context_stopped)services_.context_stopped(name);}
        catch(const std::exception& e){status_.issues.push_back(e.what());}
        try {if(services_.cancel_timelines)services_.cancel_timelines(lifetime->token);}
        catch(const std::exception& e){status_.issues.push_back(e.what());}
        EndPartners(lifetime->effects);lifetime->effects->End(fade);retired_contexts_.push_back(lifetime->effects);
    }
    contexts_.clear();
}
void VisualProgramPlayer::ServiceSignals() {
    const std::vector<std::shared_ptr<SignalExecution>> snapshot(signals_.begin(),signals_.end());
    std::set<const Lifetime*> waiting;
    for(const auto& signal:snapshot) {
        auto queued=std::find(signals_.begin(),signals_.end(),signal);
        if(queued==signals_.end())continue;
        const auto lifetime=signal->execution.lifetime;
        if(!Current(signal->execution) || (lifetime && lifetime->phase!=ContextPhase::Active)) {
            signals_.erase(queued);continue;
        }
        if(waiting.count(lifetime.get()))continue;
        const auto& definition=program_->signals.at(signal->name);
        if(!Instructions(definition.instructions,signal->cursor,signal->execution)) {
            waiting.insert(lifetime.get());continue;
        }
        status_.completed_signal=signal->serial;
        queued=std::find(signals_.begin(),signals_.end(),signal);
        if(queued!=signals_.end())signals_.erase(queued);
    }
}
}
