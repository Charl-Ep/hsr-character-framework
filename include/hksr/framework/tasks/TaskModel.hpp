#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

namespace hksr::framework {
struct TaskKey {
    std::string catalog,name;
    bool operator==(const TaskKey& other) const {return catalog==other.catalog && name==other.name;}
    bool operator<(const TaskKey& other) const {return std::tie(catalog,name)<std::tie(other.catalog,other.name);}
};
struct TaskContextToken {std::uint64_t value=0;};
struct TaskExecutionToken {std::uint64_t value=0;};
struct TaskParticipant {std::string name;};
struct TaskSlot {std::int32_t value=0;};
using TaskValue=std::variant<bool,std::int64_t,double,std::string>;
using TaskValues=std::map<std::string,TaskValue>;
enum class TaskValueType { Boolean,Integer,Number,Text };
TaskValueType TypeOf(const TaskValue&);
struct TaskInputDefinition {
    std::string name;
    TaskValueType type=TaskValueType::Text;
    std::optional<TaskValue> default_value;
};
struct TaskValueEquals {std::string name;TaskValue value;};
struct TaskPhaseEquals {std::string phase;};
struct TaskNeedsParticipant {TaskParticipant participant;};
struct TaskUnresolvedRequirement {std::string source,detail;};
using TaskRequirement=std::variant<TaskValueEquals,TaskPhaseEquals,TaskNeedsParticipant,TaskUnresolvedRequirement>;
struct TaskSourceLocation {std::string document,field;};
// Authored visual task, not an Animator state or permission to invoke the
// containing battle ability. The entry identifies a compiled visual sequence.
struct TaskDefinition {
    TaskKey key;
    std::vector<TaskSourceLocation> sources;
    std::vector<TaskInputDefinition> inputs;
    std::vector<TaskRequirement> requirements; // conjunction
    std::vector<std::string> tags;
    std::string entry;
};
enum class TaskAvailability { Available,Unavailable,Unknown };
enum class TaskReasonKind { Unimplemented,MissingInput,InputType,Condition,Participant,Disabled,Unresolved };
struct TaskReason {TaskReasonKind kind;std::string source,detail;};
struct TaskQueryResult {
    TaskAvailability availability=TaskAvailability::Available;
    TaskValues inputs;
    std::vector<TaskReason> reasons;
};
struct TaskQuery {
    TaskValues inputs;
    std::vector<TaskParticipant> participants;
    std::optional<TaskSlot> slot;
};
enum class TaskExitReason { Completed,Interrupted,Replaced,ActorLost,WorldLeft,Deleted,Failed };
// The adapter reserves request identity before native submission. Model is
// zero until binding (or for non-model operations), then immutable. Reusing a
// state name on a replacement model never replaces an existing binding.
struct TaskRequestReceipt {
    TaskExecutionToken execution;
    std::uint64_t request=0,model=0;
    TaskParticipant participant;
    std::uint64_t entity_generation=0;
    bool SameRequest(const TaskRequestReceipt& other) const {
        return execution.value==other.execution.value && request==other.request &&
            participant.name==other.participant.name && entity_generation==other.entity_generation;
    }
    bool operator==(const TaskRequestReceipt& other) const {
        return SameRequest(other) && model==other.model;
    }
};
struct TaskRequestState {
    bool submitted=false,request_complete=false,performance_complete=false,drained=false;
    std::optional<double> progress;
    std::string error;
    // This execution deliberately handed the same animation channel to a
    // later request. Not natural completion and not an external interruption.
    bool superseded=false;
};
}
