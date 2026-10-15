#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hksr::framework {
struct TextureDescription {
    std::string name, payload;
    std::uint32_t width = 0, height = 0, format = 0, mip_count = 0;
    bool linear = false;
    int filter = 0, anisotropy = 0;
    float mip_bias = 0;
    std::array<int, 3> wrap{};
    std::uint64_t payload_bytes = 0;
};
struct MaterialProperty {
    std::string name;
    std::uint32_t kind = 0; // retained source enum, texture = 4 in this format
    std::array<float, 4> value{}, scale_offset{};
    int texture_index = -1;
};
struct MaterialDescription {
    std::uint32_t renderer_role = 0, slot = 0;
    std::string name, shader;
    std::vector<std::string> keywords;
    std::vector<MaterialProperty> properties;
    int render_queue = -1;
};
struct MaterialAssets {
    std::vector<TextureDescription> textures;
    std::vector<MaterialDescription> materials;
};
// Decode existing CPMAT02 sample bytes without runtime pointers. Serialized
// property IDs and old payload digests are ignored; resolve names at runtime.
MaterialAssets DecodeMaterialAssets(const std::vector<std::uint8_t>& bytes);
MaterialAssets LoadMaterialAssets(const std::filesystem::path& file);
}
