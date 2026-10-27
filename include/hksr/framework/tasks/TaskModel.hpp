#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

namespace hksr::framework {
struct ControlRelation;
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
struct TaskNeedsTarget {std::string name;bool allow_position=false;};
struct TaskNeedsAnchor {std::string name;};
struct TaskChoiceEquals {std::string name,value;};
struct TaskUnresolvedRequirement {std::string source,detail;};
using TaskRequirement=std::variant<TaskValueEquals,TaskPhaseEquals,TaskNeedsParticipant,TaskUnresolvedRequirement,
    TaskNeedsTarget,TaskNeedsAnchor,TaskChoiceEquals>;
struct TaskSourceLocation {std::string document,field;};
// Editable visual parameters. These describe inputs, not execution permission.
// Required targets/anchors/choices are also exposed by TaskBindings().
struct TaskBindingSchema {
    std::vector<std::string> targets,anchors;
    std::map<std::string,std::vector<std::string>> choices;
};
// Authored visual task, not an Animator state or permission to invoke the
// containing battle ability. The entry identifies a compiled visual sequence.
struct TaskDefinition {
    TaskKey key;
    std::vector<TaskSourceLocation> sources;
    std::vector<TaskInputDefinition> inputs;
    std::vector<TaskRequirement> requirements; // conjunction
    std::vector<std::string> tags;
    std::string entry;
    TaskBindingSchema bindings;
    std::shared_ptr<const ControlRelation> control;
};
TaskBindingSchema TaskBindings(const TaskDefinition&);
enum class TaskAvailability { Available,Unavailable,Unknown };
enum class TaskReasonKind { Unimplemented,MissingInput,InputType,Condition,Participant,Disabled,Unresolved };
struct TaskReason {TaskReasonKind kind;std::string source,detail;};
struct TaskQueryResult {
    TaskAvailability availability=TaskAvailability::Available;
    TaskValues inputs;
    std::vector<TaskReason> reasons;
    struct Facets {
        TaskAvailability path=TaskAvailability::Available;
        TaskAvailability inputs=TaskAvailability::Available;
        TaskAvailability binding=TaskAvailability::Available;
    };
    std::optional<Facets> facets;
};
struct TaskQuery {
    TaskValues inputs;
    std::vector<TaskParticipant> participants;
    std::optional<TaskSlot> slot;
    std::vector<std::string> targets,anchors;
    std::map<std::string,std::string> choices;
    std::vector<std::string> position_targets;
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
    bool started=false; // actual asynchronous start, distinct from queue acceptance
    bool animation_end=false,state_left=false,animation_finished=false;
};
struct TaskRequestSnapshot {
    std::string name,source;
    TaskRequestReceipt receipt;
    TaskRequestState state;
};
enum class TaskExecutionPhase { Running,Interrupting,Draining,Finished };
struct TaskAbilitySnapshot {
    std::string ability,source;
    std::uint64_t invocation=0;
    bool waiting=false,executing=false;
};
struct TaskExecutionStatus {
    TaskExecutionToken token;
    TaskExecutionPhase phase=TaskExecutionPhase::Running;
    std::optional<TaskExitReason> reason;
    bool finish_requested=false;
    std::size_t retained_requests=0;
    std::vector<std::string> issues,waiting;
    std::vector<TaskAbilitySnapshot> abilities;
};
}
