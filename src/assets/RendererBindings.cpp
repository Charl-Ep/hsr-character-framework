#include <hksr/framework/assets/RendererBindings.hpp>
#include <array>
#include <set>
#include <stdexcept>

namespace hksr::framework {
namespace {
using Root = api::ManagedRootId;
enum Slot : std::size_t { Renderer, OriginalMesh, OriginalMaterials, NewMesh, NewMaterials, OriginalBones, NewBones, Count };
struct Binding { BindingReceipt receipt; std::array<Root, Count> roots{}; };
}
struct RendererBindings::State {
    std::shared_ptr<const api::ModelServices> services;
    const api::RendererAccess& access;
    std::unique_ptr<api::ManagedRoots> roots;
    std::vector<Root> all_roots;
    std::vector<Binding> bindings;
    bool prepared = false, started = false, applied = false, closing = false;
    bool release_boundary_seen = false;
    bool bone_methods=false;
    explicit State(std::shared_ptr<const api::ModelServices> input) : services(std::move(input)), access(services->Renderers()), roots(services->CreateRoots()), bone_methods(services->CanOverrideBones()) {
        if (!services->CanOverrideMeshAndMaterials())
            throw std::invalid_argument("renderer overrides require paired read and restore methods");
    }
    Root Keep(void* object) {
        if (!object) return 0; // null mesh is a valid original reference
        // Reserve bookkeeping before acquiring a handle.
        all_roots.push_back(0);
        const auto id = roots->Keep(object); all_roots.back() = id;
        if (!roots->Status(id).ready) throw std::runtime_error("failed to root renderer input");
        return id;
    }
    void* Target(Root id) {
        if (!id) return nullptr;
        const auto target = roots->Target(id);
        if (!target.call.returned || !target.object) throw std::runtime_error("renderer input GC target unavailable");
        return target.object;
    }
};
RendererBindings::RendererBindings(std::shared_ptr<const api::ModelServices> services) : state_(std::make_unique<State>(std::move(services))) {}
RendererBindings::~RendererBindings() = default;
BindingResult RendererBindings::Prepare(const std::vector<RendererOverride>& replacements) {
    if (state_->started) throw std::logic_error("one binding set describes one model epoch");
    state_->started = true;
    BindingResult result;
    std::set<void*> seen;
    // Validate identities before any engine side effect. This is an ownership
    // requirement, not a gate on unrelated roles or experimental observations.
    for (const auto& input : replacements) {
        if (!input.renderer || !seen.insert(input.renderer).second || !input.materials)
            throw std::invalid_argument("renderer overrides require unique renderers and material arrays");
    }
    state_->bindings.resize(replacements.size());
    for (std::size_t i = 0; i < replacements.size(); ++i) {
        auto& binding = state_->bindings[i]; auto& receipt = binding.receipt; receipt.index = i;
        const auto& input = replacements[i];
        try {
            binding.roots[Renderer] = state_->Keep(input.renderer);
            receipt.original_mesh = state_->access.Mesh(state_->Target(binding.roots[Renderer]));
            if (!receipt.original_mesh.call.returned) throw std::runtime_error("read original mesh failed");
            binding.roots[OriginalMesh] = state_->Keep(receipt.original_mesh.object);
            receipt.original_materials = state_->access.Materials(state_->Target(binding.roots[Renderer]));
            if (!receipt.original_materials.call.returned || !receipt.original_materials.object) throw std::runtime_error("read original material array failed");
            binding.roots[OriginalMaterials] = state_->Keep(receipt.original_materials.object);
            binding.roots[NewMesh] = state_->Keep(input.mesh);
            binding.roots[NewMaterials] = state_->Keep(input.materials);
            if(input.bones) {
                if(!state_->bone_methods) throw std::runtime_error("bone override requires paired read/restore methods");
                receipt.original_bones=state_->access.Bones(state_->Target(binding.roots[Renderer]));
                if(!receipt.original_bones.call.returned) throw std::runtime_error("read original bone array failed");
                binding.roots[OriginalBones]=state_->Keep(receipt.original_bones.object);
                binding.roots[NewBones]=state_->Keep(input.bones);
            }
        } catch (const std::exception& error) { result.errors.push_back(std::to_string(i) + ": " + error.what()); }
    }
    // Capture every original before assigning any role, including late failures.
    result.complete = result.errors.empty(); state_->prepared = result.complete;
    return result;
}
BindingResult RendererBindings::Apply() {
    if (!state_->prepared || state_->closing || state_->applied) throw std::logic_error("renderer bindings are not prepared for a first application");
    state_->applied = true;
    BindingResult result;
    for (auto& binding : state_->bindings) {
        auto& receipt = binding.receipt;
        try {
            if(state_->closing) throw std::runtime_error("model ended during publication");
            auto* renderer = state_->Target(binding.roots[Renderer]);
            auto* mesh = state_->Target(binding.roots[NewMesh]);
            auto* materials = state_->Target(binding.roots[NewMaterials]);
            // A throwing setter may already have written. Include it in rollback.
            receipt.mesh_written = true; receipt.mesh_restored = false;
            receipt.apply_mesh = state_->access.SetMesh(renderer, mesh);
            if(state_->closing) throw std::runtime_error("model ended during mesh publication");
            receipt.materials_written = true; receipt.materials_restored = false;
            receipt.apply_materials = state_->access.SetMaterials(renderer, materials);
            if(state_->closing) throw std::runtime_error("model ended during material publication");
            if(binding.roots[NewBones]) {
                auto* bones=state_->Target(binding.roots[NewBones]);
                receipt.bones_written=true; receipt.bones_restored=false;
                receipt.apply_bones=state_->access.SetBones(renderer,bones);
                if(state_->closing) throw std::runtime_error("model ended during bone publication");
                if(!receipt.apply_bones.returned) throw std::runtime_error("bone assignment failed");
            }
            if (!receipt.apply_mesh.returned || !receipt.apply_materials.returned) throw std::runtime_error("renderer assignment failed");
        } catch (const std::exception& error) { result.errors.push_back(std::to_string(receipt.index) + ": " + error.what()); }
    }
    result.complete = result.errors.empty();
    if (!result.complete) {
        const auto rollback = Restore();
        for (const auto& error : rollback.errors) result.errors.push_back("rollback " + error);
    }
    return result;
}
BindingResult RendererBindings::Restore() {
    state_->closing = true;
    BindingResult result;
    for (auto& binding : state_->bindings) {
        auto& receipt = binding.receipt;
        if (receipt.renderer_destroyed || (receipt.mesh_restored && receipt.materials_restored && receipt.bones_restored)) continue;
        if (state_->release_boundary_seen) {
            result.errors.push_back(std::to_string(receipt.index) + ": restoration unresolved at model release; no late writes permitted");
            continue;
        }
        void* renderer = nullptr;
        try { renderer = state_->Target(binding.roots[Renderer]); }
        catch (const std::exception& error) { result.errors.push_back(std::to_string(receipt.index) + ": " + error.what()); continue; }
        // Independent assignments: one failure must not prevent the other restore.
        if (!receipt.mesh_restored) {
            try {
                receipt.restore_mesh = state_->access.SetMesh(renderer, state_->Target(binding.roots[OriginalMesh]));
                receipt.mesh_restored = receipt.restore_mesh.returned;
                if (!receipt.mesh_restored) throw std::runtime_error("mesh restore failed");
            } catch (const std::exception& error) { result.errors.push_back(std::to_string(receipt.index) + ": " + error.what()); }
        }
        if (!receipt.materials_restored) {
            try {
                receipt.restore_materials = state_->access.SetMaterials(renderer, state_->Target(binding.roots[OriginalMaterials]));
                receipt.materials_restored = receipt.restore_materials.returned;
                if (!receipt.materials_restored) throw std::runtime_error("materials restore failed");
            } catch (const std::exception& error) { result.errors.push_back(std::to_string(receipt.index) + ": " + error.what()); }
        }
        if(!receipt.bones_restored) {
            try {
                receipt.restore_bones=state_->access.SetBones(renderer,state_->Target(binding.roots[OriginalBones]));
                receipt.bones_restored=receipt.restore_bones.returned;
                if(!receipt.bones_restored) throw std::runtime_error("bones restore failed");
            } catch(const std::exception& error) { result.errors.push_back(std::to_string(receipt.index)+": "+error.what()); }
        }
    }
    result.complete = result.errors.empty(); return result;
}
BindingResult RendererBindings::BeforeModelRelease() {
    try {
        auto result = Restore();
        state_->release_boundary_seen = true;
        return result;
    } catch(...) {
        state_->closing=true; state_->release_boundary_seen=true;
        throw;
    }
}
void RendererBindings::SealWithoutRestore() noexcept { state_->closing=true; state_->release_boundary_seen=true; }
BindingResult RendererBindings::ObserveReleasedRenderers() {
    BindingResult result;
    if(!state_->release_boundary_seen) {
        result.errors.push_back("renderer writes have not reached their release boundary"); return result;
    }
    for(auto& binding:state_->bindings) {
        auto& receipt=binding.receipt;
        if(receipt.renderer_destroyed || (receipt.mesh_restored && receipt.materials_restored && receipt.bones_restored)) continue;
        try {
            receipt.renderer_alive=state_->services->Alive(state_->Target(binding.roots[Renderer]));
            if(receipt.renderer_alive.read.returned && !receipt.renderer_alive.value) receipt.renderer_destroyed=true;
            else result.errors.push_back(std::to_string(receipt.index)+": renderer still alive or destruction unobserved");
        } catch(const std::exception& error) { result.errors.push_back(std::to_string(receipt.index)+": "+error.what()); }
    }
    result.complete=result.errors.empty(); return result;
}
BindingResult RendererBindings::Close() {
    auto result = Restore();
    if (!result.complete) return result; // references are still needed for restoration
    for (auto& id : state_->all_roots) {
        if (!id) continue;
        if (state_->roots->Release(id).complete) { state_->roots->Forget(id); id = 0; }
        else result.errors.push_back("GC root " + std::to_string(id) + " release unresolved");
    }
    result.complete = result.errors.empty(); return result;
}
std::vector<BindingReceipt> RendererBindings::Receipts() const {
    std::vector<BindingReceipt> result;
    for (const auto& binding : state_->bindings) result.push_back(binding.receipt);
    return result;
}
}
