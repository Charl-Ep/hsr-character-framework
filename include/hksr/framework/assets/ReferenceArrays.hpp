#pragma once
#include <hksr/managed/ManagedRoots.hpp>
#include <hksr/managed/ManagedArrays.hpp>
#include <vector>
#include <string>

namespace hksr::framework {
struct ReferenceArrayPreparation {
    api::ObjectResult array;
    api::ManagedRootId root=0;
    std::vector<api::CallResult> writes;
    bool ready=false;
    std::string error;
};
// Element inputs/System.Type are rooted by their owners for this call. A new
// array receives its own root before writes; partial arrays remain releasable.
// Null slots are legal for material arrays; no fixed slot count is imposed.
ReferenceArrayPreparation PrepareReferenceArray(api::ManagedRoots& roots,const api::ManagedArrays& arrays,
    void* rooted_element_type,const std::vector<void*>& values);
struct BonePalettePreparation {
    std::vector<api::ObjectResult> bones;
    ReferenceArrayPreparation array;
    bool ready=false;
    std::string error;
};
// Paths have the authored palette order, relative to THIS instance's Art
// Transform. Missing paths never fall back to the player, another bone, or
// hierarchy enumeration order. Caller roots Art/type/path strings throughout.
BonePalettePreparation PrepareBonePalette(api::ManagedRoots& roots,const api::ManagedArrays& arrays,
    const api::HierarchyAccess& hierarchy,void* rooted_art_transform,void* rooted_transform_type,
    const std::vector<void*>& rooted_path_strings);
}
