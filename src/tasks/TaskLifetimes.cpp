#include <hksr/framework/tasks/TaskLifetimes.hpp>
#include <hksr/framework/presentation/scene/VisualProgramPlayer.hpp>
#include <hksr/framework/tasks/TaskContextRegistry.hpp>
#include <algorithm>
#include <deque>
#include <stdexcept>

namespace hksr::framework {
struct TaskLifetimes::State : std::enable_shared_from_this<TaskLifetimes::State> {
    enum class Phase { Entering, Active, Exiting, Draining };
    struct Job {
        std::string sequence;
        std::shared_ptr<TaskLifetimeReceipt> receipt;
        std::unique_ptr<TaskPresentation> player;
        bool entry = false, exit = false, restore = false;
        bool interrupts_with_playback = true;
        std::optional<VisualProgramParameters> replacement;
        std::optional<VisualContextBinding> binding;
        std::optional<TaskEventInput> event;
    };
    struct Lifetime {
        std::string name;
        VisualContextKey key;
        std::uint64_t token = 0;
        std::optional<VisualContextBinding> binding;
        VisualProgramParameters inputs;
        std::function<bool()> participants;
        Phase phase = Phase::Entering;
        std::deque<std::shared_ptr<Job>> jobs;
        std::vector<std::shared_ptr<Job>> retiring;
        std::vector<std::shared_ptr<TaskLifetimeReceipt>> enter, exit;
        struct Write {
            TaskLifetimeCommand command;
            std::shared_ptr<TaskLifetimeReceipt> receipt;
        };
        std::vector<Write> writes;
        std::shared_ptr<TaskEventSubscription> events;
    };
    std::uint64_t owner_generation;
    std::shared_ptr<const TaskCatalog> catalog;
    std::shared_ptr<VisualProgramPlayer> visual;
    Ports ports;
    std::function<TaskExecutionToken()> next;
    std::function<void(std::string)> report;
    ValueWritten value_written;
    std::map<VisualContextKey, std::shared_ptr<Lifetime>> current;
    std::vector<std::shared_ptr<Lifetime>> all;
    struct Group {
        std::shared_ptr<TaskLifetimeReceipt> receipt;
        std::vector<std::shared_ptr<const TaskLifetimeReceipt>> members;
    };
    std::vector<Group> groups;
    std::uint64_t next_instance = 0;
    bool servicing = false;
    std::string restore_error;
    static void Complete(const std::shared_ptr<TaskLifetimeReceipt>& receipt) {
        receipt->state.performance_complete = receipt->state.drained = true;
    }
    static void Cancel(const std::shared_ptr<TaskLifetimeReceipt>& receipt) {
        receipt->state.superseded = true;
        receipt->state.drained = true;
    }
    const TaskLifetimeCallbacks& Definition(const std::string& name) const {
        static const TaskLifetimeCallbacks empty;
        const auto& definition = catalog->contexts.at(name);
        return definition.callbacks ? *definition.callbacks : empty;
    }
    void InitializeNumbers(const Lifetime& life) {
        const auto& definition = catalog->contexts.at(life.name);
        const auto& rules = definition.numbers;
        const auto saved = visual->TaskContextNumbers(life.token);
        for (const auto& [name, value] : saved) {
            const auto found = rules.find(name);
            if (found == rules.end()) {
                const auto declared = definition.values.find(name);
                const auto expected =
                    std::holds_alternative<std::int64_t>(value) ? TaskValueType::Integer : TaskValueType::Number;
                if (declared == definition.values.end() || TypeOf(declared->second) != expected)
                    throw std::invalid_argument("saved context number is undeclared: " + name);
                ValidateContextNumber(value);
                continue;
            }
            if (ChangeContextNumber(value, {name, ContextNumberOperation::Set, value}, found->second) != value)
                throw std::invalid_argument("saved context number outside bounds: " + name);
        }
        for (const auto& [name, rule] : rules)
            if (!saved.count(name) && !visual->TaskContextValues(life.token).count(name))
                visual->SetTaskContextNumber(life.token, name, rule.initial);
    }
    void InitializeValues(const Lifetime& life) {
        const auto& definition = catalog->contexts.at(life.name);
        const auto saved = visual->TaskContextValues(life.token);
        for (const auto& [name, value] : saved) {
            const auto declared = definition.values.find(name);
            if (declared == definition.values.end() || TypeOf(value) != TypeOf(declared->second))
                throw std::invalid_argument("saved context value is undeclared or has another type: " + name);
        }
        for (const auto& [name, value] : definition.values)
            if (!saved.count(name) && !visual->TaskContextNumbers(life.token).count(name))
                visual->SetTaskContextValue(life.token, name, value);
    }
    void WriteNumber(const Lifetime& life, const ContextNumberChange& change) {
        const auto& rule = catalog->contexts.at(life.name).numbers.at(change.name);
        const auto& old_value = visual->TaskContextNumbers(life.token).at(change.name);
        visual->SetTaskContextNumber(life.token, change.name, ChangeContextNumber(old_value, change, rule));
        if (catalog->contexts.at(life.name).state_only && value_written)
            value_written(life.name, life.token, change.name);
    }
    void WriteValue(const Lifetime& life, const TaskLifetimeCommand& command) {
        if (command.value_input)
            throw std::logic_error("entry context input was not resolved: " + *command.value_input);
        if (!command.set_value) {
            WriteNumber(life, command.change.value());
            return;
        }
        const auto& assignment = *command.set_value;
        const auto& definition = catalog->contexts.at(life.name);
        auto value = assignment.value;
        if (const auto rule = definition.numbers.find(assignment.name); rule != definition.numbers.end()) {
            const auto numeric = std::visit(
                [](const auto& v) -> ContextNumber {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, std::int64_t> || std::is_same_v<T, double>)
                        return v;
                    else
                        throw std::invalid_argument("context number set has another type");
                },
                value);
            const auto changed =
                ChangeContextNumber(visual->TaskContextNumbers(life.token).at(assignment.name),
                                    {assignment.name, ContextNumberOperation::Set, numeric}, rule->second);
            value = std::visit([](const auto v) -> TaskValue { return v; }, changed);
        } else {
            const auto& declared = definition.values.at(assignment.name);
            if (TypeOf(value) != TypeOf(declared))
                throw std::invalid_argument("context set value type differs");
        }
        visual->SetTaskContextValue(life.token, assignment.name, std::move(value));
        if (definition.state_only && value_written)
            value_written(life.name, life.token, assignment.name);
    }
    void Queue(const std::shared_ptr<Lifetime>& life, const std::string& sequence,
               std::shared_ptr<TaskLifetimeReceipt> receipt, bool entry = false, bool exit = false,
               bool interrupts_with_playback = true) {
        auto job = std::make_shared<Job>();
        job->sequence = sequence;
        job->receipt = std::move(receipt);
        job->entry = entry;
        job->exit = exit;
        job->interrupts_with_playback = interrupts_with_playback;
        life->jobs.push_back(std::move(job));
    }
    void CancelJobs(const std::shared_ptr<Lifetime>& life, TaskExitReason reason) {
        for (const auto& write : life->writes)
            Cancel(write.receipt);
        life->writes.clear();
        auto jobs = std::move(life->jobs);
        life->jobs.clear();
        for (const auto& job : jobs) {
            Cancel(job->receipt);
            if (job->player) {
                job->player->Stop(reason);
                life->retiring.push_back(job);
            }
        }
    }
    void Release(const std::shared_ptr<Lifetime>& life) {
        life->events.reset();
        life->phase = Phase::Draining;
        if (life->token)
            visual->ReleaseTaskContext(life->key, life->token);
        const auto found = current.find(life->key);
        if (found != current.end() && found->second == life)
            current.erase(found);
    }
    void Subscribe(const std::shared_ptr<Lifetime>& life) {
        if (life->events || catalog->contexts.at(life->name).events.empty())
            return;
        const auto registry = visual->TaskContexts();
        const auto receiver =
            registry ? registry->Find(life->binding ? life->binding->receiver : visual->Character()) : nullptr;
        if (!receiver)
            throw std::runtime_error("visual event receiver unavailable: " + life->name);
        const auto epoch = receiver->Epoch();
        auto subscription = std::make_shared<TaskEventSubscription>();
        subscription->receive = [weak = weak_from_this(), owner = std::weak_ptr<Lifetime>(life), receiver,
                                 epoch](const TaskEventInput& input) -> std::shared_ptr<const TaskLifetimeReceipt> {
            const auto state = weak.lock();
            const auto current = owner.lock();
            if (!state || !current || !receiver->Active() || receiver->Epoch() != epoch || !input.current() ||
                current->phase == Phase::Exiting || current->phase == Phase::Draining ||
                (current->participants && !current->participants()))
                return {};
            const auto& events = state->catalog->contexts.at(current->name).events;
            const auto found = events.find(input.kind);
            if (found == events.end())
                return {};
            auto receipt = std::make_shared<TaskLifetimeReceipt>();
            receipt->generation = current->token;
            receipt->state.submitted = receipt->state.request_complete = true;
            state->Queue(current, found->second, receipt);
            current->jobs.back()->event = input;
            return receipt;
        };
        life->events = subscription;
        receiver->Subscribe(subscription);
    }
    std::uint64_t Resolve(const std::string& name, const VisualProgramParameters& inputs) const {
        auto key = name;
        std::set<std::string> visited;
        while (inputs.target_aliases.count(key)) {
            if (!visited.insert(key).second)
                throw std::invalid_argument("cyclic lifetime target alias");
            key = inputs.target_aliases.at(key);
        }
        if (key == "Caster") {
            if (!visual->Character())
                throw std::logic_error("context instance character identity unavailable");
            return visual->Character();
        }
        const auto found = inputs.targets.find(key);
        if (found == inputs.targets.end() || !found->second)
            throw std::invalid_argument("context instance requires a character identity: " + name);
        return found->second;
    }
    std::optional<VisualContextBinding> Binding(const TaskContextDefinition& definition,
                                                const VisualProgramParameters& inputs) const {
        if (!definition.instances)
            return {};
        const auto& policy = *definition.instances;
        return VisualContextBinding{policy.name, policy.category, policy.ability, Resolve(policy.receiver, inputs),
                                    Resolve(policy.caster, inputs)};
    }
    bool Matches(const Lifetime& life, const VisualContextBinding& binding, const TaskInstancePolicy& policy,
                 const TaskLifetimeCommand& command) const {
        if (!life.binding)
            return false;
        const auto& old = *life.binding;
        if (old.name != binding.name || old.category != binding.category || old.receiver != binding.receiver)
            return false;
        if (command.operation == TaskLifetimeOperation::Exit && command.only_caster)
            return !*command.only_caster || old.caster == binding.caster;
        if (policy.match != TaskInstanceMatch::Receiver && old.caster != binding.caster)
            return false;
        return policy.match != TaskInstanceMatch::ReceiverCasterAbility || old.ability == binding.ability;
    }
    std::shared_ptr<TaskLifetimeReceipt> Operate(const TaskLifetimeCommand& command,
                                                 const VisualProgramParameters& inputs,
                                                 const std::shared_ptr<Lifetime>& found,
                                                 const std::optional<VisualContextBinding>& binding,
                                                 bool interrupts_with_playback) {
        const auto& definition = Definition(command.context);
        const auto& policy = catalog->contexts.at(command.context).instances;
        auto receipt = std::make_shared<TaskLifetimeReceipt>();
        receipt->state.submitted = receipt->state.request_complete = true;
        if (command.operation == TaskLifetimeOperation::Enter) {
            if (!found || found->phase == Phase::Exiting || found->phase == Phase::Draining ||
                (policy && policy->repeat == TaskInstanceRepeat::NewInstance)) {
                auto life = std::make_shared<Lifetime>();
                life->name = command.context;
                life->inputs = inputs;
                life->binding = binding;
                if (binding)
                    life->participants = visual->BindTaskParticipants(*binding);
                life->key = {command.context, policy ? ++next_instance : 0};
                current[life->key] = life;
                all.push_back(life);
                Queue(life, definition.enter, receipt, true, false, interrupts_with_playback);
                life->enter.push_back(receipt);
            } else {
                const auto life = found;
                receipt->generation = life->token;
                if ((policy && policy->repeat == TaskInstanceRepeat::ReplaceInputs) || !definition.reapply.empty()) {
                    Queue(life, definition.reapply, receipt, false, false, interrupts_with_playback);
                    if (policy && policy->repeat == TaskInstanceRepeat::ReplaceInputs) {
                        life->jobs.back()->replacement = inputs;
                        life->jobs.back()->binding = binding;
                    }
                } else if (life->phase == Phase::Active)
                    Complete(receipt);
                else
                    life->enter.push_back(receipt);
            }
            return receipt;
        }
        if (!found) {
            if (command.operation == TaskLifetimeOperation::Exit)
                Complete(receipt);
            else
                receipt->state.error = "task context is absent: " + command.context;
            return receipt;
        }
        const auto life = found;
        receipt->generation = life->token;
        if (command.operation == TaskLifetimeOperation::ChangeValue) {
            if (life->phase == Phase::Draining) {
                Cancel(receipt);
                return receipt;
            }
            if (life->token) {
                WriteValue(*life, command);
                Complete(receipt);
            } else
                life->writes.push_back({command, receipt});
            return receipt;
        }
        if (command.operation == TaskLifetimeOperation::Signal) {
            if (life->phase == Phase::Exiting || life->phase == Phase::Draining) {
                Cancel(receipt);
                return receipt;
            }
            Queue(life, definition.signals.at(command.event), receipt, false, false, interrupts_with_playback);
            return receipt;
        }
        life->exit.push_back(receipt);
        if (life->phase != Phase::Exiting && life->phase != Phase::Draining) {
            life->events.reset();
            life->phase = Phase::Exiting;
            if (life->token)
                visual->WithdrawTaskContext(life->key, life->token);
            for (const auto& entry : life->enter)
                if (!entry->state.performance_complete)
                    Cancel(entry);
            // Removing this callback owner does not invoke the interrupted
            // skill's OnInsertAbort. The explicit exit sequence follows.
            CancelJobs(life, TaskExitReason::Deleted);
            Queue(life, definition.exit, receipt, false, true, interrupts_with_playback);
        }
        return receipt;
    }
    ContextNumber ReadNumber(const TaskContextNumberReference& reference, const VisualProgramParameters& inputs,
                             const std::shared_ptr<Lifetime>& origin) const {
        const auto& definition = catalog->contexts.at(reference.context);
        const auto binding = Binding(definition, inputs);
        std::shared_ptr<Lifetime> selected;
        const TaskLifetimeCommand read{reference.context, {}, TaskLifetimeOperation::Signal};
        if (origin && origin->name == reference.context &&
            (!binding || Matches(*origin, *binding, *definition.instances, read))) {
            const auto found = current.find(origin->key);
            if (found != current.end() && found->second == origin)
                selected = origin;
        } else
            for (const auto& [key, life] : current) {
                if (key.definition != reference.context || life->phase == Phase::Exiting ||
                    life->phase == Phase::Draining)
                    continue;
                if (life->participants && !life->participants())
                    continue;
                if (binding && !Matches(*life, *binding, *definition.instances, read))
                    continue;
                if (selected)
                    throw std::invalid_argument("number source has multiple matching instances: " + reference.context);
                selected = life;
            }
        if (!selected || !selected->token || (selected->participants && !selected->participants()))
            throw std::logic_error("number source is not initialized: " + reference.context);
        return visual->TaskContextNumbers(selected->token).at(reference.name);
    }
    std::shared_ptr<const TaskLifetimeReceipt> Submit(const TaskLifetimeCommand& input,
                                                      const VisualProgramParameters& inputs,
                                                      const std::shared_ptr<Lifetime>& origin = {},
                                                      bool interrupts_with_playback = true) {
        auto command = input;
        // Capture the operand when this command is submitted. Delayed target
        // initialization must not read a newer source generation or value.
        if (command.value_from)
            command.change.value().operand = ReadNumber(*command.value_from, inputs, origin);
        const auto& definition = catalog->contexts.at(command.context);
        const auto binding = Binding(definition, inputs);
        std::vector<std::shared_ptr<Lifetime>> matches;
        if (origin && origin->name == command.context &&
            (!binding || Matches(*origin, *binding, *definition.instances, command))) {
            const auto found = current.find(origin->key);
            if (found == current.end() || found->second != origin ||
                (origin->participants && !origin->participants())) {
                auto receipt = std::make_shared<TaskLifetimeReceipt>();
                Cancel(receipt);
                return receipt;
            }
            matches.push_back(origin);
        } else
            for (const auto& [key, life] : current) {
                if (key.definition != command.context)
                    continue;
                if (life->participants && !life->participants())
                    continue;
                if (binding && !Matches(*life, *binding, *definition.instances, command))
                    continue;
                if (binding && command.operation == TaskLifetimeOperation::Enter && life->phase != Phase::Active &&
                    life->phase != Phase::Entering)
                    continue;
                matches.push_back(life);
                if (!command.all)
                    break;
            }
        if (matches.empty())
            return Operate(command, inputs, {}, binding, interrupts_with_playback);
        if (matches.size() == 1)
            return Operate(command, inputs, matches.front(), binding, interrupts_with_playback);
        Group group;
        group.receipt = std::make_shared<TaskLifetimeReceipt>();
        group.receipt->state.submitted = group.receipt->state.request_complete = true;
        for (const auto& life : matches)
            group.members.push_back(Operate(command, inputs, life, binding, interrupts_with_playback));
        const auto receipt = group.receipt;
        groups.push_back(std::move(group));
        return receipt;
    }
    void Drive(const std::shared_ptr<Lifetime>& life, double seconds, std::uint64_t frame, double playback_rate) {
        if (life->phase != Phase::Draining && life->participants && !life->participants())
            Stop(life, TaskExitReason::ActorLost);
        const auto retiring = life->retiring;
        for (const auto& job : retiring)
            job->player->Service(seconds, frame, playback_rate);
        life->retiring.erase(std::remove_if(life->retiring.begin(), life->retiring.end(),
                                            [](const auto& job) { return job->player->Drained(); }),
                             life->retiring.end());
        if (life->phase == Phase::Draining)
            return;
        if (!life->token) {
            if (life->phase == Phase::Exiting) {
                Release(life);
                return;
            } // never constructed
            if (visual->InstanceToken(life->key))
                return; // prior singleton generation executing its exit
            life->token = visual->OpenTaskContext(life->key, life->inputs, life->binding);
            InitializeNumbers(*life);
            InitializeValues(*life);
            if (life->phase == Phase::Draining) {
                Release(life);
                return;
            }
            life->inputs = visual->TaskContextParameters(life->token);
            for (const auto& entry : life->enter)
                entry->generation = life->token;
            // Some source lifecycles publish Alive before invoking OnCreate /
            // OnStack. Admission and callback completion are separate facts.
            if (Definition(life->name).activate_before_callbacks && visual->ActivateTaskContext(life->key, life->token))
                Subscribe(life);
        }
        if (visual->InstanceToken(life->key) != life->token) {
            CancelJobs(life, TaskExitReason::ActorLost);
            Release(life);
            return;
        }
        auto writes = std::move(life->writes);
        life->writes.clear();
        for (const auto& write : writes) {
            try {
                WriteValue(*life, write.command);
                write.receipt->generation = life->token;
                Complete(write.receipt);
            } catch (const std::exception& error) {
                write.receipt->state.error = error.what();
                write.receipt->state.drained = true;
            }
        }
        if (life->jobs.empty())
            return;
        const auto job = life->jobs.front();
        if (job->event && !job->event->current()) {
            Cancel(job->receipt);
            life->jobs.pop_front();
            if (job->player) {
                job->player->Stop(TaskExitReason::ActorLost);
                life->retiring.push_back(job);
            }
            return;
        }
        job->receipt->generation = life->token;
        if (job->replacement) {
            if (!life->retiring.empty())
                return; // prior callback retains its captured inputs through drain
            auto participants = job->binding ? visual->BindTaskParticipants(*job->binding) : std::function<bool()>{};
            visual->ReplaceTaskContextInputs(life->key, life->token, *job->replacement, job->binding);
            if (life->phase != Phase::Active || life->jobs.empty() || life->jobs.front() != job)
                return;
            life->inputs = visual->TaskContextParameters(life->token);
            life->binding = job->binding;
            job->replacement.reset();
            life->participants = std::move(participants);
        }
        if (!job->player && !job->sequence.empty()) {
            auto services = ports(life->inputs);
            const auto inherited = services.condition;
            services.condition = [this, weak = std::weak_ptr<Lifetime>(life),
                                  inherited](const TaskRequirement& requirement) {
                const auto owner = weak.lock();
                if (!owner || visual->InstanceToken(owner->key) != owner->token)
                    return TaskAvailability::Unknown;
                const auto& numbers = visual->TaskContextNumbers(owner->token);
                const auto& values = visual->TaskContextValues(owner->token);
                const auto name = std::visit(
                    [](const auto& item) -> std::string {
                        using T = std::decay_t<decltype(item)>;
                        if constexpr (std::is_same_v<T, TaskValueEquals> || std::is_same_v<T, TaskNumberCompare> ||
                                      std::is_same_v<T, TaskValueOneOf>)
                            return item.name;
                        else
                            return {};
                    },
                    requirement);
                const auto source = std::visit(
                    [](const auto& item) -> std::optional<TaskValueContext> {
                        using T = std::decay_t<decltype(item)>;
                        if constexpr (std::is_same_v<T, TaskValueEquals> || std::is_same_v<T, TaskNumberCompare> ||
                                      std::is_same_v<T, TaskValueOneOf>)
                            return item.from_context;
                        else
                            return {};
                    },
                    requirement);
                const bool local = !source || (source->context == owner->name && source->catalog == catalog->id);
                if (local && (numbers.count(name) || values.count(name))) {
                    // A callback reads its exact lifetime, including exit after
                    // its public contribution was withdrawn. No newest-owner lookup.
                    TaskContext exact;
                    const auto token = exact.Enter(owner->name, catalog->id);
                    if (numbers.count(name))
                        std::visit([&](const auto value) { exact.SetValue(token, name, TaskValue(value)); },
                                   numbers.at(name));
                    else
                        exact.SetValue(token, name, values.at(name));
                    return exact.Evaluate(requirement, {}).availability;
                }
                return inherited ? inherited(requirement) : TaskAvailability::Unknown;
            };
            services.lifetime = [this, weak = std::weak_ptr<Lifetime>(life),
                                 interrupts = job->interrupts_with_playback](const auto& command, const auto& inputs) {
                const auto origin = weak.lock();
                if (!origin)
                    throw std::logic_error("callback owner retired");
                return Submit(command, inputs, origin, interrupts);
            };
            job->player = std::make_unique<TaskPresentation>(
                next(), owner_generation, catalog->sequences.at(job->sequence), visual, life->inputs,
                std::move(services), true, life->token, job->interrupts_with_playback);
            seconds = 0; // newly admitted callback has not consumed this frame's delta
        }
        job->receipt->state.started = true;
        if (job->player) {
            job->player->Service(seconds, frame, playback_rate);
            if (!job->player->Status().issues.empty())
                throw std::runtime_error(job->player->Status().issues.front());
            if (!job->player->YieldedControl())
                return;
        }
        // Nested lifecycle commands can withdraw this job while it is running.
        if (life->jobs.empty() || life->jobs.front() != job)
            return;
        life->jobs.pop_front();
        if (job->player)
            life->retiring.push_back(job);
        if (job->entry) {
            if (visual->ActivateTaskContext(life->key, life->token)) {
                life->phase = Phase::Active;
                Subscribe(life);
                for (const auto& entry : life->enter)
                    Complete(entry);
            }
        } else if (job->exit)
            Release(life);
        else
            Complete(job->receipt);
    }
    void Stop(const std::shared_ptr<Lifetime>& life, TaskExitReason reason) {
        for (const auto& entry : life->enter)
            if (!entry->state.performance_complete)
                Cancel(entry);
        for (const auto& exit : life->exit)
            if (!exit->state.performance_complete)
                Cancel(exit);
        CancelJobs(life, reason);
        for (const auto& job : life->retiring)
            job->player->Stop(reason);
        Release(life);
    }
};
TaskLifetimes::TaskLifetimes(std::uint64_t owner_generation, std::shared_ptr<const TaskCatalog> catalog,
                             std::shared_ptr<VisualProgramPlayer> visual, Ports ports,
                             std::function<TaskExecutionToken()> next, std::function<void(std::string)> report,
                             ValueWritten value_written)
    : state_(std::make_shared<State>()) {
    state_->owner_generation = owner_generation;
    state_->catalog = std::move(catalog);
    state_->visual = std::move(visual);
    state_->ports = std::move(ports);
    state_->next = std::move(next);
    state_->report = std::move(report);
    state_->value_written = std::move(value_written);
}
TaskLifetimes::~TaskLifetimes() {
    // Outstanding external event receipts must not remain pending after the
    // callback owner is destroyed. Native resource drain belongs to Stop/Service.
    for (const auto& life : state_->all) {
        life->events.reset();
        for (const auto& job : life->jobs)
            State::Cancel(job->receipt);
        for (const auto& write : life->writes)
            State::Cancel(write.receipt);
        for (const auto& entry : life->enter)
            if (!entry->state.drained)
                State::Cancel(entry);
        for (const auto& exit : life->exit)
            if (!exit->state.drained)
                State::Cancel(exit);
    }
}
std::shared_ptr<const TaskLifetimeReceipt> TaskLifetimes::Submit(const TaskLifetimeCommand& command,
                                                                 const VisualProgramParameters& inputs,
                                                                 bool interrupts_with_playback) {
    return state_->Submit(command, inputs, {}, interrupts_with_playback);
}
void TaskLifetimes::PrepareState(const std::string& name) {
    auto& state = *state_;
    const auto& definition = state.catalog->contexts.at(name);
    if (!definition.state_only)
        throw std::invalid_argument("local initialization requires a state-only context");
    if (state.visual->Restoring())
        throw std::logic_error("saved state has not been restored");
    const VisualContextKey key{name, 0};
    if (const auto found = state.current.find(key); found != state.current.end()) {
        if (found->second->token == state.visual->InstanceToken(key) && found->second->phase == State::Phase::Active)
            return;
        if (found->second->token == state.visual->InstanceToken(key))
            throw std::logic_error("state lifetime is still retiring: " + name);
        // The model consumer may have retired the entire visual scene before
        // this metadata-only lifetime was serviced. Retire that exact token;
        // it cannot release a later restored instance with the same name.
        state.Stop(found->second, TaskExitReason::ActorLost);
    }
    auto life = std::make_shared<State::Lifetime>();
    life->name = name;
    life->key = key;
    life->token = state.visual->OpenTaskContext(key, {});
    try {
        state.InitializeNumbers(*life);
        state.InitializeValues(*life);
        if (!state.visual->ActivateTaskContext(key, life->token))
            throw std::logic_error("state initialization was superseded");
        life->phase = State::Phase::Active;
        state.current.emplace(key, life);
        state.all.push_back(std::move(life));
    } catch (...) {
        state.visual->ReleaseTaskContext(key, life->token);
        throw;
    }
}
void TaskLifetimes::Service(double seconds, std::uint64_t frame, double playback_rate) {
    auto& state = *state_;
    if (state.servicing)
        return;
    state.servicing = true;
    struct Guard {
        bool& flag;
        ~Guard() {
            flag = false;
        }
    } guard{state.servicing};
    // Reconstructed stable contexts already ran their visual restore recipe;
    // adopt the owner without replaying transient creation callbacks.
    for (const auto& [key, token] : state.visual->ActiveContexts()) {
        (void)token;
        state.next_instance = (std::max)(state.next_instance, key.instance);
    }
    for (const auto& [name, token] : state.visual->ReadyContexts()) {
        state.next_instance = (std::max)(state.next_instance, name.instance);
        const auto definition = state.catalog->contexts.find(name.definition);
        if (definition == state.catalog->contexts.end() ||
            (!definition->second.callbacks && !definition->second.state_only) || state.current.count(name))
            continue;
        auto life = std::make_shared<State::Lifetime>();
        life->name = name.definition;
        life->key = name;
        life->token = state.visual->OpenTaskContext(name, {});
        life->binding = state.visual->TaskContextBinding(token);
        if (life->binding)
            life->participants = state.visual->BindTaskParticipants(*life->binding);
        life->inputs = state.visual->TaskContextParameters(token);
        life->phase = State::Phase::Active;
        try {
            state.InitializeNumbers(*life);
            state.InitializeValues(*life);
            state.Subscribe(life);
        } catch (const std::exception& error) {
            if (definition->second.callbacks && !definition->second.callbacks->restore.empty())
                state.restore_error =
                    name.definition + " restore " + definition->second.callbacks->restore + ": " + error.what();
            state.report(definition->second.callbacks && !definition->second.callbacks->restore.empty()
                             ? state.restore_error
                             : name.definition + ": " + error.what());
            state.visual->ReleaseTaskContext(name, token);
            continue;
        }
        if (definition->second.callbacks && !definition->second.callbacks->restore.empty()) {
            auto receipt = std::make_shared<TaskLifetimeReceipt>();
            receipt->state.submitted = receipt->state.request_complete = true;
            state.Queue(life, definition->second.callbacks->restore, receipt, false, false, false);
            life->jobs.back()->restore = true;
        }
        state.current[name] = life;
        state.all.push_back(life);
    }
    const auto snapshot = state.all;
    for (const auto& life : snapshot) {
        try {
            state.Drive(life, seconds, frame, playback_rate);
        } catch (const std::exception& error) {
            auto detail = life->name + ": " + error.what();
            for (const auto& job : life->jobs)
                if (job->restore)
                    detail = state.restore_error = life->name + " restore " + job->sequence + ": " + error.what();
            for (const auto& job : life->jobs)
                job->receipt->state.error = error.what();
            for (const auto& entry : life->enter)
                if (!entry->state.performance_complete)
                    entry->state.error = error.what();
            for (const auto& exit : life->exit)
                exit->state.error = error.what();
            state.report(detail);
            state.Stop(life, TaskExitReason::Failed);
        }
    }
    state.visual->Service();
    state.all.erase(std::remove_if(state.all.begin(), state.all.end(),
                                   [&](const auto& life) {
                                       if (life->phase != State::Phase::Draining || !life->retiring.empty() ||
                                           (life->token && !state.visual->TaskContextDrained(life->token)))
                                           return false;
                                       for (const auto& exit : life->exit)
                                           if (!exit->state.superseded)
                                               State::Complete(exit);
                                       return true;
                                   }),
                    state.all.end());
    state.groups.erase(std::remove_if(state.groups.begin(), state.groups.end(),
                                      [](auto& group) {
                                          bool done = true;
                                          for (const auto& member : group.members) {
                                              done = done && member->state.drained;
                                              if (!member->state.error.empty())
                                                  group.receipt->state.error = member->state.error;
                                              group.receipt->state.superseded =
                                                  group.receipt->state.superseded || member->state.superseded;
                                          }
                                          if (done) {
                                              group.receipt->state.drained = true;
                                              group.receipt->state.performance_complete =
                                                  group.receipt->state.error.empty() &&
                                                  !group.receipt->state.superseded;
                                          }
                                          return done;
                                      }),
                       state.groups.end());
}
void TaskLifetimes::InterruptCallbacks() {
    for (const auto& life : state_->all) {
        const auto retiring = life->retiring;
        for (const auto& job : retiring)
            if (job->interrupts_with_playback)
                job->player->Stop(TaskExitReason::Replaced);
        // Creation and destruction finish their own resource transactions.
        // Ordinary signals/events from the relinquished playback cannot submit
        // delayed body/transform commands after the next action takes over.
        for (auto it = life->jobs.begin(); it != life->jobs.end();) {
            const auto job = *it;
            if (job->entry || job->exit || !job->interrupts_with_playback) {
                ++it;
                continue;
            }
            State::Cancel(job->receipt);
            if (job->player) {
                job->player->Stop(TaskExitReason::Replaced);
                life->retiring.push_back(job);
            }
            it = life->jobs.erase(it);
        }
    }
}
void TaskLifetimes::Stop(TaskExitReason reason) {
    const auto all = state_->all;
    for (const auto& life : all)
        state_->Stop(life, reason);
    state_->restore_error.clear();
}
bool TaskLifetimes::Restoring() const {
    const auto& state = *state_;
    for (const auto& [key, token] : state.visual->ReadyContexts()) {
        const auto definition = state.catalog->contexts.find(key.definition);
        if (definition == state.catalog->contexts.end() || !definition->second.callbacks ||
            definition->second.callbacks->restore.empty())
            continue;
        const auto current = state.current.find(key);
        if (current == state.current.end() || current->second->token != token)
            return true;
    }
    for (const auto& life : state.all) {
        for (const auto& job : life->jobs)
            if (job->restore)
                return true;
        for (const auto& job : life->retiring)
            if (job->restore && !job->player->Drained())
                return true;
    }
    return false;
}
std::string TaskLifetimes::RestoreError() const {
    return state_->restore_error;
}
bool TaskLifetimes::Drained() const {
    return state_->all.empty();
}
bool TaskLifetimes::Idle() const {
    return std::all_of(state_->all.begin(), state_->all.end(), [](const auto& life) {
        return life->phase == State::Phase::Active && life->jobs.empty() && life->retiring.empty() &&
               life->writes.empty();
    });
}
std::vector<TaskRequestSnapshot> TaskLifetimes::Requests() const {
    std::vector<TaskRequestSnapshot> result;
    for (const auto& life : state_->all)
        for (const auto& job : life->retiring)
            if (job->player) {
                const auto rows = job->player->Requests();
                result.insert(result.end(), rows.begin(), rows.end());
            }
    for (const auto& life : state_->all)
        for (const auto& job : life->jobs)
            if (job->player) {
                const auto rows = job->player->Requests();
                result.insert(result.end(), rows.begin(), rows.end());
            }
    return result;
}
}
