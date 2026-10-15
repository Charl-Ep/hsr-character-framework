#include <hksr/framework/assets/MeshAssets.hpp>
#include "RelativePath.hpp"
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace hksr::framework {
namespace {
void Need(bool value, const char* message) { if (!value) throw std::invalid_argument(message); }
template<class T> std::vector<T> Read(const std::filesystem::path& file, std::size_t count) {
    const auto expected = count * sizeof(T);
    std::ifstream stream(file, std::ios::binary | std::ios::ate);
    if (!stream || stream.tellg() != static_cast<std::streamoff>(expected))
        throw std::runtime_error("mesh channel byte length/open: " + file.u8string());
    stream.seekg(0);
    std::vector<T> output(count);
    if (!stream.read(reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(expected)))
        throw std::runtime_error("mesh channel read: " + file.u8string());
    return output;
}
}
MeshData LoadMeshData(const std::filesystem::path& asset_root, const MeshDescription& mesh) {
    static_assert(sizeof(BoneWeights) == 32 && sizeof(float) == 4);
    // Keep the loader usable directly by a runtime, not only via JSON parsing.
    detail::RelativePath(mesh.directory);
    Need(mesh.vertex_count && mesh.index_count && !mesh.bone_paths.empty(), "empty mesh input");
    const auto root = asset_root / std::filesystem::u8path(mesh.directory);
    MeshData out;
    out.positions = Read<std::array<float, 3>>(root / "position.buf", mesh.vertex_count);
    out.normals = Read<std::array<float, 3>>(root / "normal.buf", mesh.vertex_count);
    out.tangents = Read<std::array<float, 4>>(root / "tangent.buf", mesh.vertex_count);
    out.colors = Read<std::array<std::uint8_t, 4>>(root / "color.buf", mesh.vertex_count);
    out.uv0 = Read<std::array<float, 2>>(root / "uv0.buf", mesh.vertex_count);
    if (mesh.uv1) out.uv1 = Read<std::array<float, 2>>(root / "uv1.buf", mesh.vertex_count);
    out.bone_weights = Read<BoneWeights>(root / "bone-weights.buf", mesh.vertex_count);
    out.indices = Read<std::uint16_t>(root / "indices.buf", mesh.index_count);
    out.bindposes = Read<std::array<float, 16>>(root / "bindposes.buf", mesh.bone_paths.size());
    ValidateMeshData(mesh,out);
    return out;
}
void ValidateMeshData(const MeshDescription& mesh,const MeshData& out) {
    const auto max=static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)());
    Need(mesh.vertex_count && mesh.vertex_count<=max && mesh.index_count && mesh.index_count<=max &&
        !mesh.bone_paths.empty() && mesh.bone_paths.size()<=max && !mesh.submeshes.empty() && mesh.submeshes.size()<=max,
        "mesh counts outside native contract");
    Need(out.positions.size()==mesh.vertex_count && out.normals.size()==mesh.vertex_count &&
        out.tangents.size()==mesh.vertex_count && out.colors.size()==mesh.vertex_count && out.uv0.size()==mesh.vertex_count &&
        out.uv1.size()==(mesh.uv1?mesh.vertex_count:0) && out.bone_weights.size()==mesh.vertex_count &&
        out.indices.size()==mesh.index_count && out.bindposes.size()==mesh.bone_paths.size(),"mesh channel count mismatch");
    std::uint32_t first=0;
    for(const auto range:mesh.submeshes) {
        Need(range.first_index==first && range.index_count && range.index_count%3==0 &&
            range.index_count<=mesh.index_count-first,"mesh submesh triangle range");
        first+=range.index_count;
    }
    Need(first==mesh.index_count,"mesh submeshes must cover indices");
    for (const auto index : out.indices) Need(index < mesh.vertex_count, "index outside supplied vertex stream");
    for (const auto& vertex : out.bone_weights)
        for (std::size_t i = 0; i < 4; ++i)
            Need(std::isfinite(vertex.weights[i]) && vertex.weights[i] >= 0 && vertex.indices[i] >= 0 &&
                static_cast<std::size_t>(vertex.indices[i]) < mesh.bone_paths.size(), "skin weight/palette reference");
    const auto finite=[](const auto& channel) {
        for(const auto& element:channel) for(const auto value:element)
            Need(std::isfinite(value),"nonfinite mesh channel");
    };
    finite(out.positions); finite(out.normals); finite(out.tangents); finite(out.uv0); finite(out.uv1); finite(out.bindposes);
}
}
