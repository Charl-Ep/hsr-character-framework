#include <hksr/framework/presentation/VisualEffectBindings.hpp>
#include <stdexcept>

namespace hksr::framework {
namespace {
bool SameOwner(api::EntityOrigin a,api::EntityOrigin b) {
    return a.token.entity==b.token.entity && a.token.generation==b.token.generation && a.world==b.world;
}
bool SameDefinition(const api::AnimationSequenceExecution& a,const api::AnimationSequenceExecution& b) {
    return a.construction==b.construction && a.definition==b.definition && a.sequence==b.sequence &&
        a.kind==b.kind && SameOwner(a.origin,b.origin);
}
}
void VisualEffectBindings::Bind(api::AnimationSequenceExecution execution,std::shared_ptr<VisualEffectContext> context) {
    if(!context || !execution.serial || !execution.construction || !execution.definition || !execution.sequence ||
        !execution.origin.token.entity || !execution.origin.token.generation || !execution.origin.world)
        throw std::invalid_argument("visual event binding requires an observed execution and context");
    const auto current=bindings_.find(execution.serial);
    if(current!=bindings_.end()) {
        if(!SameDefinition(current->second.execution,execution) || current->second.context.lock()!=context)
            throw std::logic_error("event execution already belongs to another visual context");
        return;
    }
    // A replay supersedes the preceding binding of this definition. It does
    // not end its program context, which can still own persistent outputs.
    for(auto it=bindings_.begin();it!=bindings_.end();) {
        if(it->second.execution.sequence==execution.sequence && SameOwner(it->second.execution.origin,execution.origin))
            it=bindings_.erase(it);else ++it;
    }
    bindings_.emplace(execution.serial,Binding{execution,std::move(context)});
}
std::optional<VisualEffectId> VisualEffectBindings::Track(const std::string& key,const api::EffectSubmission& submission) {
    const auto found=bindings_.find(submission.sequence.serial);
    if(found==bindings_.end() || !submission.trigger.serial || !SameOwner(submission.origin,submission.sequence.origin) ||
        !SameDefinition(found->second.execution,submission.sequence))return {};
    const auto context=found->second.context.lock();
    if(!context){bindings_.erase(found);return {};}
    return context->Track(key,submission.manager,submission.request);
}
void VisualEffectBindings::Observe(const api::AnimationSequenceObservation& value) {
    if(value.entering)return;
    const auto found=bindings_.find(value.execution.serial);
    if(found==bindings_.end() || !SameDefinition(found->second.execution,value.execution))return;
    if(!value.continues)bindings_.erase(found);
}
void VisualEffectBindings::Collect() {
    for(auto it=bindings_.begin();it!=bindings_.end();)if(it->second.context.expired())it=bindings_.erase(it);else ++it;
}
}
