#include <hksr/framework/assets/MaterialAssets.hpp>
#include "RelativePath.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace hksr::framework {
namespace {
// File records only. No pointer aliases escape parsing or survive a vector move.
#pragma pack(push, 1)
struct Header { char magic[8]; std::uint32_t version, textures, materials, keywords, properties, strings; };
struct TextureRecord {
    std::uint32_t name_offset, name_length, path_offset, path_length, width, height, format, mips;
    std::int32_t linear, filter, anisotropy;
    float mip_bias;
    std::int32_t wrap[3];
    std::uint64_t bytes;
    std::uint8_t unused_legacy_digest[32];
};
struct MaterialRecord { std::uint32_t role, slot, name_offset, name_length, shader_offset, shader_length,
    keyword_start, keyword_count, property_start, property_count; std::int32_t queue; };
struct KeywordRecord { std::uint32_t offset, length; };
struct PropertyRecord { std::uint32_t offset, length; std::int32_t unused_legacy_id;
    std::uint32_t kind; float value[4]; std::int32_t texture; float scale_offset[4]; };
#pragma pack(pop)
static_assert(sizeof(Header)==32 && sizeof(TextureRecord)==100 && sizeof(MaterialRecord)==44 &&
    sizeof(KeywordRecord)==8 && sizeof(PropertyRecord)==52);
void Need(bool value, const char* text) { if (!value) throw std::invalid_argument(text); }
template<class T> T Read(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    Need(offset <= bytes.size() && sizeof(T) <= bytes.size() - offset, "material record outside file");
    T value; std::memcpy(&value, bytes.data()+offset, sizeof(T)); return value;
}
void Range(std::uint32_t start, std::uint32_t count, std::uint32_t total) {
    Need(start <= total && count <= total-start, "material child range outside table");
}
}
MaterialAssets DecodeMaterialAssets(const std::vector<std::uint8_t>& bytes) {
    const auto header = Read<Header>(bytes, 0);
    Need(!std::memcmp(header.magic,"CPMAT02\0",8) && header.version==2, "unsupported material sample format");
    const std::size_t texture_start=sizeof(Header), material_start=texture_start+std::size_t(header.textures)*sizeof(TextureRecord);
    const auto keyword_start=material_start+std::size_t(header.materials)*sizeof(MaterialRecord);
    const auto property_start=keyword_start+std::size_t(header.keywords)*sizeof(KeywordRecord);
    const auto string_start=property_start+std::size_t(header.properties)*sizeof(PropertyRecord);
    Need(string_start+header.strings==bytes.size(), "material file table spans");
    auto text=[&](std::uint32_t offset,std::uint32_t length) {
        Need(offset < header.strings && length < header.strings-offset && !bytes[string_start+offset+length], "material string span/terminator");
        const auto* data=reinterpret_cast<const char*>(bytes.data()+string_start+offset);
        Need(!std::memchr(data,0,length), "embedded material string terminator");
        return std::string(data,length);
    };
    MaterialAssets output;
    output.textures.reserve(header.textures);
    for(std::size_t i=0;i<header.textures;++i) {
        const auto r=Read<TextureRecord>(bytes,texture_start+i*sizeof(TextureRecord));
        TextureDescription t;
        t.name=text(r.name_offset,r.name_length);t.payload=text(r.path_offset,r.path_length);
        detail::RelativePath(t.payload);
        constexpr auto max=std::numeric_limits<std::int32_t>::max();
        Need(r.width && r.width<=max && r.height && r.height<=max && r.mips && r.mips<=max && r.bytes && r.bytes<=max, "texture upload size outside engine Int32 range");
        Need(r.linear==0 || r.linear==1, "texture color space must be explicit");
        t.width=r.width;t.height=r.height;t.format=r.format;t.mip_count=r.mips;t.linear=r.linear!=0;
        t.filter=r.filter;t.anisotropy=r.anisotropy;t.mip_bias=r.mip_bias;t.payload_bytes=r.bytes;
        std::copy(std::begin(r.wrap),std::end(r.wrap),t.wrap.begin());
        output.textures.push_back(std::move(t));
    }
    output.materials.reserve(header.materials);
    for(std::size_t i=0;i<header.materials;++i) {
        const auto r=Read<MaterialRecord>(bytes,material_start+i*sizeof(MaterialRecord));
        Range(r.keyword_start,r.keyword_count,header.keywords);Range(r.property_start,r.property_count,header.properties);
        MaterialDescription m;
        m.renderer_role=r.role;m.slot=r.slot;m.name=text(r.name_offset,r.name_length);m.shader=text(r.shader_offset,r.shader_length);m.render_queue=r.queue;
        for(std::size_t j=0;j<r.keyword_count;++j) {
            const auto k=Read<KeywordRecord>(bytes,keyword_start+(r.keyword_start+j)*sizeof(KeywordRecord));
            m.keywords.push_back(text(k.offset,k.length));
        }
        for(std::size_t j=0;j<r.property_count;++j) {
            const auto p=Read<PropertyRecord>(bytes,property_start+(r.property_start+j)*sizeof(PropertyRecord));
            Need(p.texture>=-1 && (p.texture<0 || static_cast<std::uint32_t>(p.texture)<header.textures), "material texture index");
            MaterialProperty property;
            property.name=text(p.offset,p.length);property.kind=p.kind;property.texture_index=p.texture;
            std::copy(std::begin(p.value),std::end(p.value),property.value.begin());
            std::copy(std::begin(p.scale_offset),std::end(p.scale_offset),property.scale_offset.begin());
            m.properties.push_back(std::move(property));
        }
        output.materials.push_back(std::move(m));
    }
    return output;
}
MaterialAssets LoadMaterialAssets(const std::filesystem::path& file) {
    std::ifstream stream(file,std::ios::binary|std::ios::ate);
    if(!stream)throw std::runtime_error("material file open: "+file.u8string());
    const auto size=stream.tellg();
    if(size<0)throw std::runtime_error("material file size: "+file.u8string());
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));stream.seekg(0);
    if(!stream.read(reinterpret_cast<char*>(bytes.data()),size))throw std::runtime_error("material file read: "+file.u8string());
    return DecodeMaterialAssets(bytes);
}
}
