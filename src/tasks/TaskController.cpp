#include <hksr/framework/tasks/TaskController.hpp>
#include <algorithm>
#include <stdexcept>

namespace hksr::framework {
struct TaskController::State {
    struct Pending {std::uint64_t intent;TaskSelection selection;bool finish=false;};
    struct Run {
        TaskSelection selection;
        std::unique_ptr<TaskPresentation> player;
        std::optional<TaskContinuation> next;
        bool stopping=false;
    };
    struct Contribution {std::uint64_t visual;TaskContextToken logical;};
    std::uint64_t owner=0,next_execution=0;
    std::shared_ptr<const TaskCatalog> catalog;
    std::shared_ptr<VisualProgramPlayer> visual;
    TaskPresentationServices services;
    TaskContext context;
    std::map<std::string,Contribution> contributions;
    std::optional<Pending> pending;
    std::optional<TaskSelection> selected;
    std::shared_ptr<Run> active;
    std::vector<std::shared_ptr<Run>> retiring;
    TaskControllerStatus status;
    bool servicing=false;
    std::optional<std::uint64_t> frame;
    TaskQuery Inputs(const TaskSelection& selection,bool captured,const std::vector<TaskRequirement>& requirements) const {
        auto query=selection.query;
        const auto inputs=captured || selection.inputs_captured?selection.visual:visual->Parameters(selection.visual);
        query.targets.clear();query.choices=inputs.choices;query.anchors.clear();
        VisualProgramParameters required;bool needs_target=false;
        for(const auto& requirement:requirements)if(const auto* target=std::get_if<TaskNeedsTarget>(&requirement)) {
            needs_target=true;
            if(const auto found=inputs.targets.find(target->name);found!=inputs.targets.end())required.targets.insert(*found);
        }
        if(needs_target)query.targets=visual->AvailableTargets(required);
        for(const auto& [name,anchor]:inputs.anchors){(void)anchor;query.anchors.push_back(name);}
        return query;
    }
    TaskSelectionResult Query(const TaskSelection& selection,bool captured=false) const {
        const auto key=selection.query.slot?context.ResolveSlot(*selection.query.slot,selection.task):selection.task;
        const auto& task=catalog->Resolve(key);
        try {return {0,key,context.Query(task.definition,Inputs(selection,captured,task.definition.requirements))};}
        catch(const std::exception& error) {
            return {0,key,{TaskAvailability::Unknown,{},{{TaskReasonKind::Unresolved,"visual inputs",error.what()}}}};
        }
    }
    void SyncContexts() {
        const auto live=visual->ReadyContexts();
        for(auto it=contributions.begin();it!=contributions.end();) {
            const auto found=live.find(it->first);
            if(found!=live.end() && found->second==it->second.visual){++it;continue;}
            context.Exit(it->second.logical);it=contributions.erase(it);
        }
        // Preserve actual entry order, not lexical context-name ordering.
        std::vector<std::pair<std::uint64_t,std::string>> added;
        for(const auto& [name,token]:live)if(!contributions.count(name) && catalog->contexts.count(name))added.emplace_back(token,name);
        std::sort(added.begin(),added.end());
        for(const auto& [visual_token,name]:added) {
            const auto& definition=catalog->contexts.at(name);const auto logical=context.Enter(name);
            for(const auto& [key,value]:definition.values)context.SetValue(logical,key,value);
            context.SetPhase(logical,definition.phase);
            for(const auto& [slot,key]:definition.slots)context.SetSlot(logical,{slot},key);
            for(const auto& disable:definition.disables)context.Disable(logical,disable);
            contributions.emplace(name,Contribution{visual_token,logical});
        }
    }
    void Publish() {
        status.pending=pending?std::optional<TaskKey>(pending->selection.task):std::nullopt;
        status.retiring=retiring.size();
        if(active){status.current=active->selection.task;status.execution=active->player->Status();}
    }
    void Start(Pending desired) {
        if(!desired.selection.inputs_captured)desired.selection.visual=visual->Parameters(desired.selection.visual);
        desired.selection.inputs_captured=true;
        auto checked=Query(desired.selection,true);
        if(checked.query.availability!=TaskAvailability::Available) {
            status.blocked=std::move(checked.query);return;
        }
        desired.selection.task=checked.resolved;desired.selection.query.inputs=std::move(checked.query.inputs);
        auto run=std::make_shared<Run>();run->selection=std::move(desired.selection);
        const auto& plan=catalog->Resolve(run->selection.task);run->next=plan.next;
        auto ports=services;
        const auto selection=run->selection;
        ports.condition=[this,selection](const auto& requirement) {
            SyncContexts();return context.Evaluate(requirement,Inputs(selection,true,{requirement})).availability;
        };
        run->player=std::make_unique<TaskPresentation>(TaskExecutionToken{++next_execution},owner,
            catalog->sequences.at(plan.definition.entry),visual,run->selection.visual,std::move(ports),true);
        if(desired.finish)run->player->RequestFinish();
        active=std::move(run);status.started=desired.intent;status.blocked.reset();status.error.clear();
        selected=active->selection;selected->query.slot.reset();
    }
    void RetireActive() {
        if(!active)return;
        status.current=active->selection.task;status.execution=active->player->Status();
        retiring.push_back(std::move(active));active.reset();
    }
};
TaskController::TaskController(std::uint64_t owner,std::shared_ptr<const TaskCatalog> catalog,
    std::shared_ptr<VisualProgramPlayer> visual,TaskPresentationServices services) {
    if(!owner || !catalog || !visual)throw std::invalid_argument("task controller requires owned actor, catalog and scene");
    ValidateTaskCatalog(*catalog);state_=std::make_unique<State>();
    state_->owner=owner;state_->catalog=std::move(catalog);state_->visual=std::move(visual);state_->services=std::move(services);
}
TaskController::~TaskController()=default;
TaskSelectionResult TaskController::Query(const TaskSelection& selection) const{return state_->Query(selection);}
TaskSelectionResult TaskController::Select(TaskSelection selection) {
    auto& state=*state_;
    if(state.status.closing)throw std::logic_error("task controller is closing");
    state.SyncContexts();auto result=state.Query(selection);
    if(result.query.availability!=TaskAvailability::Available)return result; // leave the running task unchanged
    result.intent=++state.status.requested;
    state.pending=State::Pending{result.intent,std::move(selection)};
    state.selected=state.pending->selection;
    state.status.error.clear();state.status.blocked.reset();state.Publish();return result;
}
void TaskController::RequestFinish() {
    if(state_->pending)state_->pending->finish=true;
    else if(state_->active)state_->active->player->RequestFinish();
}
void TaskController::Cancel() {
    auto& state=*state_;state.pending.reset();state.selected.reset();
    if(state.active && !state.active->stopping) {
        state.active->stopping=true;state.active->next.reset();state.active->player->Stop(TaskExitReason::Interrupted);
    }
    for(const auto& run:state.retiring)run->player->Stop(TaskExitReason::Interrupted);
    state.Publish();
}
void TaskController::Close(TaskExitReason reason) {
    if(reason==TaskExitReason::Completed || reason==TaskExitReason::Interrupted || reason==TaskExitReason::Replaced)
        throw std::invalid_argument("controller close requires actor/domain retirement");
    auto& state=*state_;state.status.closing=true;state.pending.reset();state.selected.reset();
    if(state.active){state.active->next.reset();state.active->player->Stop(reason);}
    for(const auto& run:state.retiring)run->player->Stop(reason);
    state.visual->End();state.SyncContexts();state.Publish();
}
void TaskController::Reset(TaskExitReason reason) {
    if(reason==TaskExitReason::Completed || reason==TaskExitReason::Interrupted || reason==TaskExitReason::Replaced)
        throw std::invalid_argument("task reset requires domain/owner invalidation");
    auto& state=*state_;state.pending.reset();state.selected.reset();
    if(state.active){state.active->next.reset();state.active->stopping=true;state.active->player->Stop(reason);}
    for(const auto& run:state.retiring)run->player->Stop(reason);
    state.visual->Leave();state.SyncContexts();state.Publish();
}
void TaskController::Service(double seconds,std::uint64_t frame,bool allow_start) {
    auto& state=*state_;if(state.servicing || state.frame==frame)return;
    state.servicing=true;struct Guard{bool& value;~Guard(){value=false;}} guard{state.servicing};state.frame=frame;
    try {
        state.SyncContexts();
        if(state.pending && state.active && !state.active->stopping) {
            state.active->stopping=true;state.active->next.reset();state.active->player->Stop(TaskExitReason::Replaced);
        }
        const auto old=state.retiring;
        for(const auto& run:old)run->player->Service(seconds,frame);
        if(auto run=state.active)run->player->Service(seconds,frame);
        state.visual->Service();state.SyncContexts();
        if(state.active && state.active->player->YieldedControl()) {
            const auto run=state.active;
            if(!state.pending && !state.status.closing && run->next && run->player->Status().reason==TaskExitReason::Completed) {
                TaskSelection next{run->next->task,run->selection.query,run->selection.visual};
                next.inputs_captured=true;
                next.query.inputs=run->next->inputs;next.query.slot.reset();
                state.pending=State::Pending{++state.status.requested,std::move(next)};
                state.selected=state.pending->selection;
            }
            state.RetireActive();
        }
        if(allow_start && !state.active && state.pending && !state.status.closing) {
            auto desired=std::move(*state.pending);state.pending.reset();
            state.Start(std::move(desired));
            // A successor starts with no guessed carry-over time. It can
            // submit now; no recursive same-frame chain of completed tasks.
            if(auto run=state.active)run->player->Service(0,frame);
            state.SyncContexts();
        }
        state.retiring.erase(std::remove_if(state.retiring.begin(),state.retiring.end(),
            [](const auto& run){return run->player->Drained();}),state.retiring.end());
    }catch(const std::exception& error) {
        state.status.error=error.what();state.pending.reset();
        if(state.active){state.active->next.reset();state.active->player->Stop(TaskExitReason::Failed);}
    }
    state.Publish();
}
TaskContext& TaskController::Context(){return state_->context;}
const TaskContext& TaskController::Context() const{return state_->context;}
const TaskControllerStatus& TaskController::Status() const{return state_->status;}
std::optional<TaskSelection> TaskController::Selection() const{return state_->selected;}
VisualStepResult TaskController::RestoreInputsReady(const TaskSelection& selection) const {
    VisualProgramParameters required;
    const auto& definition=state_->catalog->Resolve(selection.task).definition;
    for(const auto& requirement:definition.requirements)if(const auto* target=std::get_if<TaskNeedsTarget>(&requirement))
        if(const auto found=selection.visual.targets.find(target->name);found!=selection.visual.targets.end())required.targets.insert(*found);
    return state_->visual->RestoreInputsReady(required);
}
std::vector<TaskRequestSnapshot> TaskController::Requests() const {
    auto out=state_->active?state_->active->player->Requests():std::vector<TaskRequestSnapshot>{};
    for(const auto& run:state_->retiring) {
        const auto rows=run->player->Requests();out.insert(out.end(),rows.begin(),rows.end());
    }
    return out;
}
bool TaskController::Drained() const {
    return !state_->pending && (!state_->active || state_->active->player->Drained()) && state_->retiring.empty() &&
        (!state_->status.closing || state_->visual->Drained());
}
}
