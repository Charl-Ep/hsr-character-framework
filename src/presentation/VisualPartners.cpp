#include <hksr/framework/presentation/VisualPartners.hpp>
#include <hksr/entity/LifecycleHooks.hpp>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace hksr::framework {
namespace {
bool Live(api::EntityToken token) {
    return token.entity && token.generation && api::TrackedEntities().Current(token.entity).generation==token.generation &&
        !api::TrackedEntities().IsDisposed(token) && !api::TrackedEntities().IsRetiring(token);
}
}
struct VisualPartners::Entry {
    std::string key;
    std::shared_ptr<VisualEffectContext> context;
    std::unique_ptr<api::EntityCreationSession> creation;
    std::unique_ptr<api::PartnerControlSession> controls;
    api::EntityToken token;
    bool starting=true,ending=false,ready=false,named=false,name_attempted=false;
    std::string error,last_trace;
};
VisualPartners::VisualPartners(api::EntityOrigin owner,std::map<std::string,VisualPartnerInput> inputs,VisualPartnerServices services)
    :owner_(owner),inputs_(std::move(inputs)),services_(std::move(services)) {
    if(!services_.create || !services_.controls || !services_.target)throw std::invalid_argument("partner services incomplete");
}
VisualPartners::~VisualPartners()=default;
bool VisualPartners::Current(const std::shared_ptr<Entry>& entry) const {
    const auto found=entries_.find(entry->key);
    return found!=entries_.end() && found->second==entry && !entry->ending && !entry->context->Closing() && Live(owner_.token) &&
        have_frame_ && frame_.world==owner_.world && frame_.disposing.read.returned && !frame_.disposing.value;
}
void VisualPartners::End(const std::shared_ptr<VisualEffectContext>& context) {
    for(const auto& [key,entry]:entries_)if(entry->context==context) {
        (void)key;entry->ending=true;if(entry->creation)entry->creation->Remove();
    }
}
VisualStepResult VisualPartners::Execute(std::shared_ptr<VisualEffectContext> context,const VisualPartnerOperation& operation) {
    const auto op=operation;
    if(!context || context->Closing() || !Live(owner_.token))return {VisualStepState::Failed,"partner context ended"};
    if(have_frame_ && (frame_.world!=owner_.world || !frame_.disposing.read.returned || frame_.disposing.value))
        return {VisualStepState::Failed,"partner owner world is not active"};
    const auto definition=inputs_.find(op.partner);
    if(definition==inputs_.end())return {VisualStepState::Failed,"unknown partner: "+op.partner};
    auto found=entries_.find(op.partner);
    if(found!=entries_.end() && found->second->context!=context)
        return {found->second->ending?VisualStepState::Pending:VisualStepState::Failed,
            "partner belongs to another visual context: "+op.partner};
    if(std::holds_alternative<VisualPartnerRemove>(op.command)) {
        if(found!=entries_.end()) {found->second->ending=true;if(found->second->creation)found->second->creation->Remove();}
        return {VisualStepState::Done,{}};
    }
    if(std::holds_alternative<VisualPartnerCreate>(op.command) && found==entries_.end()) {
        if(!have_frame_ || frame_.world!=owner_.world || !frame_.manager.read.returned || !frame_.manager.value ||
            !frame_.disposing.read.returned || frame_.disposing.value)
            return {VisualStepState::Pending,"waiting for partner owner world"};
        auto entry=std::make_shared<Entry>();entry->key=op.partner;entry->context=context;
        entries_.emplace(op.partner,entry); // cancellation can re-enter factory
        try {
            entry->creation=services_.create();
            if(!entry->creation)throw std::runtime_error("partner factory session absent");
            if(Current(entry))entry->creation->Create({owner_.world,frame_.manager.value,owner_.token,definition->second.model,definition->second.config});
            else entry->ending=true;
        }catch(const std::exception& e){entry->error=e.what();entry->ending=true;}
        entry->starting=false;
        if(entry->ending && entry->creation)entry->creation->Remove();
        Collect(entry);found=entries_.find(op.partner);
    }
    if(found==entries_.end())return {VisualStepState::Failed,"partner was not created: "+op.partner};
    const auto entry=found->second;
    if(!entry->error.empty())return {VisualStepState::Failed,entry->error};
    if(entry->ending)return {VisualStepState::Pending,"partner retirement pending: "+op.partner};
    // Creation acceptance does not await the model. A configuration can issue
    // several independent creates before awaiting their attachment/control.
    if(std::holds_alternative<VisualPartnerCreate>(op.command))return {VisualStepState::Done,{}};
    if(!entry->ready || !entry->named)return {VisualStepState::Pending,"partner model pending: "+op.partner};
    if(!Current(entry) || !Live(entry->token))return {VisualStepState::Failed,"partner lifetime ended: "+op.partner};
    auto target=[&](const std::string& name) {
        if(name.empty())return api::EntityToken{};
        const auto value=services_.target(*context,name);
        if(!Current(entry) || !Live(entry->token) || !Live(value.token) || value.world!=owner_.world)
            throw std::runtime_error("partner target lifetime ended");
        return value.token;
    };
    const auto result=std::visit([&](const auto& command)->api::PartnerControlResult {
        using T=std::decay_t<decltype(command)>;
        if constexpr(std::is_same_v<T,VisualPartnerAttach>) {
            return entry->controls->Attach({target(command.target),command.self_point,command.target_point,
                command.disable_when_invisible,command.use_target_scale,command.visibility});
        }else if constexpr(std::is_same_v<T,VisualPartnerAnimationFollow>)return entry->controls->FollowAnimation(target(command.target));
        else if constexpr(std::is_same_v<T,VisualPartnerVisibilityFollow>)return entry->controls->FollowVisibility(target(command.target),command.flags);
        else if constexpr(std::is_same_v<T,api::PartnerVisibility>)return entry->controls->SetVisibility(command);
        else if constexpr(std::is_same_v<T,api::PartnerIdleAnimation>)return entry->controls->PlayIdle(command);
        else return {};
    },op.command);
    return {result.returned?VisualStepState::Done:VisualStepState::Failed,result.error};
}
void VisualPartners::Collect(const std::shared_ptr<Entry>& entry) {
    if(entry->starting)return;
    if(!Live(owner_.token) || entry->context->Closing())entry->ending=true;
    if(!entry->creation)return;
    if(entry->ending)entry->creation->Remove();
    entry->creation->Service(frame_);
    const auto& status=entry->creation->Status();
    if(status.removal_requested || status.closed)entry->ending=true;
    if(!status.create_started){entry->ending=true;return;}
    if(!status.create_returned) {
        if(entry->error.empty())entry->error="partner factory did not return a live owned entity";
        entry->ending=true;
    }
    entry->ready=false;
    for(const auto& entity:status.entities)if(entity.token.entity==status.factory.entity) {
        entry->token=entity.token;
        entry->ready=!entity.disposed && Live(entity.token) && entity.observation.loaded.read.returned && entity.observation.loaded.value;
        if(entity.disposed || !Live(entity.token))entry->ending=true;
    }
    if(!Current(entry) || !Live(entry->token) || frame_.world!=owner_.world || !frame_.disposing.read.returned || frame_.disposing.value)return;
    if(!entry->controls)entry->controls=services_.controls(entry->token);
    if(!entry->controls){entry->error="partner controls absent";entry->ending=true;return;}
    if(!entry->name_attempted) {
        entry->name_attempted=true;
        const auto result=entry->controls->SetMemberName(inputs_.at(entry->key).member);
        entry->named=result.returned;
        if(!result.returned){entry->error="partner member name: "+result.error;entry->ending=true;}
    }
}
void VisualPartners::Report(Entry& entry) {
    if(!services_.trace)return;
    std::ostringstream out;out<<"partner key="<<entry.key<<" owner="<<owner_.token.entity<<" generation="<<owner_.token.generation
        <<" entity="<<entry.token.entity<<" partner_generation="<<entry.token.generation<<" ready="<<entry.ready
        <<" named="<<entry.named<<" ending="<<entry.ending;
    if(!entry.error.empty())out<<" error=["<<entry.error<<"]";
    if(entry.creation) {
        out<<" closed="<<entry.creation->Status().closed;
        for(const auto& error:entry.creation->Status().errors)out<<" creation_error=["<<error<<"]";
    }
    const auto text=out.str();if(text!=entry.last_trace){entry.last_trace=text;services_.trace(text);}
}
void VisualPartners::Service(const api::WorldFrame& frame) {
    BeginFrame(frame);
    if(servicing_)return;
    servicing_=true;struct Guard{bool& flag;~Guard(){flag=false;}} guard{servicing_};
    std::vector<std::shared_ptr<Entry>> snapshot;
    for(const auto& [key,entry]:entries_){(void)key;snapshot.push_back(entry);}
    for(const auto& entry:snapshot) {
        try{Collect(entry);}catch(const std::exception& e){entry->error=e.what();entry->ending=true;}
        Report(*entry);
        if(entry->starting || !entry->ending)continue;
        const bool closed=!entry->creation || !entry->creation->Status().create_started || entry->creation->Status().closed;
        if(closed && (!entry->controls || entry->controls->Close())) {
            const auto found=entries_.find(entry->key);
            if(found!=entries_.end() && found->second==entry)entries_.erase(found);
        }
    }
}
api::EntityOrigin VisualPartners::Resolve(const std::string& key) const {
    const auto found=entries_.find(key);
    if(found==entries_.end() || !Current(found->second) || !found->second->ready || !Live(found->second->token))
        throw std::runtime_error("partner target not loaded: "+key);
    return {found->second->token,owner_.world};
}
bool VisualPartners::Drained() const {return entries_.empty();}
}
