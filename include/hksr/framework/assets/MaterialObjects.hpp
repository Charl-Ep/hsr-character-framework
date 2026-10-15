#pragma once
#include <hksr/unity/OwnedUnityObjects.hpp>
#include <hksr/unity/UnityAssetAccess.hpp>

namespace hksr::framework {
struct TextureBindingInput {
    void* managed_property_name=nullptr;
    void* texture=nullptr;
    api::Float2 scale{1,1}, offset{0,0};
};
struct TextureBindingResult {
    api::Observed<std::int32_t> property_id, texture_dimension;
    api::CallResult texture, scale, offset;
    bool ready=false;
    std::string error;
};
struct MaterialPreparation {
    api::UnityObjectId owned=0;
    api::CallResult initialize;
    api::ShaderProperties shader;
    std::vector<TextureBindingResult> bindings;
    bool ready=false;
    std::string error;
};
// Only immutable independently acquired templates are allowed. Allocate a fresh
// Material first; this function consumes its ownership even on failure. Names,
// textures and the template must be retained for the synchronous call. The
// resulting material must be detached before its owner is closed.
// Bindings override textures/transforms only; no numeric/keyword/queue replay.
MaterialPreparation PrepareMaterial(api::OwnedUnityObjects& owner, const api::MaterialAccess& material,
    const api::ShaderAccess& shader, const api::TextureAccess& textures, void* allocated_material,
    void* independent_template, const std::vector<TextureBindingInput>& bindings);
}
