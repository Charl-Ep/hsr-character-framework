#include "../support/ModelServicesFixture.hpp"
#include <hksr/framework/appearance/AppearanceInputs.hpp>
#include <hksr/metadata/MetadataResolver.hpp>
#include <Windows.h>
#include <cstddef>
#include <map>
#include <stdexcept>
#include <iostream>
namespace {
using namespace hksr;
int weapons,events,good_path,bad_path,info,model,loads=0,hidden=0,sequence=0;
struct Config { int unrelated[7]; void* hide=&weapons; void* event=&events; } config;
std::map<int,void*> handles;
void Check(bool ok) { if(!ok) throw std::runtime_error("appearance configuration mismatch"); }
template<class T> api::Method M(T f) { return {reinterpret_cast<void*>(f),&info}; }
int __fastcall Keep(void* p,int,int,void*) { handles[++sequence]=p; return sequence; }
void* __fastcall Target(int h,void*) { return handles.at(h); }
void __fastcall Free(int h,void*) { handles.erase(h); }
void* __fastcall Load(void* p,void* metadata) {
    Check(metadata==&info); ++loads;
    if(p==&bad_path) RaiseException(0xE0640001,0,0,nullptr);
    Check(p==&good_path); return &config;
}
void __fastcall Hide(void* p,void* array,void* metadata) { Check(p==&model && array==&weapons && metadata==&info); ++hidden; }
struct Objects:api::ManagedObjects {
    api::ObjectResult NewStringUtf8(const char* text) override { return {{true,0},std::string(text)=="good"?&good_path:&bad_path}; }
    api::ObjectResult AllocateObject(const char*,const char*) override { throw std::runtime_error("no native allocation for config"); }
    api::ObjectResult ManagedType(const char*,const char*) override { throw std::runtime_error("no array recreation"); }
};
}
void TestAppearanceInputs() {
    // Loader signature is retained. Field names follow the old live consumers;
    // offsets/objects below are synthetic, not a game declaration snapshot.
    api::MetadataResolver resolver({{"RPG.GameCore","GameCoreConfigLoader",{
        {"LoadAdventureCharacterConfig","RPG.GameCore.AdventureCharacterConfig",{"System.String"},true,M(Load)}}}});
    std::vector<api::DeclaredField> fields{
        {"RPG.GameCore","AdventureCharacterConfig","HideWeaponList","System.String[]",offsetof(Config,hide)},
        {"RPG.GameCore","CharacterConfig","AnimEventConfigList","System.String[]",offsetof(Config,event)}};
    auto methods=api::ResolveAdventureConfigMethods(resolver,fields); Check(methods.binding_errors.empty());
    api::AdventureConfig source(methods); api::ReferenceMethods refs{M(Keep),M(Target),M(Free),{}, {}};
    api::ArtAdmission admission({{},{},M(Hide)}); Objects objects;
    auto* component=reinterpret_cast<api::CharacterModelComponent*>(&model);
    framework::Appearance description; description.character_config=description.weapon_config="good";
    {
        framework::AppearanceInputs input(*TestModelServices(refs)); input.Prepare(objects,source,description);
        Check(loads==1 && input.Character().data.event_paths.object==&events && input.Weapons().ready);
        Check(input.ApplyWeaponDefaults(admission,component).apply.returned && hidden==1);
        Check(input.Close().complete && handles.empty() && !input.ApplyWeaponDefaults(admission,component).attempted);
    }
    {
        description.character_config="bad";
        framework::AppearanceInputs input(*TestModelServices(refs)); input.Prepare(objects,source,description);
        Check(!input.Character().ready && input.Weapons().ready && loads==3);
        Check(input.ApplyWeaponDefaults(admission,component).apply.returned && input.Close().complete && handles.empty());
    }
    {
        fields.push_back(fields.back()); auto ambiguous=api::ResolveAdventureConfigMethods(resolver,fields);
        Check(ambiguous.has_hidden_weapons && !ambiguous.has_event_paths && ambiguous.binding_errors.size()==1);
        config.hide=nullptr; description.character_config="good";
        framework::AppearanceInputs input(*TestModelServices(refs)); input.Prepare(objects,api::AdventureConfig(ambiguous),description);
        Check(input.Weapons().ready && input.ApplyWeaponDefaults(admission,component).no_values && !input.Character().data.event_paths.call.returned);
        Check(input.Close().complete && handles.empty()); config.hide=&weapons;
    }
    std::cout<<"Appearance config inputs passed: automatic source arrays, shared path acquisition, independent fields and no shared mutation.\n";
}
