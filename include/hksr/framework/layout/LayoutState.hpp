#pragma once
#include <cstdint>
#include <string>
#include <map>
#include <vector>

namespace hksr::framework {
enum class LayoutOperation { Refresh, Save, Load, Delete, RetryCurrent, KeepCurrent };
enum class LayoutPhase { Idle, Reading, Preparing, Removing, Applying, Ready, Failed, Io, Cancelled };
enum class LayoutActivity { None, RestoreCurrent, Refresh, Save, Load, DeleteSnapshot, RetrySave, KeepCurrent };
inline const char* LayoutPhaseName(LayoutPhase phase) {
    switch (phase) {
    case LayoutPhase::Idle:
        return "";
    case LayoutPhase::Reading:
        return "reading";
    case LayoutPhase::Preparing:
        return "preparing";
    case LayoutPhase::Removing:
        return "removing";
    case LayoutPhase::Applying:
        return "applying";
    case LayoutPhase::Ready:
        return "ready";
    case LayoutPhase::Failed:
        return "failed";
    case LayoutPhase::Io:
        return "io";
    case LayoutPhase::Cancelled:
        return "cancelled";
    }
    return "";
}
inline const char* LayoutActivityName(LayoutActivity operation) {
    switch (operation) {
    case LayoutActivity::None:
        return "";
    case LayoutActivity::RestoreCurrent:
        return "restore_current";
    case LayoutActivity::Refresh:
        return "refresh";
    case LayoutActivity::Save:
        return "save";
    case LayoutActivity::Load:
        return "load";
    case LayoutActivity::DeleteSnapshot:
        return "delete_snapshot";
    case LayoutActivity::RetrySave:
        return "retry_save";
    case LayoutActivity::KeepCurrent:
        return "keep_current";
    }
    return "";
}
struct LayoutCommand {
    LayoutOperation operation;
    std::string name;
    bool overwrite = false;
    std::string scene;
};
struct MapLayoutStatus {
    bool initialized = false, busy = false;
    LayoutPhase phase = LayoutPhase::Idle;
    LayoutActivity operation = LayoutActivity::None;
    std::string error, storage_error, name;
    std::uint64_t command = 0, revision = 0, saved_revision = 0;
    std::vector<std::string> names;
};
struct LayoutSnapshot {
    bool available = false;
    std::uint64_t sequence = 0;
    std::map<std::string, MapLayoutStatus> maps;
};

}
