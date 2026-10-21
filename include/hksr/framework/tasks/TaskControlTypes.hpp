#pragma once
#include "TaskContext.hpp"
#include "../presentation/ActionTypes.hpp"
#include "../presentation/VisualProgramState.hpp"

namespace hksr::framework {
struct TaskSelection {
    TaskKey task;
    TaskQuery query;
    VisualProgramParameters visual;
    bool inputs_captured=false; // restart a saved request without rereading runtime defaults
};
void ValidateTaskSelection(const TaskSelection&);
bool SameTaskSelection(const TaskSelection&,const TaskSelection&);
void RemapTaskTargets(TaskSelection&,const std::map<std::uint64_t,std::uint64_t>&);
struct TaskSelectionResult {
    std::uint64_t intent=0;
    TaskKey resolved;
    TaskQueryResult query;
};
struct TaskControllerStatus {
    std::uint64_t requested=0,started=0;
    std::optional<TaskKey> current,pending;
    std::optional<TaskExecutionStatus> execution;
    std::optional<TaskQueryResult> blocked;
    std::size_t retiring=0;
    bool closing=false;
    std::string error;
};
enum class TaskControlOperation { Query,Select,Finish,Cancel };
struct TaskControlInput {
    TaskControlOperation operation=TaskControlOperation::Query;
    std::optional<TaskSelection> selection;
};
struct TaskControlResult {
    std::optional<TaskSelectionResult> selection;
    TaskControllerStatus status;
    std::vector<TaskContextView> contexts;
    std::map<std::string,TaskQueryResult> catalog;
    std::vector<TaskRequestSnapshot> requests; // detailed receipts, explicit query only
    std::optional<TaskKey> preparing;
    std::string preparation_error;
};
struct CharacterTaskStatus {
    ActionDomain domain=ActionDomain::Alternate;
    std::map<std::string,TaskDefinition> definitions;
    TaskControlResult control;
};
}
