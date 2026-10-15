#pragma once
#include "MaterialAssets.hpp"
#include <hksr/unity/OwnedUnityObjects.hpp>
#include <hksr/unity/UnityAssetAccess.hpp>

namespace hksr::framework {
struct TexturePreparation {
    api::UnityObjectId owned = 0;
    api::CallResult initialize;
    api::Observed<bool> load;
    api::TextureSamplingResult sampling;
    api::CallResult apply;
    bool ready = false;
    std::string error;
};
// Consumes a fresh AllocateObject(Texture2D) result, not a shared template or
// an existing texture. Ownership is registered BEFORE the Unity constructor.
// The caller must preserve the owner even on failure, and close it after all
// Renderer/material references have been restored or detached.
TexturePreparation PrepareTexture(api::OwnedUnityObjects& owner, const api::TextureAccess& api,
    void* allocated_texture, const TextureDescription& description, const std::vector<std::uint8_t>& payload);
}
