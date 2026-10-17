#pragma once
#include "VisualEffectBindings.hpp"
#include "VisualEventRule.hpp"

namespace hksr::framework {
class VisualEventRoutes {
public:
    using RuleId=std::uint64_t;
    using Read=std::function<api::AnimationEventDefinitions(const api::AnimationEventSource&)>;
    using TrackSequence=std::function<api::AnimationSequenceTracking(const api::AnimationEventSource&,const api::AnimationTaskConstruction&)>;
    using Trace=std::function<void(const std::string&)>;
    VisualEventRoutes(Read,TrackSequence,Trace={});
    // Declare before the model constructs events. Definitions persist between
    // plays; attaching a context later does not require rebuilding the model.
    RuleId Declare(api::EntityOrigin,VisualEventRule);
    void Attach(RuleId,void* receiver,std::shared_ptr<VisualEffectContext>);
    void Detach(RuleId); // future Begin only; in-flight receipts keep their scope
    // The program explicitly supplies its entity generation, current event
    // receiver and context. No character/menu/path-to-entity inference.
    RuleId Bind(api::EntityOrigin,void* receiver,VisualEventRule,std::shared_ptr<VisualEffectContext>);
    void Unbind(RuleId); // existing executions keep their original association
    void Before(const api::AnimationEventSource&,const std::string& file);
    void Built(const api::AnimationEventSource&,const api::AnimationTaskConstruction&);
    void After(const api::AnimationEventSource&);
    void Observe(const api::AnimationSequenceObservation&);
    std::optional<VisualEffectId> Submitted(const api::EffectSubmission&);
    void Collect();
    void Clear();
private:
    struct Rule {api::EntityOrigin owner;void* receiver=nullptr;VisualEventRule input;std::weak_ptr<VisualEffectContext> context;bool declared=false;};
    struct Selection {void* configurations;RuleId rule;};
    struct Definition {api::EntityOrigin owner;void* sequence;void* receiver;RuleId rule;};
    struct Execution {api::AnimationSequenceExecution source;std::string key;std::weak_ptr<VisualEffectContext> context;};
    Read read_;TrackSequence track_;Trace trace_;
    RuleId next_=0;
    std::map<RuleId,Rule> rules_;
    std::map<std::uint64_t,std::vector<Selection>> constructions_;
    std::map<std::pair<std::uint64_t,std::uint64_t>,Definition> definitions_;
    std::map<std::uint64_t,Execution> executions_;
    VisualEffectBindings bindings_;
};
}
