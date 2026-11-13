#pragma once
#include <hksr/framework/presentation/scene/VisualInstruction.hpp>
#include <hksr/framework/presentation/scene/VisualProgramState.hpp>
#include <hksr/animation/OfficialCatalog.hpp>
#include <hksr/presentation/effects/EffectInputs.hpp>
#include <hksr/presentation/effects/EffectMutex.hpp>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace hksr::framework {
struct VisualEffectInput {
    std::string path, target;
    api::EntityEffectOptions options;
    std::string anchor; // named world pose, mutually exclusive with target
    bool allow_position = false;
    std::optional<api::EffectMutexRule> mutex;
    api::EffectSubmissionOptions submission;
};
struct VisualProgramContext {
    std::vector<VisualInstruction> enter, exit;
    std::string event_scope, phase;
    std::optional<std::vector<VisualInstruction>> restore;
    std::set<std::string> restore_effects;
};
struct VisualProgramSignalDefinition {
    std::string context;
    std::vector<VisualInstruction> instructions;
};
struct VisualCue {
    float at = 0;
    std::vector<VisualInstruction> instructions;
};
struct VisualProgramAction {
    std::string state;
    bool restart = false;
    std::vector<VisualInstruction> enter;
    // Independent lanes advance from the same observed body clock. Pending
    // resource control in one lane does not stop unrelated visual work.
    std::vector<std::vector<VisualCue>> lanes;
    std::string completion_state;
    float complete_at = 1;
    float completion_at = 0; // observed successor normalized progress, not elapsed time
    std::vector<VisualInstruction> after_completion;
};
struct VisualProgram {
    std::string id, source, scope;
    VisualTargetDefaults target_defaults;
    std::map<std::string, VisualEffectInput> effects;
    std::map<std::string, VisualPartnerInput> partners;
    std::map<std::string, VisualProgramAction> actions;
    std::map<std::string, VisualProgramSignalDefinition> signals;
    std::map<std::string, VisualProgramContext> contexts;
    std::map<std::string, api::PhaseVisualSource> phases;
    std::map<std::string, api::DefinitionSource> projectiles;
    std::optional<std::vector<VisualInstruction>> restore;
};
VisualProgram ParseVisualProgram(const std::string& json);
void ValidateVisualRestore(const VisualProgram&, const VisualProgramSnapshot&);
}
