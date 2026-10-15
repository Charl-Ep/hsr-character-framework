#pragma once
#include <cstdint>
#include <string>
#include <map>

namespace hksr::framework {
enum class OperationKind { PrepareSource, Create, Rebuild, ReplaceSource, Enable, Disable, Action, Placement, Gaze, Delete };
enum class OperationPhase { Waiting, Running, Returned, Failed, Cancelled, Superseded };
struct OperationStatus {
    std::uint64_t id=0,character=0,source_request=0,revision=0,action_intent=0,creation=0;
    OperationKind kind=OperationKind::Create;
    OperationPhase phase=OperationPhase::Waiting;
    std::string detail;
    bool Terminal() const noexcept {
        return phase!=OperationPhase::Waiting && phase!=OperationPhase::Running;
    }
};
struct OperationSnapshot {
    std::uint64_t sequence=0;
    std::map<std::uint64_t,OperationStatus> entries;
};
}
