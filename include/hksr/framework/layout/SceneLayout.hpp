#pragma once
#include <hksr/framework/catalog/ActionSelection.hpp>
#include <hksr/framework/catalog/SourceTypes.hpp>
#include <hksr/framework/character/ControlValues.hpp>
#include <hksr/framework/character/CharacterRestoreState.hpp>
#include <hksr/framework/presentation/scene/VisualProgramState.hpp>
#include <hksr/framework/tasks/TaskControlTypes.hpp>

namespace hksr::framework {
class SceneCharacters;
struct SavedConfiguredAction {
    ActionDomain domain = ActionDomain::Exploration;
    std::string state;
    bool controller_observed = false;
};
struct SavedReadyAction {
    std::string source, state, skill;
    int variant = -1;
};
using SavedAction =
    std::variant<NpcMotion, SavedConfiguredAction, SavedReadyAction, ProgramAction, BattleIdle, VisualActionKey>;
struct SavedCharacter {
    std::uint64_t key = 0; // document-local identity; remapped on restoration
    BrowseSource source;
    CharacterPlacement placement;
    LookIntent look;
    bool enabled = true;
    std::optional<SavedAction> action;
    std::string task_catalog;
    CharacterRestoreState restore;
};
struct SceneLayout {
    std::string scene;
    std::vector<SavedCharacter> characters;
};
// No engine pointers, cached plans or event/configuration graphs enter the file.
SceneLayout CaptureSceneLayout(const SceneCharacters&, const std::string& scene);
std::optional<PlacedAction> RestoreLayoutAction(const std::optional<SavedAction>&, const ActionCatalogStatus&,
                                                const api::VisualReactionInput* death_names = nullptr);
std::string EncodeSceneLayout(const SceneLayout&);
SceneLayout DecodeSceneLayout(const std::string&);
void ValidateSceneLayout(const SceneLayout&);
bool SameLayout(const SceneLayout&, const SceneLayout&); // typed value comparison, no encoding/hashing

}
