#pragma once
#include <hksr/unity/RendererAccess.hpp>
#include <hksr/model/ModelServices.hpp>
#include <hksr/managed/ManagedRoots.hpp>
#include <hksr/unity/OwnedUnityObjects.hpp>
#include <memory>
#include <string>
#include <vector>

namespace hksr::framework {
struct RendererOverride {
    void* renderer = nullptr;
    void* mesh = nullptr;
    void* materials = nullptr;
    void* bones = nullptr; // null means leave the original palette unchanged
};
struct BindingReceipt {
    std::size_t index = 0;
    api::ObjectResult original_mesh, original_materials;
    api::CallResult apply_mesh, apply_materials, restore_mesh, restore_materials;
    bool mesh_written = false, materials_written = false;
    bool mesh_restored = true, materials_restored = true;
    api::ObjectResult original_bones;
    api::CallResult apply_bones, restore_bones;
    bool bones_written=false, bones_restored=true;
    api::Observed<bool> renderer_alive;
    bool renderer_destroyed=false; // not equivalent to restoration
};
struct BindingResult {
    bool complete = false;
    std::vector<std::string> errors;
};
// One model epoch, explicit engine-thread calls. Caller owns the private
// meshes/materials and must not destroy them until Restore completes.
// Retain this object through the SDK's before-model-release callback; a late
// restoration after pooling is not supported. No JSON/probe state dependency.
class RendererBindings {
public:
    explicit RendererBindings(std::shared_ptr<const api::ModelServices> services);
    ~RendererBindings(); // no engine calls; caller must Close at a valid boundary
    RendererBindings(const RendererBindings&) = delete;
    RendererBindings& operator=(const RendererBindings&) = delete;
    BindingResult Prepare(const std::vector<RendererOverride>& replacements);
    BindingResult Apply();
    BindingResult Restore();
    // Use this from the SDK before-release sink. Seals this epoch even if
    // restoration fails: a pooled renderer must never be written on a later retry.
    BindingResult BeforeModelRelease();
    void SealWithoutRestore() noexcept; // owner already gone; no late engine calls
    // Only after writes have been sealed. Observe each unresolved renderer,
    // never infer destruction from the parent/entity or destroy a borrowed one.
    BindingResult ObserveReleasedRenderers();
    BindingResult Close(); // restore, then free GC handles; never destroy borrowed assets
    std::vector<BindingReceipt> Receipts() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
