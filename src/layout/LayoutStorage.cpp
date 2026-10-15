#include <hksr/framework/layout/SceneLayout.hpp>
#include <Windows.h>
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace hksr::framework {
namespace {
void Need(bool value,const char* message){if(!value)throw std::invalid_argument(message);}
}
std::filesystem::path LayoutStorage::Directory(const std::string& scene) const {
    Need(!scene.empty(),"map identity absent");
    std::string name="map-";const char* hex="0123456789ABCDEF";
    for(unsigned char c:scene) {
        if((c>='0'&&c<='9')||(c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='-'||c=='_')name+=static_cast<char>(c);
        else {name+='%';name+=hex[c>>4];name+=hex[c&15];}
    }
    return root_/name;
}
std::filesystem::path LayoutStorage::NamedPath(const std::string& scene,const std::string& name) const {
    Need(!name.empty() && name.back()!='.' && name.back()!=' ' &&
        name.find_first_of("<>:\"/\\|?*")==std::string::npos &&
        std::none_of(name.begin(),name.end(),[](unsigned char c){return c<32;}),"invalid snapshot name");
    // Prefix avoids Windows reserved device names; the user-facing name is kept.
    return Directory(scene)/"snapshots"/std::filesystem::u8path("layout-"+name+".json");
}
void LayoutStorage::Write(const std::filesystem::path& file,const SceneLayout& value,bool overwrite) {
    const auto bytes=EncodeSceneLayout(value);
    std::filesystem::create_directories(file.parent_path());
    auto temporary=file;temporary+=L".tmp";
    {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out.exceptions(std::ios::badbit|std::ios::failbit);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));out.close();}
    const auto flags=MOVEFILE_WRITE_THROUGH|(overwrite?MOVEFILE_REPLACE_EXISTING:0);
    if(!MoveFileExW(temporary.c_str(),file.c_str(),flags))throw std::system_error(GetLastError(),std::system_category(),"save layout");
}
SceneLayout LayoutStorage::Read(const std::filesystem::path& file,const std::string& scene) {
    std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("cannot read layout: "+file.u8string());
    auto value=DecodeSceneLayout(std::string((std::istreambuf_iterator<char>(input)),{}));
    Need(value.scene==scene,"layout belongs to a different map");return value;
}
void LayoutStorage::SaveCurrent(const SceneLayout& value){Write(Directory(value.scene)/"current.json",value,true);}
std::optional<SceneLayout> LayoutStorage::Current(const std::string& scene) const {
    const auto file=Directory(scene)/"current.json";if(!std::filesystem::exists(file))return {};return Read(file,scene);
}
void LayoutStorage::SaveNamed(const std::string& name,const SceneLayout& value,bool overwrite){Write(NamedPath(value.scene,name),value,overwrite);}
SceneLayout LayoutStorage::Named(const std::string& scene,const std::string& name) const{return Read(NamedPath(scene,name),scene);}
void LayoutStorage::DeleteNamed(const std::string& scene,const std::string& name) {
    if(!std::filesystem::remove(NamedPath(scene,name)))throw std::runtime_error("snapshot not found");
}
std::vector<std::string> LayoutStorage::Names(const std::string& scene) const {
    std::vector<std::string> names;const auto folder=Directory(scene)/"snapshots";
    if(!std::filesystem::exists(folder))return names;
    for(const auto& entry:std::filesystem::directory_iterator(folder))if(entry.is_regular_file() && entry.path().extension()==".json") {
        auto name=entry.path().stem().u8string();if(name.compare(0,7,"layout-")==0)names.push_back(name.substr(7));
    }
    std::sort(names.begin(),names.end());return names;
}
}
