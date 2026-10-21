#pragma once
#include "../catalog/SourceTypes.hpp"
#include "../catalog/ActionCatalogTypes.hpp"
#include "../assets/ModelPayload.hpp"
#include "../appearance/AppearanceTypes.hpp"
#include "../presentation/PresentationProfile.hpp"
#include "../presentation/VisualProgramState.hpp"
#include "../tasks/TaskControlTypes.hpp"

namespace hksr::framework {
struct VisualProgramInput;
enum class CharacterPersistence {Layout,Session};
// Immutable, copied selection for ONE NPC creation epoch. A missing battle
// source does not invalidate an exploration source. Payload IO happens before
// this object reaches the official world thread.
struct CharacterPlan {
    CharacterPackage package;
    std::shared_ptr<const ModelAssetInput> payload;
    std::string npc_look_source;
    std::optional<BattlePresentation> alternate;
    VisualRoot visual_root=VisualRoot::UniqueAnimator;
    std::vector<ActionSourceRoot> action_sources;
    std::vector<std::string> source_notes;
    // Original user selection, retained independently of a preparation request.
    // Layouts persist this identity, never reverse-parse display/package names.
    std::optional<BrowseSource> source_intent;
    std::shared_ptr<const PresentationProfile> presentation_profile;
    std::string presentation_phase;
    std::shared_ptr<const VisualProgramInput> visual_program;
    std::optional<VisualProgramSnapshot> visual_state;
    std::optional<TaskSelection> task_selection;
    CharacterPersistence persistence=CharacterPersistence::Layout;
    static CharacterPlan FromPrepared(const PreparedSource& source,
        std::shared_ptr<const ModelAssetInput> payload={},std::string npc_look_source={});
};
}
