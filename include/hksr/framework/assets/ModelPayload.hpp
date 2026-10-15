#pragma once
#include "CharacterPackage.hpp"
#include "MeshAssets.hpp"
#include "MaterialAssets.hpp"

namespace hksr::framework {
// File I/O belongs before engine-thread assembly. Plain owned data, no SDK
// references or process addresses. Call once per loaded package, not per frame.
struct ModelAssetInput {
    AssetManifest manifest;
    std::vector<MeshData> meshes;
    MaterialAssets materials;
    std::vector<std::vector<std::uint8_t>> texture_payloads;
    ShapeData shapes=ShapeData::None;
};
ModelAssetInput LoadModelPayload(const std::filesystem::path& package_directory,const CharacterPackage& package);
}
