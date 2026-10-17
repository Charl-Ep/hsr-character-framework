#pragma once
#include "VisualEffectContext.hpp"
#include <hksr/presentation/effects/EffectSubmissions.hpp>
#include <optional>

namespace hksr::framework {
// Associates an actual event-sequence execution with an explicitly selected
// program context. Does not choose the program from the current menu/state.
// A context may span multiple sequences and body actions; finishing a sequence
// releases its binding, not its effects. The program owns End/Service.
class VisualEffectBindings {
public:
    VisualEffectBindings()=default;
    ~VisualEffectBindings(){Clear();}
    VisualEffectBindings(const VisualEffectBindings&)=delete;
    VisualEffectBindings& operator=(const VisualEffectBindings&)=delete;
    void Bind(api::AnimationSequenceExecution,std::shared_ptr<VisualEffectContext>);
    std::optional<VisualEffectId> Track(const std::string& key,const api::EffectSubmission&);
    void Observe(const api::AnimationSequenceObservation&);
    void Clear();
    std::size_t Size() const {return bindings_.size();}
private:
    struct Binding {api::AnimationSequenceExecution execution;std::shared_ptr<VisualEffectContext> context;};
    std::map<std::uint64_t,Binding> bindings_;
    auto Erase(std::map<std::uint64_t,Binding>::iterator it) {--it->second.context->producers_;return bindings_.erase(it);}
};
}
