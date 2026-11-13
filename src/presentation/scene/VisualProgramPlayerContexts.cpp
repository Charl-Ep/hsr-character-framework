#include <hksr/framework/presentation/scene/VisualProgramPlayer.hpp>
#include <hksr/framework/tasks/TaskContextRegistry.hpp>
#include <algorithm>
#include <stdexcept>

namespace hksr::framework {
void VisualProgramPlayer::DeclareStateContexts(std::set<std::string> names) {
    if (!contexts_.empty() && names != state_contexts_)
        throw std::logic_error("context kinds cannot change after entry");
    for (const auto& name : names) {
        const auto found = program_->contexts.find(name);
        if (found == program_->contexts.end())
            throw std::invalid_argument("state context is undeclared: " + name);
        const auto& definition = found->second;
        if (!definition.enter.empty() || !definition.exit.empty() || !definition.event_scope.empty() ||
            !definition.phase.empty() || !definition.restore || !definition.restore->empty() ||
            !definition.restore_effects.empty())
            throw std::invalid_argument("state context cannot own visual operations: " + name);
    }
    state_contexts_ = std::move(names);
}
std::function<bool()> VisualProgramPlayer::BindTaskParticipants(const VisualContextBinding& binding) const {
    auto native = services_.bind_participants ? services_.bind_participants(binding) : std::function<bool()>{};
    const auto registry = services_.task_contexts;
    const auto receiver = registry ? registry->Find(binding.receiver) : nullptr;
    const auto caster = registry ? registry->Find(binding.caster) : nullptr;
    const auto receiver_epoch = receiver ? receiver->Epoch() : 0, caster_epoch = caster ? caster->Epoch() : 0;
    return [native = std::move(native), receiver, caster, receiver_epoch, caster_epoch] {
        return (!native || native()) && (!receiver || (receiver->Active() && receiver->Epoch() == receiver_epoch)) &&
               (!caster || (caster->Active() && caster->Epoch() == caster_epoch));
    };
}
bool VisualProgramPlayer::Current(const Execution& execution) const {
    if (status_.closing || end_pending_)
        return false;
    if (!execution.lifetime)
        return !interrupt_pending_ && next_ == execution.intent;
    if (execution.lifetime->participants && !execution.lifetime->participants())
        return false;
    if (const auto scope = instruction_scopes_.find(execution.intent); scope != instruction_scopes_.end())
        if (scope->second->lifetime == execution.lifetime && scope->second->ending)
            return false;
    if (execution.lifetime->name.empty()) {
        const auto scope = instruction_scopes_.find(execution.lifetime->token);
        return scope != instruction_scopes_.end() && scope->second->lifetime == execution.lifetime &&
               !scope->second->ending;
    }
    const auto found = contexts_.find(execution.lifetime->key);
    return found != contexts_.end() && found->second == execution.lifetime;
}
std::uint64_t VisualProgramPlayer::ContextToken(const std::string& name) const {
    return InstanceToken({name});
}
std::uint64_t VisualProgramPlayer::InstanceToken(const VisualContextKey& key) const {
    const auto found = contexts_.find(key);
    return found == contexts_.end() ? 0 : found->second->token;
}
std::map<VisualContextKey, std::uint64_t> VisualProgramPlayer::ActiveContexts() const {
    std::map<VisualContextKey, std::uint64_t> out;
    for (const auto& [name, context] : contexts_)
        out.emplace(name, context->token);
    return out;
}
std::map<VisualContextKey, std::uint64_t> VisualProgramPlayer::ReadyContexts() const {
    std::map<VisualContextKey, std::uint64_t> out;
    if (status_.closing)
        return out;
    for (const auto& [name, context] : contexts_)
        if ((!end_pending_ || context->state_only) && context->phase == ContextPhase::Active &&
            (!context->participants || context->participants()))
            out.emplace(name, context->token);
    return out;
}
std::shared_ptr<const api::PhaseVisualConfigSources> VisualProgramPlayer::CurrentPhase() const {
    if (status_.closing || end_pending_)
        return {};
    std::shared_ptr<const api::PhaseVisualConfigSources> result;
    std::uint64_t latest = 0;
    for (const auto& [name, lifetime] : contexts_)
        if (lifetime->phase_data && lifetime->token > latest) {
            if (lifetime->participants && !lifetime->participants())
                continue;
            if (lifetime->binding && lifetime->binding->receiver != services_.character)
                continue;
            latest = lifetime->token;
            result = lifetime->phase_data;
        }
    return result;
}
VisualBranchState VisualProgramPlayer::BranchState() const {
    VisualBranchState out;
    if (status_.closing || end_pending_)
        return out;
    auto append = [&](const auto& choices, const auto& variables, const std::string& scope) {
        for (const auto& [key, value] : choices)
            out.values.push_back({scope, key, value});
        for (const auto& [key, value] : variables)
            out.values.push_back({scope, key, value});
    };
    if (context_ && !context_->Closing())
        append(choices_, variables_, {});
    std::uint64_t latest = 0;
    for (const auto& [name, lifetime] : contexts_) {
        if (lifetime->participants && !lifetime->participants())
            continue;
        if (!lifetime->effects->Closing()) {
            const auto scope = name.definition + (name.instance ? "#" + std::to_string(name.instance) : "");
            append(lifetime->choices, lifetime->variables, scope);
            for (const auto& [key, number] : lifetime->numbers)
                std::visit([&](const auto value) { out.values.push_back({scope, key, value}); }, number);
            for (const auto& [key, value] : lifetime->values)
                std::visit([&](const auto& item) { out.values.push_back({scope, key, item}); }, value);
        }
        // Same precedence and retirement semantics as CurrentPhase().
        if (lifetime->phase_data && lifetime->token > latest &&
            (!lifetime->binding || lifetime->binding->receiver == services_.character)) {
            latest = lifetime->token;
            out.phase = program_->contexts.at(name.definition).phase;
        }
    }
    return out;
}
std::shared_ptr<VisualEffectContext> VisualProgramPlayer::ExecutionContext(const VisualInstruction& input,
                                                                           const Execution& execution) const {
    if (input.context.empty() && execution.lifetime && execution.lifetime->state_only)
        throw std::invalid_argument("state context is not a visual resource scope: " + execution.lifetime->name);
    if (input.context.empty())
        return execution.lifetime ? execution.lifetime->effects : context_;
    if (state_contexts_.count(input.context))
        throw std::invalid_argument("state context is not a visual resource scope: " + input.context);
    if (execution.lifetime && execution.lifetime->name == input.context)
        return execution.lifetime->effects;
    const auto found = contexts_.find(input.context);
    return found == contexts_.end() ? nullptr : found->second->effects;
}
void VisualProgramPlayer::DiscardActionSignals() {
    signals_.erase(std::remove_if(signals_.begin(), signals_.end(),
                                  [](const auto& signal) { return !signal->execution.lifetime; }),
                   signals_.end());
}
std::shared_ptr<VisualProgramPlayer::Lifetime>
VisualProgramPlayer::StartContext(const VisualContextKey& key, const VisualContextSnapshot* restore,
                                  const VisualProgramParameters* inputs, std::optional<VisualContextBinding> binding) {
    const auto& name = key.definition;
    auto lifetime = std::make_shared<Lifetime>();
    lifetime->name = name;
    lifetime->key = key;
    lifetime->state_only = state_contexts_.count(name) != 0;
    lifetime->needs_movement_origin = movement_contexts_.count(name) != 0;
    lifetime->binding = restore ? restore->binding : std::move(binding);
    if (lifetime->state_only && (lifetime->binding || key.instance))
        throw std::invalid_argument("state context must belong to its local actor: " + name);
    if (lifetime->binding && !restore)
        lifetime->participants = BindTaskParticipants(*lifetime->binding);
    lifetime->inputs = restore ? restore->inputs : inputs ? *inputs : selected_inputs_;
    lifetime->token = ++next_context_;
    lifetime->choices = lifetime->inputs.choices;
    if (restore) {
        lifetime->variables = restore->variables;
        lifetime->numbers = restore->numbers;
        lifetime->restoring = true;
        lifetime->values = restore->values;
        if (!restore->effects.empty())
            lifetime->restore_steps.push_back({VisualCreate{restore->effects}, "restore.effects"});
        const auto& steps = *program_->contexts.at(name).restore;
        lifetime->restore_steps.insert(lifetime->restore_steps.end(), steps.begin(), steps.end());
    }
    lifetime->effects = std::make_shared<VisualEffectContext>(services_.effects);
    // Register ownership before any provider may reenter Select/End.
    const auto [found, inserted] = contexts_.emplace(key, lifetime);
    if (!inserted)
        throw std::logic_error("visual context instance already exists");
    try {
        const auto& phase = program_->contexts.at(name).phase;
        if (!phase.empty()) {
            if (!services_.read_phase)
                throw std::runtime_error("phase data reader unavailable");
            lifetime->phase_data = services_.read_phase(phase);
            if (!lifetime->phase_data || !lifetime->phase_data->visuals.context_available)
                throw std::runtime_error("phase configuration unavailable: " + phase);
        }
        if (!Current({lifetime->token, lifetime}))
            return lifetime;
        if (!lifetime->state_only && services_.prepare_inputs)
            services_.prepare_inputs(lifetime->effects, lifetime->inputs,
                                     restore || (!inputs && selected_inputs_restored_), lifetime->needs_movement_origin);
        if (!Current({lifetime->token, lifetime}))
            return lifetime;
        if (!lifetime->state_only && services_.context_started)
            services_.context_started(key, lifetime->token, lifetime->effects);
    } catch (...) {
        try {
            if (!lifetime->state_only && services_.context_stopped)
                services_.context_stopped(key, lifetime->token);
        } catch (const std::exception& e) {
            status_.issues.push_back(name + ": detach after failed entry: " + e.what());
        }
        if (!lifetime->state_only)
            EndPartners(lifetime->effects);
        lifetime->effects->End();
        retired_contexts_.push_back(lifetime->effects);
        const auto remaining = contexts_.find(key);
        if (remaining != contexts_.end() && remaining->second == lifetime)
            contexts_.erase(remaining);
        throw;
    }
    return lifetime;
}
VisualStepResult VisualProgramPlayer::ChangeContext(const VisualContextOperation& operation, const Execution* source,
                                                    const VisualTargetAliases& bindings) {
    auto found = contexts_.find(operation.name);
    if (operation.enter) {
        if (found == contexts_.end()) {
            if ((source && source->lifetime) || !bindings.empty()) {
                auto inputs = source && source->lifetime
                                  ? (services_.scope_inputs ? services_.scope_inputs(*source->lifetime->effects)
                                                            : source->lifetime->inputs)
                                  : selected_inputs_;
                if (source && source->lifetime)
                    inputs.choices = source->lifetime->choices;
                inputs = BindVisualParameters(inputs, bindings);
                StartContext(operation.name, nullptr, &inputs);
            } else
                StartContext(operation.name, nullptr, nullptr);
            found = contexts_.find(operation.name);
        }
        auto lifetime = found->second;
        if (lifetime->phase == ContextPhase::Exiting)
            return {VisualStepState::Pending, {}};
        DriveContext(lifetime);
        return {lifetime->phase == ContextPhase::Active ? VisualStepState::Done : VisualStepState::Pending, {}};
    }
    if (found == contexts_.end())
        return {VisualStepState::Done, {}};
    auto lifetime = found->second;
    lifetime->phase = ContextPhase::Exiting;
    lifetime->fade = operation.fade;
    DriveContext(lifetime);
    return {contexts_.count(operation.name) ? VisualStepState::Pending : VisualStepState::Done, {}};
}
void VisualProgramPlayer::DriveContext(std::shared_ptr<Lifetime> lifetime) {
    if (lifetime->task_owned)
        return;
    const Execution execution{lifetime->token, lifetime};
    if (!Current(execution))
        return;
    const auto& definition = program_->contexts.at(lifetime->name);
    if (lifetime->phase == ContextPhase::Entering) {
        if (lifetime->binding && !lifetime->participants)
            lifetime->participants = BindTaskParticipants(*lifetime->binding);
        const auto& steps = lifetime->restoring ? lifetime->restore_steps : definition.enter;
        if (lifetime->state_only || Instructions(steps, lifetime->enter, execution))
            lifetime->phase = ContextPhase::Active;
    } else if (lifetime->phase == ContextPhase::Exiting) {
        if (!Instructions(definition.exit, lifetime->exit, execution))
            return;
        try {
            if (!lifetime->state_only && services_.context_stopped)
                services_.context_stopped(lifetime->key, lifetime->token);
        } catch (const std::exception& e) {
            status_.issues.push_back(e.what());
        }
        try {
            if (!lifetime->state_only && services_.cancel_timelines)
                services_.cancel_timelines(lifetime->token);
        } catch (const std::exception& e) {
            status_.issues.push_back(e.what());
        }
        if (!lifetime->state_only)
            EndPartners(lifetime->effects);
        lifetime->effects->End(lifetime->fade);
        retired_contexts_.push_back(lifetime->effects);
        contexts_.erase(lifetime->key);
    }
}
void VisualProgramPlayer::ServiceContexts(bool advance) {
    if (!advance) {
        for (auto it = retired_task_contexts_.begin(); it != retired_task_contexts_.end();) {
            const auto token = it->first;
            const auto effects = it->second->effects;
            effects->Service();
            if (it->second->state_only) {
                it = retired_task_contexts_.erase(it);
                continue;
            }
            const bool timelines = !services_.timeline_scope_drained || services_.timeline_scope_drained(token);
            const bool partners = !services_.partner_scope_drained || services_.partner_scope_drained(effects);
            if (effects->Drained() && timelines && partners) {
                if (services_.forget_timelines)
                    services_.forget_timelines(token);
                it = retired_task_contexts_.erase(it);
            } else
                ++it;
        }
        for (auto it = retired_contexts_.begin(); it != retired_contexts_.end();) {
            (*it)->Service();
            if ((*it)->Drained())
                it = retired_contexts_.erase(it);
            else
                ++it;
        }
    }
    // Calls can queue a new body selection or close the whole player. Hold
    // each lifetime independently of the map throughout the native call.
    std::vector<std::shared_ptr<Lifetime>> snapshot;
    for (const auto& [name, lifetime] : contexts_)
        snapshot.push_back(lifetime);
    for (const auto& lifetime : snapshot) {
        if (advance)
            DriveContext(lifetime);
        else
            lifetime->effects->Service();
    }
}
void VisualProgramPlayer::EndContexts(bool fade) {
    // Model presentation retirement does not retire actor-local script values.
    // TaskController Reset/Close releases those exact state tokens explicitly;
    // closing the whole player also releases any remaining metadata.
    const auto retiring = contexts_;
    for (const auto& [name, lifetime] : retiring) {
        if (lifetime->state_only && !status_.closing)
            continue;
        const auto found = contexts_.find(name);
        if (found == contexts_.end() || found->second != lifetime)
            continue;
        contexts_.erase(found);
        if (lifetime->task_owned)
            retired_task_contexts_.emplace(lifetime->token, lifetime);
        else
            retired_contexts_.push_back(lifetime->effects);
        try {
            if (!lifetime->state_only && services_.context_stopped)
                services_.context_stopped(name, lifetime->token);
        } catch (const std::exception& e) {
            status_.issues.push_back(e.what());
        }
        try {
            if (!lifetime->state_only && services_.cancel_timelines)
                services_.cancel_timelines(lifetime->token);
        } catch (const std::exception& e) {
            status_.issues.push_back(e.what());
        }
        if (!lifetime->state_only)
            EndPartners(lifetime->effects);
        lifetime->effects->End(fade);
    }
}
std::uint64_t VisualProgramPlayer::OpenTaskContext(const VisualContextKey& name, VisualProgramParameters inputs,
                                                   std::optional<VisualContextBinding> binding) {
    if (status_.closing || (end_pending_ && !state_contexts_.count(name.definition)))
        throw std::logic_error("visual scene closing");
    if (const auto found = contexts_.find(name); found != contexts_.end()) {
        // A restored context already ran its declared reconstruction recipe.
        found->second->task_owned = true;
        return found->second->token;
    }
    const auto lifetime = StartContext(name, nullptr, &inputs, std::move(binding));
    lifetime->task_owned = true;
    return lifetime->token;
}
bool VisualProgramPlayer::ActivateTaskContext(const VisualContextKey& name, std::uint64_t token) {
    const auto found = contexts_.find(name);
    if (found == contexts_.end() || found->second->token != token || found->second->phase == ContextPhase::Exiting)
        return false;
    found->second->phase = ContextPhase::Active;
    return true;
}
void VisualProgramPlayer::WithdrawTaskContext(const VisualContextKey& name, std::uint64_t token) {
    const auto found = contexts_.find(name);
    if (found == contexts_.end() || found->second->token != token)
        return;
    found->second->phase = ContextPhase::Exiting;
    found->second->phase_data.reset();
}
void VisualProgramPlayer::ReleaseTaskContext(const VisualContextKey& name, std::uint64_t token) {
    const auto found = contexts_.find(name);
    if (found == contexts_.end() || found->second->token != token)
        return;
    const auto lifetime = found->second;
    // Retire identity before calling providers; late callbacks cannot act on a
    // later lifetime with the same authored name.
    contexts_.erase(found);
    retired_task_contexts_.emplace(token, lifetime);
    try {
        if (!lifetime->state_only && services_.context_stopped)
            services_.context_stopped(name, token);
    } catch (const std::exception& error) {
        status_.issues.push_back(error.what());
    }
    try {
        if (!lifetime->state_only && services_.cancel_timelines)
            services_.cancel_timelines(token);
    } catch (const std::exception& error) {
        status_.issues.push_back(error.what());
    }
    if (!lifetime->state_only)
        EndPartners(lifetime->effects);
    lifetime->effects->End();
}
bool VisualProgramPlayer::TaskContextDrained(std::uint64_t token) const {
    for (const auto& [name, lifetime] : contexts_)
        if (lifetime->token == token)
            return false;
    return !retired_task_contexts_.count(token);
}
VisualProgramParameters VisualProgramPlayer::TaskContextParameters(std::uint64_t token) const {
    for (const auto& [name, lifetime] : contexts_)
        if (lifetime->token == token) {
            auto inputs = !lifetime->state_only && services_.scope_inputs ? services_.scope_inputs(*lifetime->effects)
                                                                          : lifetime->inputs;
            inputs.choices = lifetime->choices;
            return inputs;
        }
    throw std::logic_error("task callback context retired");
}
const ContextNumbers& VisualProgramPlayer::TaskContextNumbers(std::uint64_t token) const {
    for (const auto& [key, life] : contexts_)
        if (life->token == token)
            return life->numbers;
    throw std::logic_error("task value context retired");
}
const ContextValues& VisualProgramPlayer::TaskContextValues(std::uint64_t token) const {
    for (const auto& [key, life] : contexts_)
        if (life->token == token)
            return life->values;
    throw std::logic_error("task value context retired");
}
void VisualProgramPlayer::SetTaskContextValue(std::uint64_t token, const std::string& name, ContextValue value) {
    if (const auto* number = std::get_if<std::int64_t>(&value)) {
        SetTaskContextNumber(token, name, *number);
        return;
    }
    if (const auto* number = std::get_if<double>(&value)) {
        SetTaskContextNumber(token, name, *number);
        return;
    }
    if (name.empty() || name.find('\0') != std::string::npos)
        throw std::invalid_argument("invalid context value name");
    for (const auto& [key, life] : contexts_)
        if (life->token == token) {
            if (life->participants && !life->participants())
                throw std::logic_error("task value participant retired");
            life->numbers.erase(name);
            life->values.insert_or_assign(name, std::move(value));
            return;
        }
    throw std::logic_error("task value context retired");
}
void VisualProgramPlayer::SetTaskContextNumber(std::uint64_t token, const std::string& name, ContextNumber value) {
    ValidateContextNumber(value);
    if (name.empty() || name.find('\0') != std::string::npos)
        throw std::invalid_argument("invalid context number name");
    for (const auto& [key, life] : contexts_)
        if (life->token == token) {
            if (life->participants && !life->participants())
                throw std::logic_error("task value participant retired");
            life->values.erase(name);
            life->numbers.insert_or_assign(name, std::move(value));
            return;
        }
    throw std::logic_error("task value context retired");
}
std::optional<VisualContextBinding> VisualProgramPlayer::TaskContextBinding(std::uint64_t token) const {
    for (const auto& [key, lifetime] : contexts_)
        if (lifetime->token == token)
            return lifetime->binding;
    throw std::logic_error("task callback context retired");
}
void VisualProgramPlayer::ReplaceTaskContextInputs(const VisualContextKey& key, std::uint64_t token,
                                                   VisualProgramParameters inputs,
                                                   std::optional<VisualContextBinding> binding) {
    const auto found = contexts_.find(key);
    if (found == contexts_.end() || found->second->token != token)
        throw std::logic_error("task callback context retired");
    const auto life = found->second;
    if (life->state_only && binding)
        throw std::invalid_argument("state context must belong to its local actor: " + key.definition);
    auto participants = binding ? BindTaskParticipants(*binding) : std::function<bool()>{};
    if (!life->state_only && services_.prepare_inputs)
        services_.prepare_inputs(life->effects, inputs, true, life->needs_movement_origin);
    if (!Current({token, life}))
        return;
    life->inputs = std::move(inputs);
    life->choices = life->inputs.choices;
    life->binding = std::move(binding);
    life->participants = std::move(participants);
}
void VisualProgramPlayer::ServiceSignals() {
    const std::vector<std::shared_ptr<SignalExecution>> snapshot(signals_.begin(), signals_.end());
    std::set<const Lifetime*> waiting;
    for (const auto& signal : snapshot) {
        auto queued = std::find(signals_.begin(), signals_.end(), signal);
        if (queued == signals_.end())
            continue;
        const auto lifetime = signal->execution.lifetime;
        if (!Current(signal->execution) || (lifetime && lifetime->phase != ContextPhase::Active)) {
            signals_.erase(queued);
            continue;
        }
        if (waiting.count(lifetime.get()))
            continue;
        const auto& definition = program_->signals.at(signal->name);
        if (!Instructions(definition.instructions, signal->cursor, signal->execution)) {
            waiting.insert(lifetime.get());
            continue;
        }
        status_.completed_signal = signal->serial;
        queued = std::find(signals_.begin(), signals_.end(), signal);
        if (queued != signals_.end())
            signals_.erase(queued);
    }
}
}
