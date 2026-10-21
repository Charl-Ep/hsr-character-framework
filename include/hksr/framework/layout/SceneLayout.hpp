#pragma once
#include "../presentation/ActionSelection.hpp"
#include "../catalog/SourceTypes.hpp"
#include "../presentation/VisualProgramState.hpp"
#include <filesystem>

namespace hksr::framework {
class SceneCharacters;
struct SavedConfiguredAction {ActionDomain domain=ActionDomain::Exploration;std::string state;bool controller_observed=false;};
struct SavedReadyAction {std::string source,state,skill;int variant=-1;};
using SavedAction=std::variant<NpcMotion,SavedConfiguredAction,SavedReadyAction,ProgramAction,BattleIdle,VisualActionKey>;
struct SavedCharacter {
    std::uint64_t key=0; // document-local identity; remapped on restoration
    BrowseSource source;
    CharacterPlacement placement;
    LookIntent look;
    bool enabled=true;
    std::optional<SavedAction> action;
    std::string presentation_phase;
    std::optional<VisualProgramSnapshot> visual_state;
};
struct SceneLayout {
    std::string scene;
    std::vector<SavedCharacter> characters;
};
// No engine pointers, cached plans or event/configuration graphs enter the file.
SceneLayout CaptureSceneLayout(const SceneCharacters&,const std::string& scene);
std::optional<PlacedAction> RestoreLayoutAction(const std::optional<SavedAction>&,const ActionCatalogStatus&);
std::string EncodeSceneLayout(const SceneLayout&);
SceneLayout DecodeSceneLayout(const std::string&);
void ValidateSceneLayout(const SceneLayout&);
bool SameLayout(const SceneLayout&,const SceneLayout&); // typed value comparison, no encoding/hashing

// Single worker owner. All file operations stay off the official game frame.
// Named snapshots are explicit; current.json is the automatic working state.
class LayoutStorage {
public:
    explicit LayoutStorage(std::filesystem::path root):root_(std::move(root)){}
    void SaveCurrent(const SceneLayout&);
    std::optional<SceneLayout> Current(const std::string& scene) const;
    void SaveNamed(const std::string& name,const SceneLayout&,bool overwrite=false);
    SceneLayout Named(const std::string& scene,const std::string& name) const;
    std::vector<std::string> Names(const std::string& scene) const;
    void DeleteNamed(const std::string& scene,const std::string& name);
private:
    std::filesystem::path Directory(const std::string&) const;
    std::filesystem::path NamedPath(const std::string&,const std::string&) const;
    static void Write(const std::filesystem::path&,const SceneLayout&,bool overwrite);
    static SceneLayout Read(const std::filesystem::path&,const std::string& scene);
    std::filesystem::path root_;
};
}
