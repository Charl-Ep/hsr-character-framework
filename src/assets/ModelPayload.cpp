#include <hksr/framework/assets/ModelPayload.hpp>
#include "RelativePath.hpp"
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace hksr::framework {
ModelAssetInput LoadModelPayload(const std::filesystem::path& directory,const CharacterPackage& package) {
    if(package.source!=SourceKind::External || !package.appearance)
        throw std::invalid_argument("only external packages provide mesh payloads");
    const auto& appearance=*package.appearance; detail::RelativePath(appearance.asset_directory);
    const auto root=directory/std::filesystem::u8path(appearance.asset_directory);
    std::ifstream manifest(root/"assets.json",std::ios::binary);
    if(!manifest) throw std::runtime_error("asset manifest open: "+(root/"assets.json").u8string());
    const std::string text((std::istreambuf_iterator<char>(manifest)),{});
    ModelAssetInput out; out.shapes=appearance.shapes; out.manifest=ParseAssetManifest(text);
    for(const auto& d:out.manifest.meshes) out.meshes.push_back(LoadMeshData(root,d));
    if(out.manifest.materials==MaterialSource::Package) {
        out.materials=LoadMaterialAssets(root/std::filesystem::u8path(out.manifest.material_file));
        for(const auto& d:out.materials.textures) {
            detail::RelativePath(d.payload);
            const auto path=root/std::filesystem::u8path(d.payload);
            std::ifstream stream(path,std::ios::binary|std::ios::ate);
            if(!stream || d.payload_bytes>static_cast<std::uint64_t>((std::numeric_limits<std::int32_t>::max)()) ||
                stream.tellg()!=static_cast<std::streamoff>(d.payload_bytes))
                throw std::runtime_error("texture payload length/open: "+path.u8string());
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(d.payload_bytes)); stream.seekg(0);
            if(!stream.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))
                throw std::runtime_error("texture payload read: "+path.u8string());
            out.texture_payloads.push_back(std::move(bytes));
        }
    }
    return out;
}
}
