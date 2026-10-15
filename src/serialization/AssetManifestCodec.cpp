#include <hksr/framework/assets/MeshAssets.hpp>
#include "../assets/RelativePath.hpp"
#include <nlohmann/json.hpp>
#include <limits>
#include <set>
#include <stdexcept>

namespace hksr::framework {
namespace {
using Json=nlohmann::json;
void Need(bool value,const char* message){if(!value)throw std::invalid_argument(message);}
std::uint32_t Count(const Json& value) {
    Need(value.is_number_integer(), "mesh counts must be integers");
    const auto count = value.get<std::int64_t>();
    Need(count > 0 && count <= std::numeric_limits<std::int32_t>::max(), "mesh count outside engine Int32 range");
    return static_cast<std::uint32_t>(count);
}
}
AssetManifest ParseAssetManifest(std::string_view text) {
    const auto data = Json::parse(text.begin(), text.end());
    Need(data.at("format") == "chara-placement.template-overlay-assets-v1", "unsupported sample asset format");
    AssetManifest out;
    const auto source = data.value("material_source", std::string("package"));
    Need(source == "package" || source == "template", "unsupported material source");
    out.materials = source == "template" ? MaterialSource::Template : MaterialSource::Package;
    if (out.materials == MaterialSource::Package) {
        out.material_file = data.at("materials").get<std::string>();
        detail::RelativePath(out.material_file);
    }
    std::set<std::string> roles;
    for (const auto& item : data.at("meshes")) {
        MeshDescription mesh;
        mesh.renderer = item.at("renderer").get<std::string>();
        Need(!mesh.renderer.empty() && roles.insert(mesh.renderer).second, "mesh renderer role missing/duplicate");
        mesh.directory = item.at("directory").get<std::string>();
        detail::RelativePath(mesh.directory);
        mesh.vertex_count = Count(item.at("vertex_count"));
        mesh.index_count = Count(item.at("index_count"));
        mesh.uv1 = item.value("uv1", false);
        mesh.bone_paths = item.at("bone_paths").get<std::vector<std::string>>();
        Need(!mesh.bone_paths.empty() && mesh.bone_paths.size() <= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()), "mesh bone palette range");
        std::uint32_t first = 0;
        for (const auto& size : item.at("submeshes")) {
            const auto count = Count(size);
            Need(count % 3 == 0 && count <= mesh.index_count - first, "submesh triangle range");
            mesh.submeshes.push_back({first, count}); first += count;
        }
        Need(first == mesh.index_count, "submesh ranges must cover supplied indices");
        out.meshes.push_back(std::move(mesh));
    }
    Need(!out.meshes.empty(), "external asset package has no meshes");
    return out;
}
}
