#include <hksr/framework/assets/CharacterPackage.hpp>
#include <hksr/framework/assets/MeshAssets.hpp>
#include <hksr/framework/assets/MaterialAssets.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace hksr::framework;
using Json=nlohmann::json;
static void Check(bool value){if(!value)throw std::runtime_error("content test failed");}
static bool Rejected(const Json& row){try{ParseCharacterPackage(row.dump());return false;}catch(const std::exception&){return true;}}
static bool AssetsRejected(const Json& row){try{ParseAssetManifest(row.dump());return false;}catch(const std::exception&){return true;}}
static std::string ReadText(const std::filesystem::path& file) {
    std::ifstream stream(file,std::ios::binary);Check(bool(stream));
    return std::string((std::istreambuf_iterator<char>(stream)),{});
}
static bool MaterialsRejected(const std::vector<std::uint8_t>& bytes) {
    try{DecodeMaterialAssets(bytes);return false;}catch(const std::exception&){return true;}
}
int main(int argc,char** argv){
    Json mesh={{"renderer","Body"},{"directory","meshes/body"},{"vertex_count",3},{"index_count",3},
        {"bone_paths",{"Root"}},{"submeshes",{3}}};
    Json assets={{"format","chara-placement.template-overlay-assets-v1"},{"material_source","template"},{"meshes",{mesh}}};
    auto manifest=ParseAssetManifest(assets.dump());
    Check(manifest.materials==MaterialSource::Template && manifest.meshes[0].submeshes[0].first_index==0);
    auto bad=assets;bad["meshes"][0]["submeshes"]={6};Check(AssetsRejected(bad));
    bad=assets;bad["meshes"][0]["submeshes"]={2};Check(AssetsRejected(bad));
    bad=assets;bad["meshes"][0]["vertex_count"]=1.5;Check(AssetsRejected(bad));
    bad=assets;bad["meshes"].push_back(mesh);Check(AssetsRejected(bad));
    bad=assets;bad["meshes"][0]["directory"]="../body";Check(AssetsRejected(bad));
    bad=assets;bad["meshes"][0]["bone_paths"]=Json::array();Check(AssetsRejected(bad));
    Check(MaterialsRejected({}));
    Json npc={{"format","hksr.character-package"},{"schema",1},{"id","sample"},{"display_name","sample"},
              {"source",{{"kind","official"},{"npc_id",1217}}}};
    auto value=ParseCharacterPackage(npc.dump());Check(value.npc_id==1217 && !value.appearance);
    auto external=npc;external["source"]["kind"]="external";Check(Rejected(external));
    external["source"]["appearance"]={{"template_prefab","Character.prefab"},{"character_config","Character.json"},{"asset_directory","assets"},{"blendshape","none"}};
    value=ParseCharacterPackage(external.dump());Check(value.appearance->shapes==ShapeData::None && value.appearance->asset_directory=="assets");
    auto invalid=external;invalid["source"]["appearance"]["asset_directory"]="../other";Check(Rejected(invalid));
    invalid=external;invalid["source"]["appearance"]["asset_directory"]="C:\\other";Check(Rejected(invalid));
    invalid=external;invalid["animation_states"]={"Skill01"};Check(Rejected(invalid));
    invalid=npc;invalid["source"]["npc_id"]=-1;Check(Rejected(invalid));
    invalid=npc;invalid["source"]["npc_id"]=1.5;Check(Rejected(invalid));
    invalid=external;invalid["source"]["appearance"]["blendshape"]="adapt_any_mesh";Check(Rejected(invalid));
    for(int i=1;i<argc;++i){
        std::ifstream stream(argv[i],std::ios::binary);Check(bool(stream));
        std::string text((std::istreambuf_iterator<char>(stream)),{});
        const auto package=ParseCharacterPackage(text);Check(!package.id.empty());
        std::cout<<package.id<<" parsed\n";
        if(package.source==SourceKind::External){
            const auto root=std::filesystem::u8path(argv[i]).parent_path()/std::filesystem::u8path(package.appearance->asset_directory);
            const auto supplied=ParseAssetManifest(ReadText(root/"assets.json"));
            std::size_t vertices=0;
            for(const auto& description:supplied.meshes){
                const auto data=LoadMeshData(root,description);
                Check(data.positions.size()==description.vertex_count && data.indices.size()==description.index_count);
                Check(data.bindposes.size()==description.bone_paths.size());
                vertices+=data.positions.size();
            }
            auto wrong=supplied.meshes.front();++wrong.vertex_count;
            bool failed=false;try{LoadMeshData(root,wrong);}catch(const std::exception&){failed=true;}
            Check(failed); // real byte layout, not a textual source-code check
            std::cout<<"  "<<supplied.meshes.size()<<" mesh payloads loaded, "<<vertices<<" vertices\n";
            if(supplied.materials==MaterialSource::Package){
                const auto materials=LoadMaterialAssets(root/std::filesystem::u8path(supplied.material_file));
                for(const auto& material:materials.materials){
                    Check(material.renderer_role<supplied.meshes.size());
                    Check(material.slot<supplied.meshes[material.renderer_role].submeshes.size());
                }
                for(const auto& texture:materials.textures)
                    Check(std::filesystem::file_size(root/std::filesystem::u8path(texture.payload))==texture.payload_bytes);
                std::cout<<"  "<<materials.materials.size()<<" materials, "<<materials.textures.size()<<" texture descriptors; no texture upload\n";
                const auto binary=ReadText(root/std::filesystem::u8path(supplied.material_file));
                std::vector<std::uint8_t> bytes(binary.begin(),binary.end());
                auto broken=bytes;broken.pop_back();Check(MaterialsRejected(broken));
                broken=bytes;broken[8]=3;Check(MaterialsRejected(broken));
                broken=bytes;broken[32]=255;broken[33]=255;broken[34]=255;broken[35]=255;Check(MaterialsRejected(broken));
                // The old payload digest is not a prerequisite and is never
                // recomputed. Shader property IDs are also not runtime keys.
                auto changed=bytes;changed[32+68]^=255;
                Check(DecodeMaterialAssets(changed).textures[0].name==materials.textures[0].name);
            }
        }
    }
    std::cout<<"Content behavior tests passed; no game acceptance implied.\n";
}
