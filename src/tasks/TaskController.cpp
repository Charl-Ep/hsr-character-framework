#include <hksr/framework/tasks/TaskController.hpp>
#include <hksr/framework/presentation/scene/VisualProgramPlayer.hpp>
#include <hksr/framework/tasks/TaskLifetimes.hpp>
#include <hksr/framework/tasks/TaskContextRegistry.hpp>
#include <algorithm>
#include <deque>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace hksr::framework {
struct TaskController::State {
    using StateKey = std::pair<std::string, std::string>;
    struct StateLease {
        std::uint64_t token = 0, version = 0;
    };
    struct StateTicket {
        std::map<StateKey, StateLease> keys;
        std::set<StateKey> restored;
        bool started = false, ended = false;
    };
    struct Pending {
        std::uint64_t intent;
        TaskSelection selection;
        bool finish = false;
        std::shared_ptr<StateTicket> state;
    };
    struct Run {
        TaskSelection selection;
        std::unique_ptr<TaskPresentation> player;
        std::optional<TaskContinuation> next;
        bool stopping = false;
        bool input = false;
        std::uint64_t intent = 0;
        std::shared_ptr<StateTicket> state;
    };
    struct Contribution {
        std::uint64_t visual, epoch;
        TaskContextToken logical;
        std::shared_ptr<TaskContextEndpoint> receiver;
    };
    std::uint64_t owner_generation = 0, next_execution = 0;
    std::shared_ptr<const TaskCatalog> catalog;
    std::shared_ptr<VisualProgramPlayer> visual;
    TaskPresentationServices services;
    std::shared_ptr<TaskContextEndpoint> endpoint;
    std::shared_ptr<TaskContextRegistry> registry;
    TaskContext& Context() const {
        return endpoint->Context();
    }
    std::map<VisualContextKey, Contribution> contributions;
    std::optional<Pending> pending;
    std::optional<TaskSelection> selected;
    std::shared_ptr<Run> active;
    std::deque<Pending> input_pending;
    std::shared_ptr<Run> input_active;
    std::vector<std::shared_ptr<Run>> retiring;
    std::unique_ptr<TaskLifetimes> lifetimes;
    TaskControllerStatus status;
    bool servicing = false;
    bool initial_state_ready = false;
    std::uint64_t next_state_version = 0;
    std::map<std::pair<std::uint64_t, std::string>, std::uint64_t> state_versions;
    const StateTicket* state_writer = nullptr;
    std::uint32_t next_reaction_version = 0;
    std::map<std::uint64_t, std::uint32_t> reaction_versions;
    TaskContextToken manual_reaction;
    void ClearManualReaction() {
        if (!manual_reaction.value)
            return;
        endpoint->Remove(endpoint->Epoch(), manual_reaction);
        manual_reaction = {};
    }
    std::optional<TaskVisualReaction> ManualVisualReaction() const {
        if (manual_reaction.value)
            for (const auto& owner : Context().Owners())
                if (owner.token.value == manual_reaction.value)
                    return owner.visual_reaction;
        return {};
    }
    void RestoreManualReaction(std::optional<TaskVisualReaction> value) {
        if (status.closing)
            throw std::logic_error("reaction owner is closing");
        if (value && value->source != catalog->visual->source)
            throw std::invalid_argument("reaction belongs to another character source");
        TaskContextDefinition definition;
        definition.visual_reaction = std::move(value);
        TaskContextToken next;
        if (definition.visual_reaction)
            next = endpoint->Apply("visual-reaction", definition);
        ClearManualReaction();
        manual_reaction = next;
    }
    void ApplyPlaybackEffect(const AcceptedPlaybackEffect& effect) {
        std::visit([&](const auto& accepted) {
            using T = std::decay_t<decltype(accepted)>;
            if constexpr (std::is_same_v<T, NormalBodyAccepted>)
                ClearManualReaction();
            else if constexpr (std::is_same_v<T, ReactionEntered>)
                RestoreManualReaction(TaskVisualReaction{accepted.source, accepted.flag, accepted.occurrence});
            else if constexpr (std::is_same_v<T, ReactionRecovered>) {
                const auto current = ManualVisualReaction();
                if (current && current->source == accepted.source && current->flag == accepted.flag)
                    ClearManualReaction();
            } else
                static_assert(std::is_same_v<T, void>, "unhandled accepted playback effect");
        }, effect);
    }
    TaskPresentationServices PresentationPorts() {
        auto ports = services;
        ports.accepted_playback = [this, previous = ports.accepted_playback](const auto& effect) {
            ApplyPlaybackEffect(effect);
            if (previous)
                previous(effect);
        };
        return ports;
    }
    struct StateWriteScope {
        const StateTicket*& current;
        const StateTicket* previous;
        StateWriteScope(const StateTicket*& slot, const StateTicket* ticket) : current(slot), previous(slot) {
            current = ticket;
        }
        ~StateWriteScope() {
            current = previous;
        }
    };
    std::shared_ptr<StateTicket> ReserveState(const TaskDefinition& definition) {
        auto ticket = std::make_shared<StateTicket>();
        std::set<StateKey> keys;
        for (const auto* writes : {&definition.on.accepted, &definition.on.started, &definition.on.completed,
                                   &definition.on.interrupted, &definition.on.replaced, &definition.on.failed})
            for (const auto& write : *writes)
                for (const auto& [name, value] : write.values) {
                    (void)value;
                    keys.emplace(write.context, name);
                }
        if (!definition.entry.empty())
            for (const auto& node : catalog->sequences.at(definition.entry)->nodes)
                if (const auto* submit = std::get_if<TaskSubmit>(&node.operation))
                    if (const auto* value = std::get_if<TaskLifetimeCommand>(&submit->operation);
                        value && value->operation == TaskLifetimeOperation::ChangeValue &&
                        catalog->contexts.at(value->context).state_only)
                        keys.emplace(value->context, value->set_value ? value->set_value->name : value->change->name);
        if (keys.empty())
            return {};
        // Resolve every owner first: a missing dependency must not partially
        // take over variables from a still-running execution.
        for (const auto& key : keys) {
            const auto token = visual->InstanceToken(key.first);
            if (!token)
                throw std::logic_error("task state context is not initialized: " + key.first);
            ticket->keys.emplace(key, StateLease{token, 0});
        }
        for (auto& [key, lease] : ticket->keys) {
            lease.version = ++next_state_version;
            state_versions[{lease.token, key.second}] = lease.version;
        }
        return ticket;
    }
    bool OwnsState(const StateTicket& ticket, const StateKey& key) const {
        const auto lease = ticket.keys.find(key);
        if (lease == ticket.keys.end() || visual->InstanceToken(key.first) != lease->second.token)
            return false;
        const auto current = state_versions.find({lease->second.token, key.second});
        return current != state_versions.end() && current->second == lease->second.version;
    }
    void WriteState(const std::shared_ptr<StateTicket>& ticket, const std::vector<TaskStateWrite>& writes) {
        if (!ticket)
            return;
        StateWriteScope scope(state_writer, ticket.get());
        for (const auto& write : writes)
            for (const auto& [name, value] : write.values)
                if (OwnsState(*ticket, {write.context, name})) {
                    TaskLifetimeCommand command;
                    command.context = write.context;
                    command.operation = TaskLifetimeOperation::ChangeValue;
                    command.set_value = TaskContextValueSet{name, value};
                    const auto result = lifetimes->Submit(command, {}, false);
                    if (!result->state.error.empty())
                        throw std::runtime_error(result->state.error);
                }
        SyncContexts();
    }
    void StartedState(const std::shared_ptr<Run>& run) {
        if (!run->state || run->state->started || !run->player || !run->player->Started())
            return;
        run->state->started = true;
        WriteState(run->state, catalog->Resolve(run->selection.task).definition.on.started);
    }
    void EndState(const TaskSelection& selection, const std::shared_ptr<StateTicket>& ticket, TaskExitReason reason) {
        if (!ticket || ticket->ended)
            return;
        ticket->ended = true;
        const auto& on = catalog->Resolve(selection.task).definition.on;
        const auto& writes = reason == TaskExitReason::Completed  ? on.completed
                             : reason == TaskExitReason::Replaced ? on.replaced
                             : reason == TaskExitReason::Failed   ? on.failed
                                                                  : on.interrupted;
        WriteState(ticket, writes);
    }
    bool InitializeStateContexts() {
        if (initial_state_ready || catalog->initial_contexts.empty() || status.closing || visual->Restoring())
            return false;
        for (const auto& name : catalog->initial_contexts)
            lifetimes->PrepareState(name);
        initial_state_ready = true;
        return true;
    }
    auto EventPort(const VisualProgramParameters& captured) const {
        const auto receivers = registry;
        const auto character = visual->Character();
        std::map<std::uint64_t, std::pair<std::shared_ptr<TaskContextEndpoint>, std::uint64_t>> leases;
        auto retain = [&](std::uint64_t id) {
            if (!id || !receivers)
                return;
            if (const auto endpoint = receivers->Find(id))
                leases[id] = {endpoint, endpoint->Epoch()};
        };
        retain(character);
        for (const auto& [name, id] : captured.targets) {
            (void)name;
            retain(id);
        }
        return [receivers, character, leases = std::move(leases)](
                   const TaskEventCommand& command,
                   const VisualProgramParameters& inputs) -> std::shared_ptr<const TaskEventReceipt> {
            auto target = [&](const std::string& name, bool allow_position) -> std::uint64_t {
                auto key = name;
                std::set<std::string> visited;
                while (inputs.target_aliases.count(key)) {
                    if (!visited.insert(key).second)
                        throw std::invalid_argument("cyclic event target alias");
                    key = inputs.target_aliases.at(key);
                }
                if (key == "Caster")
                    return character;
                if (const auto found = inputs.targets.find(key); found != inputs.targets.end() && found->second)
                    return found->second;
                if (allow_position && inputs.position_targets.count(key))
                    return 0;
                throw std::invalid_argument("visual event requires an owned target: " + name);
            };
            const auto receiver = target(command.recipient, command.allow_position);
            if (!receiver)
                return std::make_shared<TaskEventReceipt>(); // empty ground has no actor callbacks
            const auto source = target(command.source, false);
            if (!receivers)
                throw std::logic_error("visual event registry unavailable");
            for (const auto id : {receiver, source}) {
                const auto lease = leases.find(id);
                if (lease == leases.end() || !lease->second.first->Active() ||
                    lease->second.first->Epoch() != lease->second.second || receivers->Find(id) != lease->second.first)
                    throw std::runtime_error("visual event participant was not captured or has changed");
            }
            return receivers->Dispatch(command.kind, receiver, source);
        };
    }
    std::optional<std::uint64_t> frame;
    TaskQuery Inputs(const TaskSelection& selection, bool captured,
                     const std::vector<TaskRequirement>& requirements) const {
        auto query = selection.query;
        const auto inputs =
            captured || selection.inputs_captured ? selection.visual : visual->Parameters(selection.visual);
        query.targets.clear();
        query.position_targets.clear();
        query.choices = inputs.choices;
        query.anchors.clear();
        std::vector<std::string> needs_target;
        for (const auto& requirement : requirements)
            if (const auto* target = std::get_if<TaskNeedsTarget>(&requirement)) {
                needs_target.push_back(target->name);
            }
        if (!needs_target.empty()) {
            const auto required = VisualTargetInputs(catalog->visual->target_defaults, inputs, needs_target);
            query.targets = visual->AvailableTargets(required);
            query.position_targets = visual->AvailablePositionTargets(required);
        }
        for (const auto& [name, anchor] : inputs.anchors) {
            (void)anchor;
            query.anchors.push_back(name);
        }
        return query;
    }
    std::set<StateKey> RestoredAdmission(const TaskDefinition& definition) const {
        std::map<StateKey, TaskValue> expected;
        auto append = [&](const auto& writes) {
            for (const auto& write : writes)
                for (const auto& [name, value] : write.values)
                    expected[{write.context, name}] = value;
        };
        auto matches = [&] {
            if (expected.empty())
                return false;
            for (const auto& [key, value] : expected) {
                const auto actual = Context().Value({key.first, catalog->id}, key.second);
                if (!actual || actual->value != value)
                    return false;
            }
            return true;
        };
        std::set<StateKey> consumed;
        append(definition.on.accepted);
        if (matches())
            for (const auto& [key, value] : expected) {
                (void)value;
                consumed.insert(key);
            }
        append(definition.on.started);
        if (matches())
            for (const auto& [key, value] : expected) {
                (void)value;
                consumed.insert(key);
            }
        return consumed;
    }
    TaskSelectionResult Query(const TaskSelection& selection, bool captured = false,
                              const std::shared_ptr<StateTicket>& state_ticket = {}, bool restore = false) {
        const auto key = selection.task;
        const auto& task = catalog->Resolve(key);
        try {
            ValidateTaskSelection(selection);
            auto definition = task.definition;
            const auto consumed = restore ? RestoredAdmission(definition) : std::set<StateKey>{};
            // Accepted literal writes may deliberately make the entry guard
            // false. Admission is not repeated against its own state changes.
            if (state_ticket || !consumed.empty())
                definition.requirements.erase(
                    std::remove_if(
                        definition.requirements.begin(), definition.requirements.end(),
                        [&](const auto& requirement) {
                            return std::visit(
                                [&](const auto& predicate) {
                                    using T = std::decay_t<decltype(predicate)>;
                                    if constexpr (std::is_same_v<T, TaskValueEquals> ||
                                                  std::is_same_v<T, TaskValueOneOf> ||
                                                  std::is_same_v<T, TaskNumberCompare>) {
                                        if (!predicate.from_context || predicate.from_context->catalog != catalog->id)
                                            return false;
                                        const StateKey key{predicate.from_context->context, predicate.name};
                                        if (consumed.count(key))
                                            return true;
                                        if (!state_ticket)
                                            return false;
                                        if (!OwnsState(*state_ticket, key))
                                            return false;
                                        if (state_ticket->restored.count(key))
                                            return true;
                                        return std::any_of(definition.on.accepted.begin(), definition.on.accepted.end(),
                                                           [&](const auto& write) {
                                                               return write.context == key.first &&
                                                                      write.values.count(key.second) != 0;
                                                           });
                                    } else
                                        return false;
                                },
                                requirement);
                        }),
                    definition.requirements.end());
            auto query = Context().Query(definition, Inputs(selection, captured, task.definition.requirements));
            return {0, key, std::move(query)};
        } catch (const std::exception& error) {
            return {
                0, key, {TaskAvailability::Unknown, {}, {{TaskReasonKind::Unresolved, "visual inputs", error.what()}}}};
        }
    }
    void SyncContexts() {
        const auto live = visual->ReadyContexts();
        for (auto it = contributions.begin(); it != contributions.end();) {
            const auto found = live.find(it->first);
            if (found != live.end() && found->second == it->second.visual && it->second.receiver->Active() &&
                it->second.receiver->Epoch() == it->second.epoch) {
                ++it;
                continue;
            }
            it->second.receiver->Remove(it->second.epoch, it->second.logical);
            it = contributions.erase(it);
        }
        // Preserve actual entry order, not lexical context-name ordering.
        std::vector<std::pair<std::uint64_t, VisualContextKey>> added;
        for (const auto& [name, token] : live)
            if (!contributions.count(name) && catalog->contexts.count(name.definition)) {
                added.emplace_back(token, name);
            }
        std::sort(added.begin(), added.end());
        for (const auto& [visual_token, name] : added) {
            const auto& definition = catalog->contexts.at(name.definition);
            const auto binding = visual->TaskContextBinding(visual_token);
            auto receiver = !binding || binding->receiver == visual->Character() ? endpoint
                            : registry                                           ? registry->Find(binding->receiver)
                                                                                 : nullptr;
            if (!receiver)
                continue; // no logical consumer; visual effects retain their ordinary owner
            try {
                const auto logical = receiver->Apply(name.definition, definition, catalog->id);
                contributions.emplace(name,
                                      Contribution{visual_token, receiver->Epoch(), logical, std::move(receiver)});
            } catch (const std::exception& error) {
                // Only this projection is invalid. Retire its effects instead
                // of retrying the same foreign action key every frame.
                status.error = name.definition + ": " + error.what();
                visual->ReleaseTaskContext(name, visual_token);
            }
        }
        // Publish the current value, never the declaration's initial value.
        // Existing receiver epochs and logical tokens retain ownership.
        for (const auto& [name, owner] : contributions) {
            for (const auto& [key, value] : visual->TaskContextNumbers(owner.visual))
                std::visit(
                    [&](const auto number) {
                        owner.receiver->Context().SetValue(owner.logical, key, TaskValue(number));
                    },
                    value);
            for (const auto& [key, value] : visual->TaskContextValues(owner.visual))
                owner.receiver->Context().SetValue(owner.logical, key, value);
        }
    }
    void Publish() {
        status.pending = pending ? std::optional<TaskKey>(pending->selection.task) : std::nullopt;
        status.retiring = retiring.size();
        if (active) {
            status.current = active->selection.task;
            status.execution = active->player->Status();
        }
        status.pending_inputs = input_pending.size();
        if (input_active)
            status.input = TaskControllerStatus::Input{
                input_active->selection.task, input_active->intent, input_active->player->Status(), {}};
    }
    TaskSelectionResult Accept(TaskSelection selection, TaskSelectionResult result,
                               const std::set<StateKey>& restored = {}) {
        if (result.query.availability != TaskAvailability::Available)
            return result;
        result.intent = ++status.requested;
        const auto& definition = catalog->Resolve(result.resolved).definition;
        if (definition.mode == TaskExecutionMode::Input && !definition.entry.empty()) {
            // Input transactions commit in submission order. Capture the
            // state ownership at dispatch so an earlier queued value change
            // does not invalidate the next operation's channel versions.
            selection.task = result.resolved;
            input_pending.push_back({result.intent, std::move(selection)});
            Publish();
            return result;
        }
        auto ticket = ReserveState(definition);
        if (ticket)
            ticket->restored = restored;
        WriteState(ticket, definition.on.accepted);
        if (pending)
            EndState(pending->selection, pending->state, TaskExitReason::Replaced);
        pending = Pending{result.intent, std::move(selection), false, std::move(ticket)};
        selected = pending->selection;
        status.error.clear();
        status.blocked.reset();
        Publish();
        return result;
    }
    void Start(Pending desired, bool input = false) {
        // Preserve the acceptance receipt if input capture or player
        // construction throws before the run becomes active.
        std::shared_ptr<Run> run;
        try {
            if (!desired.selection.inputs_captured)
                desired.selection.visual = visual->Parameters(desired.selection.visual);
            desired.selection.inputs_captured = true;
            auto checked = Query(desired.selection, true, desired.state);
            if (checked.query.availability != TaskAvailability::Available) {
                EndState(desired.selection, desired.state, TaskExitReason::Failed);
                if (input) {
                    std::string error = "input is no longer available";
                    for (const auto& reason : checked.query.reasons)
                        error += "; " + reason.source + ": " + reason.detail;
                    status.input =
                        TaskControllerStatus::Input{desired.selection.task, desired.intent, {}, std::move(error)};
                } else
                    status.blocked = std::move(checked.query);
                return;
            }
            desired.selection.task = checked.resolved;
            desired.selection.query.inputs = std::move(checked.query.inputs);
            run = std::make_shared<Run>();
            run->selection = std::move(desired.selection);
            run->input = input;
            run->intent = desired.intent;
            const auto& plan = catalog->Resolve(run->selection.task);
            run->next = plan.next;
            run->state = desired.state ? desired.state : ReserveState(plan.definition);
            if (!desired.state)
                WriteState(run->state, plan.definition.on.accepted);
            auto ports = PresentationPorts();
            ports.lifetime = [this, input, weak = std::weak_ptr<Run>(run),
                              values = run->selection.query.inputs](const auto& command, const auto& inputs) {
                auto resolved = command;
                if (resolved.value_input) {
                    const auto found = values.find(*resolved.value_input);
                    if (found == values.end())
                        throw std::invalid_argument("context value input missing: " + *resolved.value_input);
                    if (!resolved.set_value)
                        throw std::logic_error("context input has no value assignment");
                    resolved.set_value->value = found->second;
                    resolved.value_input.reset();
                }
                if (resolved.operation == TaskLifetimeOperation::ChangeValue &&
                    catalog->contexts.at(resolved.context).state_only) {
                    const auto owner = weak.lock();
                    if (!owner)
                        throw std::logic_error("task state writer retired");
                    StartedState(owner);
                    const auto name = resolved.set_value ? resolved.set_value->name : resolved.change->name;
                    if (!owner->state || !OwnsState(*owner->state, {resolved.context, name})) {
                        auto skipped = std::make_shared<TaskLifetimeReceipt>();
                        skipped->state.skipped = skipped->state.drained = true;
                        skipped->state.skip_reason = "state variable is owned by a newer execution";
                        return std::shared_ptr<const TaskLifetimeReceipt>(skipped);
                    }
                }
                const auto owner = weak.lock();
                StateWriteScope scope(state_writer, owner ? owner->state.get() : nullptr);
                return lifetimes->Submit(resolved, inputs, !input);
            };
            ports.event = EventPort(run->selection.visual);
            const auto selection = run->selection;
            ports.condition = [this, selection](const auto& requirement) {
                SyncContexts();
                return Context().Evaluate(requirement, Inputs(selection, true, {requirement})).availability;
            };
            run->player = std::make_unique<TaskPresentation>(TaskExecutionToken{++next_execution}, owner_generation,
                                                             catalog->sequences.at(plan.definition.entry), visual,
                                                             run->selection.visual, std::move(ports), true, 0, !input,
                                                             catalog->Prepared(run->selection.task.name).needs_movement_origin);
            if (desired.finish)
                run->player->RequestFinish();
            if (input) {
                input_active = std::move(run);
                return;
            }
            active = std::move(run);
            status.started = desired.intent;
            status.blocked.reset();
            status.error.clear();
            selected = active->selection;
        } catch (...) {
            if (run) {
                EndState(run->selection, run->state, TaskExitReason::Failed);
            } else {
                EndState(desired.selection, desired.state, TaskExitReason::Failed);
            }
            throw;
        }
    }
    void RetireActive() {
        if (!active)
            return;
        EndState(active->selection, active->state, active->player->Status().reason.value_or(TaskExitReason::Failed));
        status.current = active->selection.task;
        status.execution = active->player->Status();
        retiring.push_back(std::move(active));
        active.reset();
    }
    void CancelPlayback(TaskExitReason reason) {
        if (pending)
            EndState(pending->selection, pending->state, reason);
        pending.reset();
        selected.reset();
        if (active && !active->stopping) {
            active->stopping = true;
            active->next.reset();
            active->player->Stop(reason);
        }
        for (const auto& run : retiring)
            if (!run->input)
                run->player->Stop(reason);
        Publish();
    }
    void StopInputs(TaskExitReason reason) {
        for (auto& request : input_pending) {
            EndState(request.selection, request.state, reason);
        }
        input_pending.clear();
        if (input_active)
            input_active->player->Stop(reason);
    }
    void Continue(const std::shared_ptr<Run>& run) {
        if (status.closing || !run->next || run->player->Status().reason != TaskExitReason::Completed)
            return;
        TaskSelection next{run->next->task, run->selection.query, run->selection.visual};
        next.inputs_captured = true;
        next.query.inputs = run->next->inputs;
        const auto& definition = catalog->Resolve(next.task).definition;
        Pending request{++status.requested, std::move(next)};
        if (definition.mode == TaskExecutionMode::Input)
            input_pending.push_front(std::move(request));
        else if (!pending) {
            selected = request.selection;
            pending = std::move(request);
        }
    }
    void ServiceInputs(double seconds, std::uint64_t sequence, bool allow_start, double playback_rate) {
        if (input_active) {
            auto run = input_active;
            run->player->Service(seconds, sequence, playback_rate);
            StartedState(run);
            if (run->player->YieldedControl()) {
                EndState(run->selection, run->state, run->player->Status().reason.value_or(TaskExitReason::Failed));
                status.input = TaskControllerStatus::Input{run->selection.task, run->intent, run->player->Status(), {}};
                Continue(run);
                retiring.push_back(run);
                input_active.reset();
            }
        }
        if (allow_start && !input_active && !input_pending.empty() && !status.closing) {
            auto request = std::move(input_pending.front());
            input_pending.pop_front();
            const auto key = request.selection.task;
            const auto intent = request.intent;
            try {
                Start(std::move(request), true);
                if (auto run = input_active) {
                    run->player->Service(0, sequence);
                    StartedState(run);
                }
            } catch (const std::exception& error) {
                status.input = TaskControllerStatus::Input{key, intent, {}, error.what()};
                if (input_active)
                    input_active->player->Stop(TaskExitReason::Failed);
            }
        }
    }
};
TaskController::TaskController(std::uint64_t owner_generation, std::shared_ptr<const TaskCatalog> catalog,
                               std::shared_ptr<VisualProgramPlayer> visual, TaskPresentationServices services) {
    if (!owner_generation || !catalog || !visual)
        throw std::invalid_argument("task controller requires owned actor, catalog and scene");
    state_ = std::make_unique<State>();
    state_->owner_generation = owner_generation;
    state_->catalog = std::move(catalog);
    state_->visual = std::move(visual);
    state_->services = std::move(services);
    std::set<std::string> state_contexts;
    for (const auto& [name, definition] : state_->catalog->contexts)
        if (definition.state_only)
            state_contexts.insert(name);
    state_->visual->DeclareStateContexts(std::move(state_contexts));
    std::set<std::string> movement_contexts;
    for (const auto& [name, definition] : state_->catalog->contexts)
        if (state_->catalog->ContextNeedsMovementOrigin(name))
            movement_contexts.insert(name);
    state_->visual->DeclareMovementContexts(std::move(movement_contexts));
    state_->endpoint = std::make_shared<TaskContextEndpoint>(state_->catalog);
    state_->registry = state_->visual->TaskContexts();
    if (state_->registry && state_->visual->Character())
        state_->registry->Register(state_->visual->Character(), state_->endpoint);
    state_->Publish();
    auto* state = state_.get();
    state->lifetimes = std::make_unique<TaskLifetimes>(
        owner_generation, state->catalog, state->visual,
        [state](const VisualProgramParameters& inputs) {
            auto ports = state->PresentationPorts();
            ports.event = state->EventPort(inputs);
            ports.condition = [state, inputs](const auto& requirement) {
                state->SyncContexts();
                TaskSelection selection;
                selection.visual = inputs;
                selection.inputs_captured = true;
                selection.query.participants = {{"body"}};
                return state->Context()
                    .Evaluate(requirement, state->Inputs(selection, true, {requirement}))
                    .availability;
            };
            return ports;
        },
        [state] { return TaskExecutionToken{++state->next_execution}; },
        [state](std::string error) { state->status.error = std::move(error); },
        [state](const std::string& context, std::uint64_t token, const std::string& name) {
            if (state->state_writer && state->OwnsState(*state->state_writer, {context, name}))
                return;
            state->state_versions[{token, name}] = ++state->next_state_version;
        });
}
TaskController::~TaskController() {
    for (const auto& [key, owner] : state_->contributions)
        owner.receiver->Remove(owner.epoch, owner.logical);
    state_->endpoint->Reset(true);
}
TaskSelectionResult TaskController::Query(const TaskSelection& selection) const {
    auto result = state_->Query(selection);
    if (state_->catalog->Resolve(result.resolved).definition.trigger == TaskTrigger::Internal) {
        result.query.availability = TaskAvailability::Unavailable;
        if (result.query.facets)
            result.query.facets->path = TaskAvailability::Unavailable;
        result.query.reasons.push_back(
            {TaskReasonKind::Condition, "trigger", "internal sequence is not an external operation"});
    }
    return result;
}
TaskSelectionResult TaskController::Select(TaskSelection selection) {
    auto& state = *state_;
    if (state.status.closing)
        throw std::logic_error("task controller is closing");
    PrepareState();
    state.SyncContexts();
    auto result = Query(selection);
    return state.Accept(std::move(selection), std::move(result));
}
void TaskController::PrepareState() {
    auto& state = *state_;
    if (state.status.closing || state.visual->Restoring())
        return;
    state.InitializeStateContexts();
    state.SyncContexts();
    state.Publish();
}
TaskSelectionResult TaskController::RestoreSelection(TaskSelection selection) {
    auto& state = *state_;
    if (state.status.closing || state.visual->Restoring() || state.lifetimes->Restoring())
        throw std::logic_error("task restoration is not ready");
    if (!state.lifetimes->RestoreError().empty())
        throw std::runtime_error(state.lifetimes->RestoreError());
    state.InitializeStateContexts();
    state.SyncContexts();
    auto result = state.Query(selection, false, {}, true);
    const auto restored = state.RestoredAdmission(state.catalog->Resolve(result.resolved).definition);
    return state.Accept(std::move(selection), std::move(result), restored);
}
void TaskController::RequestFinish() {
    if (state_->pending)
        state_->pending->finish = true;
    else if (state_->active)
        state_->active->player->RequestFinish();
}
void TaskController::Cancel() {
    state_->CancelPlayback(TaskExitReason::Interrupted);
}
void TaskController::ReleasePlayback() {
    state_->CancelPlayback(TaskExitReason::Replaced);
    state_->lifetimes->InterruptCallbacks();
}
void TaskController::Close(TaskExitReason reason) {
    if (reason == TaskExitReason::Completed || reason == TaskExitReason::Interrupted ||
        reason == TaskExitReason::Replaced)
        throw std::invalid_argument("controller close requires actor/domain retirement");
    auto& state = *state_;
    state.status.closing = true;
    state.pending.reset();
    state.selected.reset();
    if (state.active) {
        state.active->next.reset();
        state.active->player->Stop(reason);
    }
    for (const auto& run : state.retiring)
        run->player->Stop(reason);
    state.StopInputs(reason);
    state.lifetimes->Stop(reason);
    state.visual->End();
    state.SyncContexts();
    state.endpoint->Reset(true);
    state.manual_reaction = {};
    state.reaction_versions.clear();
    state.Publish();
}
void TaskController::Reset(TaskExitReason reason) {
    if (reason == TaskExitReason::Completed || reason == TaskExitReason::Interrupted ||
        reason == TaskExitReason::Replaced)
        throw std::invalid_argument("task reset requires domain/owner invalidation");
    auto& state = *state_;
    state.pending.reset();
    state.selected.reset();
    state.initial_state_ready = false;
    state.state_versions.clear();
    if (state.active) {
        state.active->next.reset();
        state.active->stopping = true;
        state.active->player->Stop(reason);
    }
    for (const auto& run : state.retiring)
        run->player->Stop(reason);
    state.StopInputs(reason);
    state.lifetimes->Stop(reason);
    state.visual->Leave();
    state.SyncContexts();
    state.endpoint->Reset();
    state.manual_reaction = {};
    state.reaction_versions.clear();
    state.Publish();
}
void TaskController::Service(double seconds, std::uint64_t frame, bool allow_start, bool allow_inputs,
                             double playback_rate) {
    auto& state = *state_;
    if (state.servicing || state.frame == frame)
        return;
    state.servicing = true;
    struct Guard {
        bool& value;
        ~Guard() {
            value = false;
        }
    } guard{state.servicing};
    state.frame = frame;
    try {
        state.SyncContexts();
        if ((allow_start || allow_inputs) && !state.visual->Restoring()) {
            if (state.InitializeStateContexts())
                state.SyncContexts();
        }
        if (state.pending && state.active && !state.active->stopping) {
            state.active->stopping = true;
            state.active->next.reset();
            state.active->player->Stop(TaskExitReason::Replaced);
        }
        const auto old = state.retiring;
        for (const auto& run : old)
            run->player->Service(seconds, frame, playback_rate);
        if (auto run = state.active)
            run->player->Service(seconds, frame, playback_rate);
        state.visual->Service();
        state.SyncContexts();
        if (state.active)
            state.StartedState(state.active);
        if (state.active && state.active->player->YieldedControl()) {
            const auto run = state.active;
            if (!state.pending)
                state.Continue(run);
            state.RetireActive();
        }
        const bool restored = !state.visual->Restoring() && !state.lifetimes->Restoring();
        if (allow_start && restored && !state.active && state.pending && !state.status.closing) {
            auto desired = std::move(*state.pending);
            state.pending.reset();
            state.Start(std::move(desired));
            // A successor starts with no guessed carry-over time. It can
            // submit now; no recursive same-frame chain of completed tasks.
            if (auto run = state.active) {
                run->player->Service(0, frame);
                state.StartedState(run);
            }
            state.SyncContexts();
        }
        state.ServiceInputs(seconds, frame, allow_inputs && restored, playback_rate);
        state.lifetimes->Service(seconds, frame, playback_rate);
        state.SyncContexts();
        state.retiring.erase(
            std::remove_if(state.retiring.begin(), state.retiring.end(),
                           [&](const auto& run) {
                               if (!run->player->Drained())
                                   return false;
                               // RetireActive captured the last execution while its resources were
                               // still draining. Publish its final receipt before dropping it; an
                               // older tail must not overwrite a newer active execution's status.
                               if (!run->input && !state.active && state.status.execution &&
                                   state.status.execution->token.value == run->player->Status().token.value) {
                                   state.status.execution = run->player->Status();
                                   state.status.execution->retained_requests = 0;
                               }
                               if (run->input && state.status.input && state.status.input->intent == run->intent) {
                                   state.status.input->execution = run->player->Status();
                                   state.status.input->execution->retained_requests = 0;
                               }
                               return true;
                           }),
            state.retiring.end());
    } catch (const std::exception& error) {
        state.status.error = error.what();
        if (state.pending)
            state.EndState(state.pending->selection, state.pending->state, TaskExitReason::Failed);
        state.pending.reset();
        if (state.active) {
            state.active->next.reset();
            state.active->player->Stop(TaskExitReason::Failed);
        }
    }
    state.Publish();
}
TaskContext& TaskController::Context() {
    return state_->Context();
}
const TaskContext& TaskController::Context() const {
    return state_->Context();
}
const TaskControllerStatus& TaskController::Status() const {
    return state_->status;
}
HitControlInputs TaskController::ReactionState() const {
    HitControlInputs out;
    out.resilience = state_->Context().Resilience();
    if (const auto reaction = state_->Context().VisualReaction()) {
        out.visual_flag = reaction->flag;
        out.visual_source = reaction->source;
        out.visual_occurrence = reaction->occurrence;
    }
    std::set<std::uint64_t> live;
    for (const auto& context : state_->Context().Owners())
        if (!context.hit_effects.empty()) {
            live.insert(context.token.value);
            auto [version, added] = state_->reaction_versions.try_emplace(context.token.value, 0);
            if (added)
                version->second = ++state_->next_reaction_version;
            for (auto effect : context.hit_effects) {
                effect.version = version->second;
                out.effects.push_back(std::move(effect));
            }
        }
    for (auto it = state_->reaction_versions.begin(); it != state_->reaction_versions.end();)
        if (!live.count(it->first))
            it = state_->reaction_versions.erase(it);
        else
            ++it;
    return out;
}
void TaskController::ApplyPlaybackEffect(const AcceptedPlaybackEffect& value) {
    state_->ApplyPlaybackEffect(value);
}
void TaskController::RestoreManualReaction(std::optional<TaskVisualReaction> value) {
    state_->RestoreManualReaction(std::move(value));
}
std::optional<TaskVisualReaction> TaskController::ManualVisualReaction() const {
    return state_->ManualVisualReaction();
}
std::optional<TaskSelection> TaskController::Selection() const {
    return state_->selected;
}
VisualStepResult TaskController::RestoreInputsReady(const TaskSelection& selection) const {
    const auto error = state_->lifetimes->RestoreError();
    if (!error.empty())
        return {VisualStepState::Failed, error};
    if (state_->visual->Restoring())
        return {VisualStepState::Pending, "saved visual contexts are restoring"};
    if (state_->lifetimes->Restoring())
        return {VisualStepState::Pending, "saved context restore callbacks are pending"};
    std::vector<std::string> names;
    const auto& definition = state_->catalog->Resolve(selection.task).definition;
    for (const auto& requirement : definition.requirements)
        if (const auto* target = std::get_if<TaskNeedsTarget>(&requirement))
            names.push_back(target->name);
    if (!definition.entry.empty())
        for (const auto& node : state_->catalog->sequences.at(definition.entry)->nodes)
            if (const auto* group = std::get_if<TaskForTargets>(&node.operation)) {
                const auto supplied = selection.visual.target_collections.find(group->collection);
                const auto& members =
                    supplied == selection.visual.target_collections.end() ? group->targets : supplied->second;
                names.insert(names.end(), members.begin(), members.end());
            }
    const auto required = VisualTargetInputs(state_->catalog->visual->target_defaults, selection.visual, names);
    return state_->visual->RestoreInputsReady(required);
}
std::vector<TaskRequestSnapshot> TaskController::Requests() const {
    auto out = state_->active ? state_->active->player->Requests() : std::vector<TaskRequestSnapshot>{};
    const auto callbacks = state_->lifetimes->Requests();
    out.insert(out.end(), callbacks.begin(), callbacks.end());
    if (state_->input_active) {
        const auto rows = state_->input_active->player->Requests();
        out.insert(out.end(), rows.begin(), rows.end());
    }
    for (const auto& run : state_->retiring) {
        const auto rows = run->player->Requests();
        out.insert(out.end(), rows.begin(), rows.end());
    }
    return out;
}
bool TaskController::Drained() const {
    return !state_->pending && state_->input_pending.empty() &&
           (!state_->input_active || state_->input_active->player->Drained()) &&
           (!state_->active || state_->active->player->Drained()) && state_->retiring.empty() &&
           state_->lifetimes->Idle() &&
           (!state_->status.closing || (state_->visual->Drained() && state_->lifetimes->Drained()));
}
}
