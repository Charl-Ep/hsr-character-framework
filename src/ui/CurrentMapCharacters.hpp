#pragma once
#include <hksr/framework/application/ApplicationService.hpp>
#include <algorithm>

namespace hksr::framework::ui {
// The registry retains other maps for restoration. Only this view is local
// to the current map; disabled and not-yet-created entries remain editable.
inline bool IsCurrentMapCharacter(const ApplicationSnapshot& snapshot, const SceneCharacterStatus& character) {
    return snapshot.scene && !snapshot.scene->key.empty() && character.scene == snapshot.scene->key &&
           !character.deleting;
}
inline const SceneCharacterStatus* FindCurrentMapCharacter(const ApplicationSnapshot& snapshot, std::uint64_t id) {
    const auto found = std::find_if(snapshot.characters.begin(), snapshot.characters.end(), [&](const auto& row) {
        return row.id == id && IsCurrentMapCharacter(snapshot, row);
    });
    return found == snapshot.characters.end() ? nullptr : &*found;
}
inline const SceneCharacterStatus* FirstCurrentMapCharacter(const ApplicationSnapshot& snapshot) {
    const auto found = std::find_if(snapshot.characters.begin(), snapshot.characters.end(), [&](const auto& row) {
        return IsCurrentMapCharacter(snapshot, row);
    });
    return found == snapshot.characters.end() ? nullptr : &*found;
}
}
