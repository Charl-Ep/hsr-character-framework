#include <hksr/framework/presentation/VisualProgram.hpp>
#include <algorithm>
#include <stdexcept>

namespace hksr::framework {
std::optional<SpatialHitEndpoint> VisualProgramPlayer::SendSequencePointHit(std::uint64_t token,const std::string& target,const HitInput& input) {
    const auto found=instruction_scopes_.find(token);
    if(found==instruction_scopes_.end() || found->second->ending || status_.closing || end_pending_)
        throw std::logic_error("point hit instruction scope ended");
    if(!found->second->error.empty())throw std::runtime_error(found->second->error);
    if(!found->second->prepared || !services_.point_hit)return {};
    return services_.point_hit(found->second->lifetime->effects,target,input);
}
std::optional<HitDelivery> VisualProgramPlayer::SendSequenceHit(std::uint64_t token,const std::string& target,const HitInput& input) {
    const auto found=instruction_scopes_.find(token);
    if(found==instruction_scopes_.end() || found->second->ending || status_.closing || end_pending_)
        throw std::logic_error("hit instruction scope ended");
    if(!services_.hit)throw std::runtime_error("owned hit target service unavailable");
    if(!found->second->error.empty())throw std::runtime_error(found->second->error);
    if(!found->second->prepared)return {};
    return services_.hit(*found->second->lifetime->effects,target,input);
}
VisualProgramParameters VisualProgramPlayer::Parameters(VisualProgramParameters overrides) const {
    auto result=services_.select_inputs?services_.select_inputs():VisualProgramParameters{{},services_.choices,{}};
    for(auto& [name,id]:overrides.targets)result.targets[name]=id;
    for(auto& [name,value]:overrides.choices)result.choices[name]=std::move(value);
    for(auto& [name,value]:overrides.anchors)result.anchors[name]=value;
    for(auto& [name,value]:overrides.position_targets)result.position_targets[name]=value;
    return result;
}
std::vector<std::string> VisualProgramPlayer::AvailableTargets(const VisualProgramParameters& inputs) const {
    const auto available=services_.available_targets?services_.available_targets(inputs):std::vector<std::string>{};
    std::vector<std::string> out;
    for(const auto& [name,value]:ResolveVisualTargetBindings(program_->target_defaults,inputs,available,services_.bound_targets))
        if(std::holds_alternative<VisualEntityTarget>(value))out.push_back(name);
    return out;
}
std::vector<std::string> VisualProgramPlayer::AvailablePositionTargets(const VisualProgramParameters& inputs) const {
    const auto available=services_.available_targets?services_.available_targets(inputs):std::vector<std::string>{};
    std::vector<std::string> out;
    for(const auto& [name,value]:ResolveVisualTargetBindings(program_->target_defaults,inputs,available,services_.bound_targets))
        if(std::holds_alternative<VisualPositionTarget>(value))out.push_back(name);
    return out;
}
std::optional<AnimationRequestEndpoint> VisualProgramPlayer::PartnerAnimation(const std::string& key) const {
    if(status_.closing || end_pending_ || !services_.partner_animation)
        throw std::runtime_error("owned partner animation service unavailable");
    return services_.partner_animation(key);
}
std::optional<MovementRequestEndpoint> VisualProgramPlayer::StartSequenceMovement(std::uint64_t token,const VisualMovement& command) {
    const auto found=instruction_scopes_.find(token);
    if(found==instruction_scopes_.end() || found->second->ending || status_.closing || end_pending_)
        throw std::logic_error("movement instruction scope ended");
    if(!services_.movement)throw std::runtime_error("owned movement service unavailable");
    if(!found->second->error.empty())throw std::runtime_error(found->second->error);
    if(!found->second->prepared)return {};
    return services_.movement(*found->second->lifetime->effects,command);
}
std::uint64_t VisualProgramPlayer::BeginSequence(VisualProgramParameters inputs,bool captured) {
    if(status_.closing || end_pending_)throw std::logic_error("visual scene closing");
    if(context_ || pending_ || action_)InterruptAction();
    auto scope=std::make_shared<InstructionScope>();
    scope->captured=captured;
    scope->lifetime=std::make_shared<Lifetime>();
    auto& lifetime=*scope->lifetime;
    lifetime.token=++next_context_;lifetime.phase=ContextPhase::Active;
    lifetime.inputs=std::move(inputs);lifetime.choices=lifetime.inputs.choices;
    lifetime.effects=std::make_shared<VisualEffectContext>(services_.effects);
    instruction_scopes_.emplace(lifetime.token,scope);
    return lifetime.token;
}
std::shared_ptr<const VisualInstructionStatus> VisualProgramPlayer::SubmitInstruction(
    std::uint64_t token,const VisualInstruction& input) {
    const auto found=instruction_scopes_.find(token);
    if(found==instruction_scopes_.end() || found->second->ending || status_.closing || end_pending_)
        throw std::logic_error("visual instruction scope ended");
    auto request=std::make_shared<InstructionRequest>();request->input=input;
    request->status=std::make_shared<VisualInstructionStatus>();
    found->second->requests.push_back(request);
    return request->status;
}
void VisualProgramPlayer::EndSequence(std::uint64_t token,bool natural) {
    const auto found=instruction_scopes_.find(token);if(found==instruction_scopes_.end())return;
    auto& scope=*found->second;
    if(scope.drained)return;
    if(!scope.ending){scope.ending=true;scope.natural=natural;}
    else if(scope.natural && !natural){scope.natural=false;scope.released=false;scope.drained=false;}
    for(const auto& request:scope.requests)if(!request->status->complete)request->status->cancelled=true;
}
void VisualProgramPlayer::CancelInstruction(std::uint64_t token,const std::shared_ptr<const VisualInstructionStatus>& status) {
    const auto found=instruction_scopes_.find(token);if(found==instruction_scopes_.end())return;
    for(const auto& request:found->second->requests)
        if(request->status==status && !status->complete)request->status->cancelled=true;
}
void VisualProgramPlayer::EndSequences() {
    for(const auto& [token,scope]:instruction_scopes_)EndSequence(token,false);
}
bool VisualProgramPlayer::SequenceDrained(std::uint64_t token) const {
    const auto found=instruction_scopes_.find(token);
    return found==instruction_scopes_.end() || found->second->drained;
}
std::string VisualProgramPlayer::SequenceError(std::uint64_t token) const {
    const auto found=instruction_scopes_.find(token);
    return found==instruction_scopes_.end()?std::string{}:found->second->error;
}
void VisualProgramPlayer::ForgetSequence(std::uint64_t token) {
    if(!SequenceDrained(token))throw std::logic_error("visual instruction scope not drained");
    if(services_.forget_timelines)services_.forget_timelines(token);
    instruction_scopes_.erase(token);
}
void VisualProgramPlayer::ServiceSequences(bool advance) {
    std::vector<std::shared_ptr<InstructionScope>> snapshot;
    for(const auto& [token,scope]:instruction_scopes_)snapshot.push_back(scope);
    for(const auto& scope:snapshot) {
        if(scope->drained)continue;
        auto lifetime=scope->lifetime;const auto token=lifetime->token;
        auto issue=[&](const std::string& text) {
            scope->error=text;
            for(const auto& request:scope->requests)if(!request->status->complete)
                request->status->issues.push_back(text);
            status_.issues.push_back("sequence="+std::to_string(token)+": "+text);
        };
        try {
            if(scope->ending && !scope->released) {
                scope->released=true; // retain ownership through reentrant providers
                if(attached_sequence_==token) {
                    attached_sequence_=0;
                    if(services_.detach)services_.detach();
                }
                EndPartners(lifetime->effects);
                if(scope->natural)lifetime->effects->Finish();
                else {
                    lifetime->effects->End();
                    if(services_.cancel_timelines)services_.cancel_timelines(token);
                }
            }
            lifetime->effects->Service();
            if(scope->ending) {
                const bool timelines=services_.timeline_scope_drained?services_.timeline_scope_drained(token):
                    !services_.timelines_drained || services_.timelines_drained();
                const bool partners=services_.partner_scope_drained?services_.partner_scope_drained(lifetime->effects):
                    !services_.partners_drained || services_.partners_drained();
                scope->drained=lifetime->effects->Drained() && timelines && partners;
                continue;
            }
            if(!advance)continue;
            if(!scope->prepared) {
                scope->prepared=true;
                auto inputs=scope->captured?VisualProgramParameters{}:
                    services_.select_inputs?services_.select_inputs():VisualProgramParameters{{},services_.choices,{}};
                if(!Current({token,lifetime}))continue;
                for(const auto& [key,value]:lifetime->inputs.targets)inputs.targets[key]=value;
                for(const auto& [key,value]:lifetime->inputs.choices)inputs.choices[key]=value;
                for(const auto& [key,value]:lifetime->inputs.anchors)inputs.anchors[key]=value;
                lifetime->inputs=std::move(inputs);lifetime->choices=lifetime->inputs.choices;
                if(services_.prepare_inputs)services_.prepare_inputs(lifetime->effects,lifetime->inputs,false);
                if(!Current({token,lifetime}))continue;
                attached_sequence_=token; // an older tail cannot detach this producer
                if(services_.attach)services_.attach(lifetime->effects);
            }
            if(!Current({token,lifetime}))continue;
            const auto requests=scope->requests;
            for(const auto& request:requests) {
                if(request->status->complete || request->status->cancelled)continue;
                const Execution execution{token,lifetime,&request->status->issues};
                const auto& input=request->input;
                if(!input.choice.empty()) {
                    const auto choice=lifetime->choices.find(input.choice);
                    if(choice==lifetime->choices.end())Issue(input,"preview input absent: "+input.choice,execution);
                    if(choice==lifetime->choices.end() || choice->second!=input.value) {
                        request->status->complete=true;continue;
                    }
                }
                VisualStepResult result;
                try{result=Execute(input,request->cursor,execution);}
                catch(const std::exception& error){result={VisualStepState::Failed,error.what()};}
                if(result.state==VisualStepState::Failed)Issue(input,result.detail,execution);
                if(!Current(execution)){request->status->cancelled=true;break;}
                request->status->complete=result.state!=VisualStepState::Pending;
            }
            // Consumers keep only value status handles, not effect owners.
            scope->requests.erase(std::remove_if(scope->requests.begin(),scope->requests.end(),[](const auto& r){
                return r->status->complete || r->status->cancelled;
            }),scope->requests.end());
        }catch(const std::exception& error) {
            issue(error.what());EndSequence(token,false);
            // Cleanup may itself be asynchronous; do not discard the scope.
            scope->released=false;
        }
    }
}
}
