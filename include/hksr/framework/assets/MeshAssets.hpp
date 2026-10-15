#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace hksr::framework {
struct SubmeshRange { std::uint32_t first_index = 0, index_count = 0; };
struct MeshDescription {
    std::string renderer, directory;
    std::uint32_t vertex_count = 0, index_count = 0;
    bool uv1 = false;
    std::vector<SubmeshRange> submeshes;
    // Ordered palette, not the order of a live renderer's bones.
    std::vector<std::string> bone_paths;
};
enum class MaterialSource { Package, Template };
struct AssetManifest {
    MaterialSource materials = MaterialSource::Package;
    std::string material_file;
    std::vector<MeshDescription> meshes;
};
struct BoneWeights {
    std::array<float, 4> weights;
    std::array<std::int32_t, 4> indices;
};
struct MeshData {
    std::vector<std::array<float, 3>> positions, normals;
    std::vector<std::array<float, 4>> tangents;
    std::vector<std::array<std::uint8_t, 4>> colors;
    std::vector<std::array<float, 2>> uv0, uv1;
    std::vector<BoneWeights> bone_weights;
    std::vector<std::uint16_t> indices;
    std::vector<std::array<float, 16>> bindposes;
};
// Current sample payload: little-endian channels, UInt16 indices, four-weight
// skinning. This does not declare arbitrary new-format import complete.
AssetManifest ParseAssetManifest(std::string_view text);
MeshData LoadMeshData(const std::filesystem::path& asset_root, const MeshDescription& description);
// Shared content/runtime input validation, including direct typed callers.
// Does not infer vertex correspondence or normalize authored skin weights.
void ValidateMeshData(const MeshDescription& description, const MeshData& data);
}
