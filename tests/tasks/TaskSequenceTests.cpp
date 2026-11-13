#include <hksr/framework/tasks/TaskSequence.hpp>
#include <stdexcept>
#include <algorithm>

namespace {
using namespace hksr::framework;
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
TaskNode Submit(const char* key, const char* actor = "body") {
    return {key, TaskSubmit{key, TaskAnimationCommand{{actor}, key, 0, 0}}};
}
struct Host {
    std::vector<TaskSubmission> requests;
    std::vector<std::pair<TaskRequestReceipt, TaskExitReason>> releases;
    bool immediate = false, drain = true;
    std::function<void()> on_submit, on_release;
    TaskSequenceServices Services() {
        TaskSequenceServices result;
        result.submit = [&](TaskExecutionToken execution, std::uint64_t serial, const TaskSubmit& node) {
            const auto& animation = std::get<TaskAnimationCommand>(node.operation);
            TaskSubmission value;
            value.receipt = {execution, serial, 42, animation.participant, 100};
            value.state.submitted = true;
            value.state.request_complete = true;
            value.state.performance_complete = immediate;
            value.state.drained = drain;
            requests.push_back(value);
            if (on_submit)
                on_submit();
            return value;
        };
        result.release = [&](const auto& receipt, auto reason) {
            releases.push_back({receipt, reason});
            if (on_release)
                on_release();
        };
        return result;
    }
};
std::shared_ptr<TaskSequence> Plan(std::initializer_list<TaskNode> nodes, TaskNodeIndex entry) {
    TaskSequenceInput input;
    input.nodes = nodes;
    input.entry = entry;
    return std::make_shared<TaskSequence>(input);
}
void Clocks() {
    Host host;
    host.immediate = true;
    auto plan = Plan({{"first", TaskWaitSeconds{0.2}},
                      {"second", TaskWaitSeconds{0.3}},
                      Submit("done"),
                      {"serial", TaskSerial{{0, 1, 2}}}},
                     3);
    TaskSequenceExecution run({1}, plan, host.Services());
    run.Advance(0.5, 1);
    Check(run.Drained() && host.requests.size() == 1 && host.releases.size() == 1,
          "serial waits did not pass remaining time");
    auto parallel = Plan({{"long", TaskWaitSeconds{0.5}},
                          {"short", TaskWaitSeconds{0.2}},
                          {"both", TaskParallel{{0, 1}}},
                          {"after", TaskWaitSeconds{0.25}},
                          Submit("end"),
                          {"serial", TaskSerial{{2, 3, 4}}}},
                         5);
    TaskSequenceExecution branches({2}, parallel, host.Services());
    branches.Advance(0.6, 1);
    Check(host.requests.size() == 1, "parallel remainder used shortest branch instead of longest");
    branches.Advance(0.2, 2);
    Check(branches.Drained() && host.requests.size() == 2, "parallel branches consumed each other's time");
    auto frame = Plan({{"frame", TaskWaitFrame{}},
                       {"second", TaskWaitSeconds{0.1}},
                       Submit("after-frame"),
                       {"serial", TaskSerial{{0, 1, 2}}}},
                      3);
    TaskSequenceExecution boundary({3}, frame, host.Services());
    boundary.Advance(1, 1);
    boundary.Advance(1, 1);
    boundary.Advance(1, 2);
    Check(host.requests.size() == 2, "frame wait invented a sub-frame completion time");
    boundary.Advance(0.1, 3);
    Check(boundary.Drained() && host.requests.size() == 3, "frame successor failed");
    Host mixed;
    mixed.immediate = true;
    auto clocks = Plan({{"first", TaskWaitSeconds{1}},
                        {"second", TaskWaitSeconds{1}},
                        Submit("mixed"),
                        {"root", TaskSerial{{0, 1, 2}}}},
                       3);
    TaskSequenceExecution rate({105}, clocks, mixed.Services());
    rate.Advance(.75, 1, 2);
    Check(mixed.requests.empty(), "serial waits completed before their scaled duration");
    rate.Advance(.25, 2, 2);
    Check(rate.Drained() && mixed.requests.size() == 1, "scaled wait remainder lost frame time");
    Host changing;
    changing.immediate = true;
    TaskSequenceExecution change({106},
                                 Plan({{"wait", TaskWaitSeconds{2}}, Submit("after"), {"root", TaskSerial{{0, 1}}}}, 2),
                                 changing.Services());
    change.Advance(.5, 1, 1);
    change.Advance(.75, 2, 2);
    Check(change.Drained() && changing.requests.size() == 1,
          "rate change restarted or failed to accelerate existing wait");
}
void Receipts() {
    Host host;
    host.drain = false;
    auto plan = Plan({Submit("body"),
                      Submit("prop", "prop"),
                      {"body-wait", TaskWaitRequest{"body"}},
                      {"prop-wait", TaskWaitRequest{"prop"}},
                      {"both", TaskParallel{{2, 3}}},
                      {"after", TaskWaitSeconds{0.1}},
                      Submit("end"),
                      {"serial", TaskSerial{{0, 1, 4, 5, 6}}}},
                     7);
    TaskSequenceExecution run({7}, plan, host.Services());
    run.Advance(0.5, 1);
    Check(host.requests.size() == 2 && !run.Drained(), "independent requests were not submitted");
    TaskRequestState done;
    done.performance_complete = true;
    auto wrong = host.requests[0].receipt;
    wrong.model = 43;
    Check(!run.Observe(wrong, done), "old model receipt was accepted");
    wrong = host.requests[0].receipt;
    wrong.execution = {8};
    Check(!run.Observe(wrong, done), "other execution receipt was accepted");
    wrong = host.requests[0].receipt;
    wrong.entity_generation = 101;
    Check(!run.Observe(wrong, done), "other entity receipt was accepted");
    Check(run.Observe(host.requests[0].receipt, done), "body observation rejected");
    run.Advance(0.5, 2);
    Check(host.requests.size() == 2, "body progress completed prop wait");
    run.Observe(host.requests[1].receipt, done);
    run.Advance(0.5, 3);
    Check(host.requests.size() == 2, "asynchronous wait invented remaining frame time");
    run.Advance(0.1, 4);
    Check(host.requests.size() == 3 && run.Status().phase == TaskExecutionPhase::Draining,
          "completion ignored resource drain");
    Check(host.releases.size() == 3 && host.releases[0].second == TaskExitReason::Completed,
          "natural finish did not release producers");
    run.Stop(TaskExitReason::Deleted);
    run.Stop(TaskExitReason::Deleted);
    Check(host.releases.size() == 6, "tail cancellation missing or duplicated");
    done.drained = true;
    for (const auto& request : host.requests)
        run.Observe(request.receipt, done);
    Check(run.Drained(), "late drain receipt was lost");
    // Queueing before a model is ready is not a failed native call. Bind the
    // actual model on its first observation; it cannot change afterwards.
    auto services = host.Services();
    auto submit = services.submit;
    services.submit = [&](auto token, auto serial, const auto& node) {
        auto value = submit(token, serial, node);
        value.receipt.model = 0;
        value.state = {};
        return value;
    };
    auto queued_plan = Plan({Submit("queued"),
                             {"submitted", TaskWaitRequest{"queued", TaskWaitSignal::Submitted}},
                             {"entry", TaskSerial{{0, 1}}}},
                            2);
    TaskSequenceExecution queued({8}, queued_plan, services);
    queued.Advance(0, 1);
    Check(queued.Status().issues.empty() && queued.Status().phase == TaskExecutionPhase::Running,
          "queued request incorrectly rejected");
    auto receipt = host.requests.back().receipt;
    TaskRequestState accepted;
    accepted.submitted = true;
    Check(queued.Observe(receipt, accepted), "first actual model binding rejected");
    auto replaced = receipt;
    replaced.model = 99;
    Check(!queued.Observe(replaced, accepted), "request rebound to a replacement model");
    queued.Advance(0, 2);
    Check(queued.Status().phase == TaskExecutionPhase::Draining, "queued submit did not complete");
    accepted.drained = true;
    queued.Observe(receipt, accepted);
    Check(queued.Drained(), "queued request did not drain");
    auto producer_later = Plan({{"wait", TaskWaitRequest{"parallel-submit", TaskWaitSignal::Submitted}},
                                Submit("parallel-submit"),
                                {"parallel", TaskParallel{{0, 1}}}},
                               2);
    TaskSequenceExecution forward({9}, producer_later, host.Services());
    forward.Advance(0, 1);
    Check(forward.Status().issues.empty(), "parallel request wait depended on vector traversal order");
    forward.Advance(0, 2);
    Check(forward.Status().phase == TaskExecutionPhase::Draining, "parallel request was not observed after submission");
    done.drained = true;
    forward.Observe(host.requests.back().receipt, done);
    Check(forward.Drained(), "forward request did not drain");
}
void Loops() {
    Host host;
    host.immediate = true;
    auto plan = Plan({{"interval", TaskWaitSeconds{0.1}},
                      Submit("repeat"),
                      {"body", TaskSerial{{0, 1}}},
                      {"loop", TaskRepeat{2, 3, false}}},
                     3);
    TaskSequenceExecution finite({10}, plan, host.Services());
    finite.Advance(0.35, 1);
    Check(finite.Drained() && host.requests.size() == 3, "finite loop discarded frame remainder");
    Check(finite.Status().retained_requests == 1, "drained earlier loop requests accumulated");
    auto ongoing = Plan({{"interval", TaskWaitSeconds{0.1}},
                         Submit("iteration"),
                         {"body", TaskSerial{{0, 1}}},
                         {"loop", TaskRepeat{2, {}, true}},
                         Submit("outro"),
                         {"entry", TaskSerial{{3, 4}}}},
                        5);
    TaskSequenceExecution continuous({11}, ongoing, host.Services());
    continuous.Advance(0.05, 1);
    continuous.RequestFinish();
    continuous.Advance(0.1, 2);
    Check(continuous.Drained() && host.requests.size() == 5, "finish did not complete iteration then outro");
    auto invalid = Plan({{"empty", TaskSerial{}}, {"loop", TaskRepeat{0, {}, true}}}, 1);
    TaskSequenceExecution no_progress({12}, invalid, host.Services());
    no_progress.Advance(0, 1);
    Check(no_progress.Status().reason == TaskExitReason::Failed && no_progress.Status().issues.size() == 1,
          "immediate unbounded loop was not diagnosed");
    // Cooperative budget never truncates a finite sequence.
    bool yield = false;
    auto services = host.Services();
    services.yield = [&] { return yield; };
    services.submit = [&](auto token, auto serial, const auto& node) {
        auto value = host.Services().submit(token, serial, node);
        yield = true;
        return value;
    };
    auto budgeted = Plan({Submit("one"), Submit("two"), {"serial", TaskSerial{{0, 1}}}}, 2);
    TaskSequenceExecution work({13}, budgeted, services);
    work.Advance(0, 1);
    Check(!work.Drained(), "budget did not yield");
    yield = false;
    work.Advance(0, 2);
    Check(work.Drained() && host.requests.size() == 7, "yield discarded task cursor");
    // Different parallel branches may exhaust the work budget at different
    // points; retain their independent time credits until the join.
    unsigned checks = 0;
    bool budget_enabled = true;
    auto credit_services = host.Services();
    credit_services.yield = [&] { return budget_enabled && ++checks >= 4; };
    auto credits = Plan({{"short", TaskWaitSeconds{0.1}},
                         {"long", TaskWaitSeconds{0.2}},
                         {"parallel", TaskParallel{{0, 1}}},
                         {"tail", TaskWaitSeconds{0.2}},
                         Submit("credited"),
                         {"serial", TaskSerial{{2, 3, 4}}}},
                        5);
    TaskSequenceExecution credited({14}, credits, credit_services);
    credited.Advance(0.5, 1);
    Check(!credited.Drained(), "credit test did not yield");
    budget_enabled = false;
    credited.Advance(0, 2);
    Check(credited.Drained() && host.requests.size() == 8, "budget yield discarded parallel remaining time");
}
void TerminalProgress() {
    Host host;
    auto plan = Plan({Submit("body"),
                      {"final-cue", TaskWaitRequest{"body", TaskWaitSignal::Progress, 1}},
                      Submit("cue"),
                      {"entry", TaskSerial{{0, 1, 2}}}},
                     3);
    TaskSequenceExecution run({40}, plan, host.Services());
    run.Advance(0, 1);
    TaskRequestState ended;
    ended.performance_complete = true;
    ended.progress = 0.99;
    run.Observe(host.requests[0].receipt, ended);
    run.Advance(0.1, 2);
    Check(run.Status().reason == TaskExitReason::Failed && host.requests.size() == 1,
          "terminal progress wait either hung or executed an unobserved cue");
    Check(run.Status().issues.at(0).find("observed=0.990000") != std::string::npos,
          "terminal progress diagnostic omitted its observation");
    const auto receipts = run.Requests();
    Check(receipts.size() == 1 && receipts[0].name == "body" && receipts[0].receipt.execution.value == 40 &&
              receipts[0].state.performance_complete && receipts[0].state.progress == 0.99,
          "request snapshot lost value/ownership");
    // Performance completion is a different, explicitly authored dependency.
    auto completion =
        Plan({Submit("body"), {"end", TaskWaitRequest{"body"}}, Submit("after"), {"entry", TaskSerial{{0, 1, 2}}}}, 3);
    TaskSequenceExecution finished({41}, completion, host.Services());
    finished.Advance(0, 1);
    finished.Observe(host.requests.back().receipt, ended);
    finished.Advance(0.1, 2);
    Check(host.requests.size() == 3 && finished.Status().reason == TaskExitReason::Completed,
          "state exit did not satisfy completion wait");
}
void Stops() {
    Host host;
    host.immediate = true;
    auto plan =
        Plan({Submit("start"), {"wait", TaskWaitSeconds{1}}, {"entry", TaskSerial{{0, 1}}}, Submit("abort")}, 2);
    TaskSequenceInput stoppable(*plan);
    stoppable.interruption = 3;
    plan = std::make_shared<TaskSequence>(stoppable);
    TaskSequenceExecution run({20}, plan, host.Services());
    run.Advance(0.1, 1);
    run.Stop(TaskExitReason::Interrupted);
    Check(run.Status().phase == TaskExecutionPhase::Interrupting && host.requests.size() == 1,
          "interrupt branch timing changed");
    run.Advance(0.1, 2);
    Check(run.Drained() && host.requests.size() == 2 && host.releases.size() == 2,
          "interrupt not independently executed");
    TaskSequenceExecution removed({21}, plan, host.Services());
    removed.Advance(0.1, 1);
    removed.Stop(TaskExitReason::WorldLeft);
    removed.Advance(0.1, 2);
    Check(removed.Drained() && host.requests.size() == 3, "world exit ran live-model abort presentation");
    TaskSequenceExecution reentrant({22}, plan, host.Services());
    host.on_submit = [&] { reentrant.Stop(TaskExitReason::Deleted); };
    reentrant.Advance(0.1, 1);
    host.on_submit = {};
    Check(reentrant.Drained() && host.releases.size() == 4, "stop during submission lost the newly returned receipt");
    TaskSequenceExecution cleanup({23}, plan, host.Services());
    cleanup.Advance(0.1, 1);
    host.on_release = [&] { cleanup.Stop(TaskExitReason::Deleted); };
    cleanup.Stop(TaskExitReason::Replaced);
    host.on_release = {};
    cleanup.Advance(0.1, 2);
    Check(cleanup.Drained() && host.requests.size() == 5, "delete during cleanup was overwritten by interruption");
    auto branch = Plan({Submit("yes"), Submit("no"), {"branch", TaskBranch{TaskPhaseEquals{"x"}, 0, 1}}}, 2);
    auto services = host.Services();
    services.condition = [](const auto&) { return TaskAvailability::Unknown; };
    TaskSequenceExecution unknown({24}, branch, services);
    unknown.Advance(0, 1);
    Check(unknown.Status().reason == TaskExitReason::Failed && host.requests.size() == 5,
          "unknown branch executed a guessed side");
    // Successful branch decisions do not merge the two instruction streams.
    services.condition = [](const auto&) { return TaskAvailability::Unavailable; };
    TaskSequenceExecution alternate({25}, branch, services);
    alternate.Advance(0, 1);
    Check(alternate.Drained() && host.requests.size() == 6, "false branch was not executed");
    // Partial submission remains owned and is cancelled exactly once.
    services = host.Services();
    auto submit = services.submit;
    services.submit = [&](auto token, auto serial, const auto& node) {
        auto value = submit(token, serial, node);
        value.state.error = "partial failure";
        return value;
    };
    TaskSequenceExecution partial({26}, Plan({Submit("partial")}, 0), services);
    partial.Advance(0, 1);
    const auto released = host.releases.size();
    partial.Stop(TaskExitReason::Deleted);
    Check(partial.Status().reason == TaskExitReason::Failed && host.releases.size() == released && released == 7,
          "partial submission cleanup repeated or disappeared");
    // A raw graph cycle is invalid, rather than being treated as an implicit loop.
    TaskSequenceInput cycle;
    cycle.nodes = {{"cycle", TaskSerial{{0}}}};
    bool rejected = false;
    try {
        ValidateTaskSequence(cycle);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected, "structural cycle accepted");
}
void SupersededWaits() {
    for (auto signal :
         {TaskWaitSignal::Progress, TaskWaitSignal::PerformanceComplete, TaskWaitSignal::RequestComplete}) {
        Host host;
        auto ports = host.Services();
        ports.submit = [&](auto token, auto serial, const auto& node) {
            auto value = host.Services().submit(token, serial, node);
            value.state.request_complete = false;
            return value;
        };
        auto plan = Plan({Submit("body"),
                          {"wait", TaskWaitRequest{"body", signal, 1}},
                          Submit("must-not-run"),
                          {"entry", TaskSerial{{0, 1, 2}}}},
                         3);
        TaskSequenceExecution run({42}, plan, ports);
        run.Advance(0, 1);
        TaskRequestState state;
        state.superseded = true;
        state.drained = true;
        state.progress = 0.95;
        run.Observe(host.requests[0].receipt, state);
        run.Advance(0.1, 2);
        Check(run.Status().reason == TaskExitReason::Failed && host.requests.size() == 1,
              "superseded request satisfied an unmet dependency or hung");
    }
    Host host;
    auto plan = Plan({Submit("body"),
                      {"cue", TaskWaitRequest{"body", TaskWaitSignal::Progress, 0.9}},
                      Submit("after"),
                      {"entry", TaskSerial{{0, 1, 2}}}},
                     3);
    TaskSequenceExecution run({43}, plan, host.Services());
    run.Advance(0, 1);
    TaskRequestState state;
    state.superseded = true;
    state.drained = true;
    state.progress = 0.95;
    run.Observe(host.requests[0].receipt, state);
    run.Advance(0.1, 2);
    Check(run.Status().reason == TaskExitReason::Completed && host.requests.size() == 2,
          "handoff erased an already observed cue");
}
void AbilityInvocations() {
    Host host;
    host.immediate = true;
    auto plan = Plan({Submit("body"),
                      {"body-wait", TaskWaitSeconds{1}},
                      {"body-sequence", TaskSerial{{0, 1}}},
                      {"call", TaskAbilityCall{"phase", 2, true}},
                      {"join", TaskWaitAbilities{}},
                      Submit("after"),
                      {"entry", TaskSerial{{3, 4, 5}}}},
                     6);
    TaskSequenceExecution joined({50}, plan, host.Services());
    joined.Advance(0.4, 1);
    Check(host.requests.size() == 1 && !joined.ProducersComplete(),
          "ability invocation joined or completed prematurely");
    Check(joined.Status().abilities.size() == 1 && joined.Status().abilities[0].executing &&
              joined.Status().abilities[0].waiting,
          "ability registration is not observable");
    joined.Advance(0.7, 2);
    Check(joined.Drained() && host.requests.size() == 2 && !joined.Status().abilities[0].executing,
          "registered ability completion did not release the join");

    // IsSkillPerform=false does not suppress execution or cancel its body when
    // the caller proceeds. Producer lifetime and user control are separate.
    TaskSequenceInput detached_input(*plan);
    std::get<TaskAbilityCall>(detached_input.nodes[3].operation).wait_for_performance = false;
    auto detached = std::make_shared<TaskSequence>(detached_input);
    TaskSequenceExecution tail({51}, detached, host.Services());
    tail.Advance(0.4, 3);
    Check(host.requests.size() == 4 && tail.Status().phase == TaskExecutionPhase::Draining && !tail.ProducersComplete(),
          "unregistered ability blocked control or was discarded");
    const auto releases = host.releases.size();
    tail.Advance(0.3, 4);
    Check(!tail.Drained() && host.releases.size() == releases, "unregistered producer was released before completing");
    tail.Advance(0.4, 5);
    Check(tail.Drained() && tail.ProducersComplete(), "detached producer never drained");

    // Calling an executing identity is not another copy, and completion of an
    // earlier invocation is not a permanent completed flag for the next one.
    auto repeated = Plan({Submit("hit"),
                          {"wait", TaskWaitSeconds{0.2}},
                          {"child", TaskSerial{{0, 1}}},
                          {"first", TaskAbilityCall{"same", 2, true}},
                          {"duplicate", TaskAbilityCall{"same", 2, true}},
                          {"join", TaskWaitAbilities{}},
                          {"again", TaskAbilityCall{"same", 2, false}},
                          {"join-again", TaskWaitAbilities{}},
                          {"entry", TaskSerial{{3, 4, 5, 6, 7}}}},
                         8);
    const auto before = host.requests.size();
    TaskSequenceExecution reentry({52}, repeated, host.Services());
    reentry.Advance(0.1, 6);
    Check(host.requests.size() == before + 1, "active ability was invoked twice");
    reentry.Advance(0.2, 7);
    Check(host.requests.size() == before + 2 && !reentry.Drained() && reentry.Status().abilities[0].invocation == 2 &&
              reentry.Status().abilities[0].waiting,
          "ability reentry lost registration or reused its earlier completion");
    reentry.Advance(0.2, 8);
    Check(reentry.Drained(), "second invocation remained blocked");

    // A child can register another ability before its own body becomes ready.
    auto nested = Plan({{"child-wait", TaskWaitSeconds{1}},
                        {"child-call", TaskAbilityCall{"child", 0, true}},
                        {"parent-call", TaskAbilityCall{"parent", 1, false}},
                        {"join", TaskWaitAbilities{}},
                        Submit("after-nested"),
                        {"entry", TaskSerial{{2, 3, 4}}}},
                       5);
    TaskSequenceExecution children({53}, nested, host.Services());
    children.Advance(0.4, 9);
    Check(children.Status().abilities.size() == 2 && !children.ProducersComplete(), "nested waiting registration lost");
    children.Advance(0.7, 10);
    Check(children.Drained(), "nested child advanced twice or not at all");

    // Cancellation stops pending children, including those never registered as
    // skill waits. No delayed callback may submit work on a retired actor.
    auto delayed = Plan({{"wait", TaskWaitSeconds{1}},
                         Submit("too-late"),
                         {"child", TaskSerial{{0, 1}}},
                         {"call", TaskAbilityCall{"tail", 2, false}}},
                        3);
    TaskSequenceExecution removed({54}, delayed, host.Services());
    removed.Advance(0.1, 11);
    const auto count = host.requests.size();
    removed.Stop(TaskExitReason::WorldLeft);
    removed.Advance(9, 12);
    Check(removed.Drained() && removed.ProducersComplete() && host.requests.size() == count &&
              !removed.Status().abilities[0].executing,
          "retired ability continued submitting work");

    // Request resource drain is later than the ability body becoming ready.
    Host draining;
    draining.immediate = true;
    draining.drain = false;
    auto quick = Plan({Submit("tail"),
                       {"call", TaskAbilityCall{"quick", 0, true}},
                       {"join", TaskWaitAbilities{}},
                       {"root", TaskSerial{{1, 2}}}},
                      3);
    TaskSequenceExecution resources({55}, quick, draining.Services());
    resources.Advance(0, 13);
    Check(resources.ProducersComplete() && !resources.Drained() && !resources.Status().abilities[0].executing,
          "resource tail was counted as an executing ability");
    TaskRequestState done;
    done.drained = true;
    resources.Observe(draining.requests[0].receipt, done);
    Check(resources.Drained(), "finished ability lost its resource drain receipt");

    Host scoped;
    std::vector<std::string> names;
    auto ports = scoped.Services();
    const auto submit = ports.submit;
    ports.submit = [&](auto token, auto id, const auto& input) {
        names.push_back(input.request);
        return submit(token, id, input);
    };
    TaskSequenceExecution fresh({57}, repeated, ports);
    fresh.Advance(0.1, 1);
    fresh.Advance(0.2, 2);
    Check(fresh.Status().issues.empty() && scoped.requests.size() == 2 && names[0] != names[1],
          "new ability invocation collided with its previous animation receipt");
    fresh.Stop(TaskExitReason::Deleted);

    // A launched child may wait on an animation the parent will submit later.
    // It binds that invocation's receipt, not another ability's same-name slot.
    auto inherited = Plan({{"hit-clock", TaskWaitRequest{"clock", TaskWaitSignal::Progress, 0.5}},
                           {"hit", TaskAbilityCall{"hit", 0, true}},
                           Submit("clock"),
                           {"body-tail", TaskWaitSeconds{0.8}},
                           {"parent", TaskSerial{{1, 2, 3}}},
                           {"entry-call", TaskAbilityCall{"entry", 4, true}},
                           {"all", TaskWaitAbilities{}},
                           {"root", TaskSerial{{5, 6}}}},
                          7);
    TaskSequenceExecution borrowed({58}, inherited, scoped.Services());
    borrowed.Advance(0.1, 3);
    Check(borrowed.Status().issues.empty() && !borrowed.ProducersComplete(), "forward parent clock binding failed");
    TaskRequestState progress;
    progress.progress = 0.6;
    borrowed.Observe(scoped.requests.back().receipt, progress);
    borrowed.Advance(0.8, 4);
    Check(borrowed.ProducersComplete() && borrowed.Status().issues.empty(),
          "child wait used an unrelated request namespace");
}
void TargetScopes() {
    Host host;
    host.immediate = true;
    auto services = host.Services();
    auto submit = services.submit;
    std::vector<TaskSubmit> inputs;
    services.submit = [&](auto token, auto serial, const auto& node) {
        inputs.push_back(node);
        return submit(token, serial, node);
    };
    auto plan = Plan({Submit("hit"),
                      {"delay", TaskWaitSeconds{.2}},
                      {"ability-body", TaskSerial{{1, 0}}},
                      {"call", TaskAbilityCall{"impact", 2, true}},
                      {"members", TaskForTargets{"Param", {"A", "B"}, 3, {}}},
                      {"all", TaskWaitAbilities{}},
                      Submit("outside"),
                      {"entry", TaskSerial{{4, 5, 6}}}},
                     7);
    TaskSequenceExecution run({60}, plan, services);
    run.Advance(0, 1);
    Check(inputs.empty() && run.Status().abilities.size() == 2, "target branches suppressed the other's ability");
    run.Advance(.2, 2);
    Check(inputs.size() == 3 && run.Drained(), "per-target abilities did not join");
    Check(inputs[0].target_bindings.at("Param") == "A" && inputs[1].target_bindings.at("Param") == "B",
          "target inputs changed across frames");
    Check(inputs[0].request != inputs[1].request && inputs[2].target_bindings.empty(),
          "target scope escaped to parent");
    auto nested = Plan({Submit("value"),
                        {"inner", TaskForTargets{"Copy", {"Param"}, 0, {}}},
                        {"outer", TaskForTargets{"Param", {"B"}, 1, {}}}},
                       2);
    inputs.clear();
    TaskSequenceExecution child({61}, nested, services);
    child.Advance(0, 3);
    Check(inputs.size() == 1 && inputs[0].target_bindings.at("Copy") == "B",
          "nested binding was not captured from parent");
    auto empty = Plan({Submit("not-selected"), Submit("fallback"), {"empty", TaskForTargets{"Param", {}, 0, 1}}}, 2);
    inputs.clear();
    TaskSequenceExecution fallback({62}, empty, services);
    fallback.Advance(0, 4);
    Check(inputs.size() == 1 && inputs[0].request == "fallback", "empty collection did not run its fallback");
    auto waits = Plan({Submit("pending"),
                       {"wait", TaskWaitRequest{"pending"}},
                       {"body", TaskSerial{{0, 1}}},
                       {"members", TaskForTargets{"Param", {"A", "B"}, 2, {}}}},
                      3);
    host.immediate = false;
    host.requests.clear();
    inputs.clear();
    TaskSequenceExecution waiting({63}, waits, services);
    waiting.Advance(0, 5);
    TaskRequestState done;
    done.performance_complete = true;
    waiting.Observe(host.requests[0].receipt, done);
    waiting.Advance(0, 6);
    Check(!waiting.ProducersComplete(), "one target completed another target's wait");
    waiting.Observe(host.requests[1].receipt, done);
    waiting.Advance(0, 7);
    Check(waiting.Drained(), "target wait did not finish");
    auto collection =
        Plan({Submit("hit"), Submit("empty"), {"members", TaskForTargets{"Param", {"fallback"}, 0, 1, "selected"}}}, 2);
    host.immediate = true;
    inputs.clear();
    int reads = 0;
    std::optional<std::vector<std::string>> selected = std::vector<std::string>{"B", "A"};
    services.targets = [&](const auto& name) {
        Check(name == "selected", "wrong collection");
        ++reads;
        return selected;
    };
    TaskSequenceExecution supplied({64}, collection, services);
    supplied.Advance(0, 8);
    Check(inputs.size() == 2 && inputs[0].target_bindings.at("Param") == "B" &&
              inputs[1].target_bindings.at("Param") == "A" && reads == 1,
          "supplied collection did not preserve order or was read repeatedly");
    selected = std::vector<std::string>{};
    inputs.clear();
    TaskSequenceExecution none({65}, collection, services);
    none.Advance(0, 9);
    Check(inputs.size() == 1 && inputs[0].request == "empty", "explicit empty input used fallback members");
    selected.reset();
    inputs.clear();
    TaskSequenceExecution omitted({66}, collection, services);
    omitted.Advance(0, 10);
    Check(inputs.size() == 1 && inputs[0].target_bindings.at("Param") == "fallback",
          "absent collection lost authored fallback");
}
void AnimationBoundaries() {
    Host host;
    auto body = Submit("body");
    std::get<TaskAnimationCommand>(std::get<TaskSubmit>(body.operation).operation).normalized_end = 1.5f;
    auto plan = Plan({body,
                      {"end", TaskWaitRequest{"body", TaskWaitSignal::AnimationEnd}},
                      Submit("notified"),
                      {"left", TaskWaitRequest{"body", TaskWaitSignal::StateLeft}},
                      Submit("departed"),
                      {"root", TaskSerial{{0, 1, 2, 3, 4}}}},
                     5);
    TaskSequenceExecution run({56}, plan, host.Services());
    run.Advance(0, 1);
    TaskRequestState clip;
    clip.performance_complete = true;
    clip.progress = 1.1;
    run.Observe(host.requests[0].receipt, clip);
    run.Advance(0.1, 2);
    Check(host.requests.size() == 1 && run.Status().issues.empty(),
          "clip completion satisfied or failed a separate request End");
    clip.animation_end = true;
    clip.progress = 1.6;
    run.Observe(host.requests[0].receipt, clip);
    run.Advance(0.1, 3);
    Check(host.requests.size() == 2 && !run.ProducersComplete(), "request End implied state departure");
    clip.state_left = true;
    run.Observe(host.requests[0].receipt, clip);
    run.Advance(0.1, 4);
    Check(host.requests.size() == 3 && run.Drained(), "observed state departure was not delivered");
    TaskSequenceInput invalid_plan(*plan);
    std::get<TaskAnimationCommand>(std::get<TaskSubmit>(invalid_plan.nodes[0].operation).operation).normalized_end.reset();
    bool rejected = false;
    try {
        ValidateTaskSequence(invalid_plan);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected, "end wait without an authored End was accepted");
}
void OmittedSteps() {
    Host host;
    host.immediate = true;
    std::vector<TaskNode> nodes{
        {"source/effect", TaskOmitted{"missing", "deferred complex unit", "effect", {"partner"}}}};
    const auto signals = {
        TaskWaitSignal::Submitted,           TaskWaitSignal::Started,           TaskWaitSignal::AnimationEnd,
        TaskWaitSignal::StateLeft,           TaskWaitSignal::AnimationFinished, TaskWaitSignal::RequestComplete,
        TaskWaitSignal::PerformanceComplete, TaskWaitSignal::Progress,          TaskWaitSignal::Drained};
    std::vector<TaskNodeIndex> wait_indices;
    for (const auto signal : signals) {
        wait_indices.push_back(nodes.size());
        nodes.push_back({"wait", TaskWaitRequest{"missing", signal, 1}});
    }
    const auto waits = nodes.size();
    nodes.push_back({"all-waits", TaskParallel{wait_indices}});
    const auto continued = nodes.size();
    nodes.push_back(Submit("continued"));
    const auto serial = nodes.size();
    nodes.push_back({"serial", TaskSerial{{0, waits, continued}}});
    const auto sibling = nodes.size();
    nodes.push_back(Submit("independent"));
    const auto root = nodes.size();
    nodes.push_back({"root", TaskParallel{{serial, sibling}}});
    TaskSequenceInput plan_input;
    plan_input.nodes = std::move(nodes);
    plan_input.entry = root;
    auto plan = std::make_shared<TaskSequence>(plan_input);
    TaskSequenceExecution execution({80}, plan, host.Services());
    execution.Advance(0, 1);
    Check(execution.Drained() && host.requests.size() == 2 && host.releases.size() == 2,
          "omitted work reached native provider or blocked independent work");
    Check(execution.Status().issues.empty() && execution.Status().skipped.size() == 1,
          "omission was hidden or treated as failure");
    const auto& skipped = execution.Status().skipped.front();
    Check(skipped.request == "missing" && skipped.source == "source/effect" && skipped.unit == "effect" &&
              skipped.dependencies == std::vector<std::string>{"partner"},
          "omission provenance was lost");
    const auto requests = execution.Requests();
    const auto found =
        std::find_if(requests.begin(), requests.end(), [](const auto& r) { return r.name == "missing"; });
    Check(found != requests.end() && found->state.skipped && found->state.drained && !found->state.submitted &&
              !found->state.request_complete && !found->state.started && !found->state.performance_complete &&
              !found->state.progress,
          "omitted work fabricated a native success signal");
    execution.Stop(TaskExitReason::Deleted);
    Check(host.releases.size() == 2, "omission requested native cancellation");
    TaskSequenceInput invalid(*plan);
    std::get<TaskOmitted>(invalid.nodes[0].operation).reason.clear();
    bool rejected = false;
    try {
        ValidateTaskSequence(invalid);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected, "unexplained omission accepted");
    auto failed_services = host.Services();
    failed_services.submit = [](TaskExecutionToken t, std::uint64_t id, const TaskSubmit&) {
        TaskSubmission r;
        r.receipt = {t, id, 0, {"body"}, 1};
        r.state.error = "actual provider failure";
        return r;
    };
    TaskSequenceExecution failed({81}, Plan({Submit("failed")}, 0), failed_services);
    failed.Advance(0, 2);
    Check(failed.Status().reason == TaskExitReason::Failed && failed.Status().skipped.empty(),
          "actual failure was relabelled skipped");
}
}
void TestTaskSequence() {
    Clocks();
    Receipts();
    Loops();
    TerminalProgress();
    Stops();
    SupersededWaits();
    AbilityInvocations();
    AnimationBoundaries();
    TargetScopes();
    OmittedSteps();
    Host host;
    host.drain = true;
    auto plan = Plan({Submit("change"),
                      Submit("success"),
                      {"continuation", TaskOnSuccess{"change", 1}},
                      {"root", TaskSerial{{0, 2}}}},
                     3);
    TaskSequenceExecution pending({101}, plan, host.Services());
    pending.Advance(0, 1);
    Check(host.requests.size() == 1, "success arm ran before native completion");
    auto ready = host.requests.front().state;
    ready.performance_complete = true;
    pending.Observe(host.requests.front().receipt, ready);
    pending.Advance(0, 2);
    Check(host.requests.size() == 2, "successful producer did not enter its continuation");
    pending.Stop(TaskExitReason::Deleted);
    auto skipped = host.Services();
    unsigned calls = 0;
    skipped.submit = [&](TaskExecutionToken token, std::uint64_t serial, const TaskSubmit& command) {
        ++calls;
        TaskSubmission result;
        result.receipt = {token, serial, 0, {"visual"}, 100};
        result.state.skipped = true;
        result.state.drained = true;
        result.state.skip_reason = "no model change";
        Check(command.request == "change", "unchanged producer entered success arm");
        return result;
    };
    TaskSequenceExecution unchanged({102}, plan, skipped);
    unchanged.Advance(0, 1);
    Check(unchanged.Drained() && calls == 1, "unchanged producer did not skip only its success arm");
    auto failed = host.Services();
    failed.submit = [&](TaskExecutionToken token, std::uint64_t serial, const TaskSubmit& command) {
        ++calls;
        TaskSubmission result;
        result.receipt = {token, serial, 0, {"visual"}, 100};
        result.state.error = "native change failed";
        result.state.drained = true;
        Check(command.request == "change", "failed producer entered success arm");
        return result;
    };
    TaskSequenceExecution fault({103}, plan, failed);
    fault.Advance(0, 1);
    Check(fault.Status().reason == TaskExitReason::Failed, "failed producer was treated as successful");
    Host clock;
    auto timed = Plan({Submit("change"),
                       {"delay", TaskWaitSeconds{.25}},
                       Submit("after"),
                       {"success", TaskSerial{{1, 2}}},
                       {"continuation", TaskOnSuccess{"change", 3}},
                       {"root", TaskSerial{{0, 4}}}},
                      5);
    TaskSequenceExecution delayed({104}, timed, clock.Services());
    delayed.Advance(1, 1);
    auto completed = clock.requests.front().state;
    completed.performance_complete = true;
    delayed.Observe(clock.requests.front().receipt, completed);
    delayed.Advance(10, 2);
    Check(clock.requests.size() == 1, "native success invented earlier completion time for its child delay");
    delayed.Advance(.25, 3);
    Check(clock.requests.size() == 2, "success arm failed to advance after its own delay");
    delayed.Stop(TaskExitReason::Deleted);
}
