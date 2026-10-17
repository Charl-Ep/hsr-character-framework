#include <hksr/framework/presentation/VisualEventRoutes.hpp>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <sstream>

namespace hksr::framework {
namespace {
bool Same(api::EntityOrigin a,api::EntityOrigin b) {
    return a.token.entity==b.token.entity && a.token.generation==b.token.generation && a.world==b.world;
}
bool Matches(const VisualEventRule& rule,const api::AnimationEventDefinition& definition) {
    return rule.part==definition.part && rule.behavior==definition.behavior &&
        std::set<std::string>(rule.states.begin(),rule.states.end())==
        std::set<std::string>(definition.states.begin(),definition.states.end());
}
}
VisualEventRoutes::VisualEventRoutes(Read read,TrackSequence track,Trace trace,ReadPath path)
    :read_(std::move(read)),track_(std::move(track)),trace_(std::move(trace)),path_(std::move(path)) {
    if(!read_ || !track_)throw std::invalid_argument("visual event routes require definition and sequence services");
}
bool VisualEventRoutes::Interested(api::EntityOrigin owner) const {
    return std::any_of(rules_.begin(),rules_.end(),[&](const auto& item){return Same(item.second.owner,owner);});
}
VisualEventRoutes::RuleId VisualEventRoutes::Bind(api::EntityOrigin owner,void* receiver,VisualEventRule input,
    std::shared_ptr<VisualEffectContext> context) {
    if(!owner.token.entity || !owner.token.generation || !owner.world || !receiver || !context || input.file.empty() || input.key.empty())
        throw std::invalid_argument("visual event rule requires owner, receiver, file, key and context");
    const auto id=++next_;rules_.emplace(id,Rule{owner,receiver,std::move(input),std::move(context)});return id;
}
VisualEventRoutes::RuleId VisualEventRoutes::Declare(api::EntityOrigin owner,VisualEventRule input) {
    if(!owner.token.entity || !owner.token.generation || !owner.world || input.file.empty() || input.key.empty())
        throw std::invalid_argument("visual event declaration requires owner, file and key");
    const auto id=++next_;rules_.emplace(id,Rule{owner,nullptr,std::move(input),{},true});return id;
}
void VisualEventRoutes::Attach(RuleId id,void* receiver,std::shared_ptr<VisualEffectContext> context) {
    if(!receiver || !context || context->Closing())throw std::invalid_argument("visual event attachment requires a live receiver/context");
    auto& rule=rules_.at(id);rule.receiver=receiver;rule.context=std::move(context);
}
void VisualEventRoutes::Detach(RuleId id) {
    auto& rule=rules_.at(id);rule.receiver=nullptr;rule.context.reset();
}
void VisualEventRoutes::Unbind(RuleId id) {
    rules_.erase(id);
    for(auto it=definitions_.begin();it!=definitions_.end();)if(it->second.rule==id)it=definitions_.erase(it);else ++it;
}
void VisualEventRoutes::Before(const api::AnimationEventSource& source,const std::string& file) {
    std::vector<RuleId> candidates;
    for(const auto& [id,rule]:rules_)if(Same(rule.owner,source.origin) && rule.input.file==file &&
        (rule.declared || (rule.receiver==source.receiver && !rule.context.expired())))candidates.push_back(id);
    if(candidates.empty())return; // no config traversal for other actors/files
    const auto definitions=read_(source);
    if(trace_)for(const auto& issue:definitions.issues)trace_("visual event source index="+std::to_string(issue.index)+": "+issue.error);
    auto& selected=constructions_[source.serial];
    for(const auto& row:definitions.rows)if(row.configurations) {
        bool matched=false;
        for(auto id:candidates)if(const auto rule=rules_.find(id);rule!=rules_.end() && Matches(rule->second.input,row)) {
            selected.push_back({row.configurations,id});matched=true;
        }
        // A shared collection also used by an unselected definition cannot be
        // disambiguated by pointer equality. Preserve that negative evidence.
        if(!matched)selected.push_back({row.configurations,0});
    }
}
void VisualEventRoutes::Built(const api::AnimationEventSource& source,const api::AnimationTaskConstruction& task) {
    const auto input=constructions_.find(source.serial);
    if(input==constructions_.end() || !task.original_returned || !task.parent || !task.configurations)return;
    RuleId selected=0;
    for(const auto& entry:input->second)if(entry.configurations==task.configurations) {
        if(!entry.rule || !rules_.count(entry.rule))return;
        if(selected && selected!=entry.rule) {
            if(trace_)trace_("visual event task collection has conflicting program rules");return;
        }
        selected=entry.rule;
    }
    if(!selected)return;
    const auto tracking=track_(source,task);
    if(tracking!=api::AnimationSequenceTracking::Tracked) {
        if(trace_)trace_("visual event sequence definition="+std::to_string(task.serial)+
            (tracking==api::AnimationSequenceTracking::Unsupported?" unsupported":" unavailable"));
        return;
    }
    for(auto it=definitions_.begin();it!=definitions_.end();) {
        if(it->second.sequence==task.parent && Same(it->second.owner,source.origin))it=definitions_.erase(it);else ++it;
    }
    definitions_[{source.serial,task.serial}]={source.origin,task.parent,source.receiver,selected};
    if(trace_)trace_("visual event tracked construction="+std::to_string(source.serial)+" definition="+std::to_string(task.serial)+" rule="+std::to_string(selected));
}
void VisualEventRoutes::After(const api::AnimationEventSource& source){constructions_.erase(source.serial);}
void VisualEventRoutes::Observe(const api::AnimationSequenceObservation& value) {
    const auto& execution=value.execution;
    if(value.entering && value.operation==api::AnimationSequenceOperation::Begin && execution.serial) {
        for(auto it=executions_.begin();it!=executions_.end();) {
            if(it->first!=execution.serial && it->second.source.sequence==execution.sequence && Same(it->second.source.origin,execution.origin))
                it=executions_.erase(it);else ++it;
        }
        const auto found=definitions_.find({execution.construction,execution.definition});
        if(found!=definitions_.end() && found->second.sequence==execution.sequence && Same(found->second.owner,execution.origin)) {
            const auto rule=rules_.find(found->second.rule);
            if(rule!=rules_.end() && rule->second.receiver==found->second.receiver)if(auto context=rule->second.context.lock()) {
                // Closing contexts accept already-attributed in-flight effects,
                // but no new Begin may extend a program that has ended.
                if(!context->Closing()) {
                    bindings_.Bind(execution,context);executions_[execution.serial]={execution,rule->second.input.key,context,rule->second.input.effect_keys,{}};
                }
            }
        }
    }
    bindings_.Observe(value);
    if(!value.entering && !value.continues)executions_.erase(execution.serial);
    if(!value.entering && (value.operation==api::AnimationSequenceOperation::Dispose ||
        value.operation==api::AnimationSequenceOperation::DefinitionRetired))
        definitions_.erase({execution.construction,execution.definition});
}
std::optional<VisualEffectId> VisualEventRoutes::Submitted(const api::EffectSubmission& value) {
    auto found=executions_.find(value.sequence.serial);
    if(found==executions_.end())return {};
    const auto selector=found->second;std::string key=selector.key;
    if(!selector.effect_keys.empty()) {
        try {
            if(!path_)throw std::logic_error("effect source path reader missing");
            const auto source=path_(value.trigger);
            if(!source.read.returned)throw std::runtime_error(source.error);
            if(const auto match=selector.effect_keys.find(source.utf8);match!=selector.effect_keys.end())key=match->second;
        }catch(const std::exception& e) {
            // Label failure does not discard an already attributed receipt.
            // Context exit can still cancel it through the default group.
            if(trace_)trace_(std::string("effect source label unresolved: ")+e.what());
        }
    }
    // Source reading can call managed code; use the current execution again.
    found=executions_.find(value.sequence.serial);if(found==executions_.end())return {};
    const auto context=found->second.context.lock();
    std::optional<api::EffectRemovalOptions> removal;
    if(const auto remove=found->second.removals.find(key);remove!=found->second.removals.end())removal=remove->second;
    const auto id=bindings_.Track(key,value);
    if(id && removal && context)context->Remove(*id,*removal);
    if(trace_ && id) {
        std::ostringstream out;out<<"visual event submission execution="<<value.sequence.serial<<" key="<<key
            <<" receipt="<<*id<<" context="<<context.get()<<" cancelled="<<static_cast<bool>(removal);trace_(out.str());
    }
    return id;
}
bool VisualEventRoutes::RemoveInFlight(std::shared_ptr<VisualEffectContext> context,const std::string& key,api::EffectRemovalOptions options) {
    if(!context)return false;
    const auto receipts=context->Requests(key);bool found=!receipts.empty();
    for(const auto id:receipts)context->Remove(id,options);
    for(auto& [serial,execution]:executions_) {
        (void)serial;if(execution.context.lock()!=context)continue;
        const bool matches=execution.key==key || std::any_of(execution.effect_keys.begin(),execution.effect_keys.end(),
            [&](const auto& item){return item.second==key;});
        if(matches){execution.removals[key]=options;found=true;}
    }
    return found;
}
void VisualEventRoutes::Collect() {
    std::vector<RuleId> expired;
    for(const auto& [id,rule]:rules_)if(!rule.declared && rule.context.expired())expired.push_back(id);
    for(auto id:expired)Unbind(id);
    for(auto it=executions_.begin();it!=executions_.end();)if(it->second.context.expired())it=executions_.erase(it);else ++it;
}
void VisualEventRoutes::Clear(){rules_.clear();constructions_.clear();definitions_.clear();executions_.clear();bindings_.Clear();}
}
