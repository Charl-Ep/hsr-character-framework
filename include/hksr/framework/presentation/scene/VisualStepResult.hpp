#pragma once
#include <string>

namespace hksr::framework {
enum class VisualStepState { Done, Pending, Failed, Skipped };
struct VisualStepResult {
    VisualStepState state = VisualStepState::Failed;
    std::string detail;
};
}
