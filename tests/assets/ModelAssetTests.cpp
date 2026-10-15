#include "../support/ModelServicesFixture.hpp"
#include <hksr/framework/assets/ModelAssets.hpp>
#include <hksr/framework/appearance/ModelEpoch.hpp>
#include <hksr/framework/appearance/NpcModels.hpp>
#include <hksr/framework/appearance/AppearanceModel.hpp>
#include <hksr/framework/character/CharacterInstance.hpp>
#include <hksr/framework/presentation/ModelActionHandoff.hpp>
#include <hksr/framework/character/CharacterSession.hpp>
#include <hksr/presentation/events/NpcEventBindings.hpp>
#include <algorithm>
#include <Windows.h>
#include <map>
#include <memory>
#include <iostream>
#include <stdexcept>

namespace {
using namespace hksr;
struct Node {
    std::string kind,text;
    bool owned=false,alive=false;
    int vertices=3,shapes=1;
    std::vector<void*> slots;
    void* mesh=nullptr; void* materials=nullptr; void* bones=nullptr;
    bool active=true;
    Node* parent=nullptr;
    void* event_files=nullptr;
    void* excluded_layers=nullptr;
};
Node renderer,source_mesh,source_material,source_materials,source_bones,art,bone,shader,material_type,transform_type;
Node component_list,game_object,model;
Node prefab,source_renderer,source_components,logical,hidden_weapons;
struct VisualModel { void* path=&logical; void* current=&prefab; void* root=&prefab; } visual_model;
struct Config { void* hide=&hidden_weapons; void* events=nullptr; } config;
Node event_paths,baseline_paths,event_records,event_type,data_type,event_context;
Config baseline_config;
struct Data { void* config=&baseline_config; } data_component;
struct EventComponent { void* consumer=&event_records; } event_component;
unsigned event_registers=0,event_detaches=0,event_disposals=0;
unsigned npc_motion_calls=0;
bool visual_mode=false,expect_payload=true,supply_fault=false;
unsigned source_loads=0,source_releases=0,weapon_defaults=0,visual_reads=0;
std::vector<std::unique_ptr<Node>> objects;
std::map<int,void*> handles;
int next=0,info,writes=0,destroys=0;
bool missing_bone=false,material_fault=false,restore_fault=false;
bool unload_on_assignment=false;
bool free_fault=false;
unsigned model_unloads=0;
int npc_storage,world_storage,manager_storage,definition_storage,row_storage;
bool model_loaded=false;
unsigned entity_reads=0;
auto* npc_entity=reinterpret_cast<api::GameEntity*>(&npc_storage);
auto* npc_world=reinterpret_cast<api::GameWorld*>(&world_storage);
void CheckAt(bool ok,int line) {
    if(!ok) { std::cerr<<"ModelAssetTests.cpp:"<<line<<" check failed\n"; throw std::runtime_error("model asset assembly mismatch"); }
}
#define Check(value) CheckAt((value),__LINE__)
template<class T> api::Method M(T f) { return {reinterpret_cast<void*>(f),&info}; }
Node* New(std::string kind) {
    auto p=std::make_unique<Node>(); p->kind=std::move(kind); auto* value=p.get(); objects.push_back(std::move(p)); return value;
}
struct Objects:api::ManagedObjects {
    api::ObjectResult NewStringUtf8(const char* text) override { auto* p=New("String"); p->text=text; return {{true,0},p}; }
    api::ObjectResult AllocateObject(const char* space,const char* name) override {
        if(std::string(space).empty() && std::string(name)=="HLCPCNFMECN") return {{true,0},New("Operation")};
        Check(std::string(space)=="UnityEngine"); auto* p=New(name); p->owned=true; return {{true,0},p};
    }
    api::ObjectResult ManagedType(const char*,const char* name) override {
        return {{true,0},std::string(name)=="Transform"?&transform_type:&material_type};
    }
};
int __fastcall Keep(void* p,int,int,void*) { handles[++next]=p; return next; }
void* __fastcall Target(int id,void*) { return handles.at(id); }
void __fastcall Free(int id,void*) {
    handles.erase(id);
    if(free_fault) { free_fault=false; RaiseException(0xE0600003,0,0,nullptr); }
}
bool __fastcall Alive(Node* p,void*) { return p->alive; }
void __fastcall Destroy(Node* p,void*) {
    Check(p->owned);
    if(p->kind!="GameObject")
        Check(!renderer.alive || (renderer.mesh==&source_mesh && renderer.materials==&source_materials && renderer.bones==&source_bones));
    ++destroys; p->alive=false;
    if(p==&game_object || game_object.parent==p) { game_object.alive=false; renderer.alive=false; }
}
void __fastcall CreateMesh(Node* p,void*) { Check(p->owned); p->alive=true; }
void __fastcall Format(Node*,api::MeshIndex,void*) {}
void __fastcall Channel(Node* p,api::MeshAttribute a,api::MeshElement,int,const void*,int count,int,int,void*) {
    if(a==api::MeshAttribute::Position) p->vertices=count;
}
void __fastcall Submeshes(Node*,int,void*) {}
void __fastcall Triangles(Node*,int,int,api::MeshIndex,const void*,int,int,bool,int,void*) {}
void __fastcall Bounds(Node*,void*) {}
void __fastcall Upload(Node*,bool,void*) {}
void* __fastcall Cached(Node* p,void*) { return p; }
bool __fastcall Weights(Node*,const void*,int) { return true; }
void __fastcall Matrices(Node*,const void*,int) {}
void* __fastcall Clone(Node* p,void*) { auto* n=New("Mesh"); *n=*p; n->owned=true; n->alive=true; return n; }
int __fastcall Vertices(Node* p,void*) { return p->vertices; }
int __fastcall Shapes(Node* p,void*) { return p->shapes; }
void __fastcall TextureCtor(Node* p,int,int,int,int,bool,void*,void*) { Check(p->owned); p->alive=true; }
bool __fastcall LoadTexture(Node*,const void*,int,void*) { return true; }
void __fastcall ApplyTexture(Node*,bool,bool,void*) {}
void __fastcall Setting(Node*,int,void*) {}
void __fastcall Bias(Node*,float,void*) {}
int __fastcall Dimension(Node*,void*) { return 2; }
void __fastcall MaterialCtor(Node* p,void* original,void*) {
    Check(p->owned && original==&source_material); p->alive=true;
    if(material_fault) RaiseException(0xE0600001,0,0,nullptr);
}
int __fastcall Id(Node* text,void*) { Check(text->text=="_MainTex"); return 7; }
bool __fastcall Has(Node*,int,void*) { return true; }
void __fastcall SetTexture(Node* p,int id,Node* texture,void*) { Check(p->owned && id==7 && texture->owned && texture->kind=="Texture2D"); }
void __fastcall Transform(Node*,int,api::Float2,void*) {}
void* __fastcall Shader(Node*,void*) { return &shader; }
int __fastcall One(Node*,void*) { return 1; }
int __fastcall PropertyId(Node*,int,void*) { return 7; }
int __fastcall PropertyType(Node*,int,void*) { return 4; }
int __fastcall PropertyDimension(Node*,int,void*) { return 2; }
void* __fastcall ArrayCreate(void*,int count,void*) { auto* p=New("Array"); p->slots.resize(count); return p; }
int __fastcall Length(Node* p,void*) { return static_cast<int>(p->slots.size()); }
void* __fastcall Get(Node* p,int i,void*) { return p->slots.at(i); }
void __fastcall Set(Node* p,void* value,int i,void*) { p->slots.at(i)=value; }
void* __fastcall Find(void* root,Node* path,void*) {
    Check(root==&art && path->text=="Main/Root_M"); return missing_bone?nullptr:&bone;
}
void* __fastcall GetMesh(Node* p,void*) { return p->mesh; }
void* __fastcall GetMaterials(Node* p,void*) { return p->materials; }
void* __fastcall GetBones(Node* p,void*) { return p->bones; }
__declspec(noinline) void __fastcall Unload(api::CharacterModelComponent*,bool,void*) {
    ++model_unloads;
    if(visual_mode) {
        if(visual_model.current==&game_object && !restore_fault)
            Check(renderer.mesh==&source_mesh && renderer.materials==&source_materials && renderer.bones==&source_bones);
        visual_model.current=nullptr;
        return; // deferred native destruction, completed explicitly by the test
    }
    Check(renderer.mesh==&source_mesh && renderer.materials==&source_materials && renderer.bones==&source_bones);
}
__declspec(noinline) void __fastcall ReleaseRoot(api::CharacterModelComponent*,void*) {
    ++model_unloads;
    Check(renderer.mesh==&source_mesh && renderer.materials==&source_materials && renderer.bones==&source_bones);
}
void __fastcall SetMesh(Node* p,void* v,void*) {
    ++writes; p->mesh=v;
    if(unload_on_assignment && v!=&source_mesh) {
        unload_on_assignment=false;
        Unload(reinterpret_cast<api::CharacterModelComponent*>(&model),false,&info);
    }
}
void __fastcall SetMaterials(Node* p,void* v,void*) {
    ++writes;
    if(restore_fault && v==&source_materials) RaiseException(0xE0600002,0,0,nullptr);
    p->materials=v;
}
void __fastcall SetBones(Node* p,void* v,void*) { ++writes; p->bones=v; }
void* __fastcall SceneTransform(void* object,void*) { return object==&game_object?&art:object; }
void* __fastcall Components(void* object,void*,bool inactive,void*) {
    Check((object==&game_object || object==&prefab) && inactive);
    return object==&prefab?&source_components:&component_list;
}
void* __fastcall Name(void* object,void*) { Check(object==&renderer || object==&source_renderer); auto* name=New("String"); name->text="Body"; return name; }
bool __fastcall Equal(Node* a,Node* b,void*) { return a->text==b->text; }
struct ReleaseResolver:api::MethodResolver {
    api::Method Resolve(const char*,const char*,const char* name,const char*,std::initializer_list<const char*>,api::MethodKind) override {
        if(std::string(name)=="OnArtNodeUnload") return M(Unload);
        if(std::string(name)=="_ReleaseRootModel") return M(ReleaseRoot);
        throw std::runtime_error("unexpected release hook");
    }
};
api::GameEntity* __fastcall CreateNpc(api::EntityManager*,api::MapNpcDefinition*,api::NpcDataRow*,api::EntityCallback*,bool,void*) { return npc_entity; }
std::uint32_t __fastcall RuntimeId(api::GameEntity*,void*) { ++entity_reads; return 123; }
api::GameWorld* __fastcall OwnerWorld(api::GameEntity*,void*) { ++entity_reads; return npc_world; }
bool __fastcall EntityLoaded(api::GameEntity*,void*) { ++entity_reads; return true; }
void* __fastcall ModelComponent(api::GameEntity* p,void* type,void*) {
    Check(p==npc_entity);
    if(type==&event_type) return &event_component;
    if(type==&data_type) return &data_component;
    return visual_mode?static_cast<void*>(&visual_model):&model;
}
void __fastcall Kill(api::GameEntity*,bool,void*) {}
void __fastcall Die(api::GameWorld* world,api::GameEntity* entity,api::GameEntity*,void*) {
    Check(world==npc_world && entity==npc_entity);
    if(visual_mode) Check(event_records.slots.empty());
    Unload(reinterpret_cast<api::CharacterModelComponent*>(&model),false,&info);
    api::TrackedEntities().Disposed(api::TrackedEntities().Current(entity));
}
void* __fastcall Art(api::CharacterModelComponent*,void*) { return &game_object; }
bool __fastcall Changing(api::CharacterModelComponent*,void*) { return false; }
bool __fastcall LoadedArt(api::CharacterModelComponent*,void*) { return model_loaded; }
void* __fastcall LoadPrefab(Node* path,void*,bool option,void*) {
    Check(path->text=="independent.prefab" && !option); ++source_loads; return &prefab;
}
void __fastcall ReleasePrefab(void* object,void*) { Check(object==&prefab && !game_object.alive); ++source_releases; }
void __fastcall InitGameObject(Node* object,void*) { object->alive=true; }
void __fastcall Active(Node* object,bool active,void*) { object->active=active; }
void* __fastcall CloneVisual(Node* object,Node* parent,bool world,void*) {
    Check(object==&prefab && !parent->active && !world);
    game_object.kind="GameObject"; game_object.owned=game_object.alive=renderer.alive=true;
    game_object.parent=parent; return &game_object;
}
int __fastcall Children(Node* object,void*) { return game_object.alive && game_object.parent==object?1:0; }
void* __fastcall PrivateArt(void* object,void*) { Check(object==&visual_model); ++visual_reads; return visual_model.current; }
void __fastcall Supply(void* object,Node* key,void* path,bool option,void* callback,void* provided,void*) {
    Check(object==&visual_model && key->text=="ArtRoot" && path==&logical && !option && !callback && provided==&game_object);
    Check(!game_object.parent->active && (expect_payload ? renderer.mesh!=&source_mesh : renderer.mesh==&source_mesh));
    Unload(reinterpret_cast<api::CharacterModelComponent*>(object),true,&info);
    // Old-Art unload must not restore the new private renderer.
    Check(expect_payload ? renderer.mesh!=&source_mesh : renderer.mesh==&source_mesh);
    visual_model.current=&game_object; game_object.parent=nullptr;
    if(supply_fault) RaiseException(0xE0600004,0,0,nullptr);
}
void* __fastcall LoadConfig(Node*,void*) { return &config; }
void __fastcall HideWeapons(void* object,void* list,void*) {
    Check(object==&visual_model && visual_model.current==&game_object && list==&hidden_weapons); ++weapon_defaults;
}
void* __fastcall EventContext(void* object,void*) { Check(object==&event_component); return &event_context; }
void* __fastcall EventCaster(void* object,void*) { Check(object==&event_context); return npc_entity; }
void* __fastcall EventRecords(void* object,void*) { Check(object==&event_component); return &event_records; }
void* __fastcall RegisterEvent(void* object,void* context,Node* path,void*) {
    Check(object==&event_component && context==&event_context && path->text=="private.event");
    ++event_registers; auto* record=New("Record"); event_records.slots.push_back(record); return record;
}
void __fastcall DetachEvent(void* object,void* record,bool option,void*) {
    Check(object==&event_component && option); ++event_detaches;
    event_records.slots.erase(std::remove(event_records.slots.begin(),event_records.slots.end(),record),event_records.slots.end());
}
void __fastcall DisposeEvent(void*,void*) { ++event_disposals; }
void __fastcall PlayNpcMotion(void* component,unsigned id,void*) {
    Check(component==&visual_model && id==17); ++npc_motion_calls;
}
Node original_path,original_config,profile_layers;
void* profile_path=&original_path;
void* profile_config=&original_config;
unsigned profile_begins=0,profile_restores=0,profile_events=0,battle_motion_calls=0;
void* __fastcall ProfilePath(void* m,void*) { Check(m==&visual_model); return profile_path; }
void* __fastcall ProfileConfig(void* m,void*) { Check(m==&visual_model); return profile_config; }
void __fastcall OperationCtor(void*,void*) {}
bool __fastcall Ready(void*,void*) { return true; }
int __fastcall ModelState(void*,void*) { return model_loaded?5:4; }
void* __fastcall ModelAnimator(void*,void*) { return visual_model.current; }
bool __fastcall CanChangeProfile(void* m,void* path,void** resolved,void*) { Check(m==&visual_model); *resolved=path; return true; }
void __fastcall BeginProfile(void*,void* e,api::ModelInputs input,bool option,void* callback,void*) {
    Check(e==npc_entity && !option && !callback && event_records.slots.empty());
    Check(renderer.mesh==&source_mesh && renderer.materials==&source_materials && renderer.bones==&source_bones);
    ++profile_begins; profile_path=input.model_path; profile_config=nullptr; model_loaded=false;
    Unload(reinterpret_cast<api::CharacterModelComponent*>(&visual_model),false,&info);
}
void __fastcall CancelProfile(void*,void*) {}
void __fastcall RestoreProfile(void* m,void* path,void* cfg,void* supplied,void*) {
    Check(m==&visual_model && !supplied && event_records.slots.empty() && renderer.mesh==&source_mesh);
    ++profile_restores; profile_path=path; profile_config=cfg; model_loaded=false;
    Unload(reinterpret_cast<api::CharacterModelComponent*>(&visual_model),false,&info);
}
void __fastcall SetProfileEvents(void* c,void* paths,void* layers,void*) {
    Check(c==&event_component && event_records.slots.empty()); // supplements must be installed AFTER this call
    ++profile_events; event_records.event_files=paths; event_records.excluded_layers=layers;
}
int __fastcall StateHash(Node* text,void*) { Check(text->text=="AnyBattleState"); return 50; }
bool __fastcall HasState(void* a,int layer,int hash,void*) { Check(a==&game_object && layer==0 && hash==50); return true; }
void __fastcall PlayBattle(void* component,int hash,float first,float second,void*) {
    Check(component==&visual_model && hash==50 && first==0 && second==0 && renderer.mesh!=&source_mesh && event_records.slots.empty());
    ++battle_motion_calls;
}
framework::ModelAssetInput Input() {
    framework::ModelAssetInput in; in.manifest.materials=framework::MaterialSource::Package;
    framework::MeshDescription d; d.renderer="Body"; d.vertex_count=3; d.index_count=3; d.submeshes={{0,3}}; d.bone_paths={"Main/Root_M"};
    in.manifest.meshes.push_back(d);
    framework::MeshData data; data.positions.resize(3); data.normals.resize(3); data.tangents.resize(3); data.colors.resize(3);
    data.uv0.resize(3); data.bone_weights.resize(3); data.indices={0,1,2}; data.bindposes.resize(1);
    for(auto& v:data.bone_weights) v.weights={1,0,0,0}; in.meshes.push_back(data);
    framework::TextureDescription texture; texture.width=texture.height=texture.mip_count=1; texture.format=4; texture.payload_bytes=4;
    in.materials.textures.push_back(texture); in.texture_payloads={{255,255,255,255}};
    framework::MaterialDescription material; framework::MaterialProperty property;
    property.name="_MainTex"; property.kind=4; property.texture_index=0; property.scale_offset={1,1,0,0};
    material.properties.push_back(property); in.materials.materials.push_back(material); return in;
}
}
void TestModelAssets() {
    source_materials.slots={&source_material}; source_bones.slots={&bone};
    renderer.mesh=&source_mesh; renderer.materials=&source_materials; renderer.bones=&source_bones;
    renderer.alive=true;
    Objects objects_source;
    api::MeshAccess mesh({M(CreateMesh),M(Format),M(Channel),M(Triangles),M(Submeshes),M(Bounds),M(Upload)});
    api::MeshSkinning skin({M(Cached),reinterpret_cast<void*>(Weights),reinterpret_cast<void*>(Matrices)});
    api::MeshTemplateAccess templates({M(Clone),M(Vertices),M(Shapes)});
    api::TextureAccess textures({M(TextureCtor),M(LoadTexture),M(ApplyTexture),M(Setting),M(Setting),M(Bias),M(Setting),M(Setting),M(Setting),M(Dimension)});
    api::MaterialAccess materials({M(MaterialCtor),M(Id),M(Has),M(SetTexture),M(Transform),M(Transform)});
    api::ShaderAccess shaders({M(Shader),M(One),M(PropertyId),M(PropertyType),M(PropertyDimension)});
    api::ManagedArrays arrays({M(ArrayCreate),M(Length),M(Get),M(Set)}); api::HierarchyAccess hierarchy({M(Find)});
    framework::ModelAssetServices services{objects_source,mesh,skin,templates,textures,materials,shaders,arrays,hierarchy};
    api::RendererMethods renderers{M(GetMesh),M(SetMesh),M(GetMaterials),M(SetMaterials),M(GetBones),M(SetBones)};
    api::ReferenceMethods refs{M(Keep),M(Target),M(Free),{}, {}}; api::UnityLifetimeMethods lifetime{M(Alive),M(Destroy)};
    std::vector<framework::ModelRendererTarget> targets{{"Body",&renderer,&source_mesh,&source_materials}};
    auto input=Input();
    {
        framework::ModelAssets closed(TestModelServices(refs,lifetime,renderers)); Check(closed.Close().complete);
        bool rejected=false;
        try { closed.Prepare(services,input,&art,targets); } catch(const std::logic_error&) { rejected=true; }
        Check(rejected && handles.empty());
    }
    {
        framework::ModelAssets assets(TestModelServices(refs,lifetime,renderers));
        const auto prepared=assets.Prepare(services,input,&art,targets);
        Check(prepared.result.complete && prepared.meshes.size()==1 && prepared.materials.size()==1 && writes==0 && !handles.empty());
        Check(assets.Apply().complete && renderer.mesh!=&source_mesh && renderer.bones!=&source_bones);
        Check(assets.BeforeModelRelease().complete && renderer.bones==&source_bones);
        Check(!assets.Close().complete && assets.Close().complete && handles.empty() && destroys==3);
    }
    {
        framework::ModelAssets assets(TestModelServices(refs,lifetime,renderers)); missing_bone=true; const auto before=writes;
        const auto failed=assets.Prepare(services,input,&art,targets); missing_bone=false;
        Check(!failed.result.complete && writes==before && failed.materials.size()==1);
        Check(!assets.Close().complete && assets.Close().complete && handles.empty());
    }
    {
        framework::ModelAssets assets(TestModelServices(refs,lifetime,renderers)); material_fault=true;
        const auto failed=assets.Prepare(services,input,&art,targets); material_fault=false;
        Check(!failed.result.complete && failed.materials[0].initialize.exception==0xE0600001);
        Check(!assets.Close().complete && assets.Close().complete && handles.empty());
    }
    {
        framework::ModelAssets assets(TestModelServices(refs,lifetime,renderers));
        auto template_input=input; template_input.manifest.materials=framework::MaterialSource::Template;
        template_input.materials={}; template_input.texture_payloads={}; template_input.shapes=framework::ShapeData::MatchingTemplate;
        Check(assets.Prepare(services,template_input,&art,targets).result.complete && assets.Apply().complete);
        Check(!assets.Close().complete && assets.Close().complete && handles.empty());
    }
    {
        framework::ModelAssets assets(TestModelServices(refs,lifetime,renderers));
        Check(assets.Prepare(services,input,&art,targets).result.complete && assets.Apply().complete);
        restore_fault=true; const auto before=destroys;
        Check(!assets.Close().complete && destroys==before && !handles.empty());
        restore_fault=false;
        Check(!assets.Close().complete && assets.Close().complete && handles.empty());
    }
    component_list.slots={&renderer};
    api::SceneObjects scene({M(SceneTransform),M(Components),M(Name),M(Equal)});
    auto* model_component=reinterpret_cast<api::CharacterModelComponent*>(&model);
    ReleaseResolver resolver; api::InstallModelReleaseHooks(resolver);
    {
        framework::ModelEpoch epoch(TestModelServices(refs,lifetime,renderers));
        epoch.PrepareAndApply(model_component,&game_object,&game_object,services,scene,input);
        Check(epoch.Status().published && renderer.mesh!=&source_mesh);
        Unload(model_component,false,&info);
        const auto before=writes;
        ReleaseRoot(model_component,&info);
        Check(epoch.Status().release_seen && epoch.Status().restoration.complete && writes==before);
        Check(!epoch.Close().complete && epoch.Status().detached);
        // Same model component can load another Art while the previous private
        // resources finish destruction; old token cannot unregister the new one.
        framework::ModelEpoch next_epoch(TestModelServices(refs,lifetime,renderers));
        next_epoch.PrepareAndApply(model_component,&game_object,&game_object,services,scene,input);
        Check(next_epoch.Status().published);
        Check(epoch.Close().complete);
        Unload(model_component,false,&info);
        Check(next_epoch.Status().release_seen && !next_epoch.Close().complete && next_epoch.Close().complete && handles.empty());
    }
    {
        framework::ModelEpoch epoch(TestModelServices(refs,lifetime,renderers)); material_fault=true;
        epoch.PrepareAndApply(model_component,&game_object,&game_object,services,scene,input); material_fault=false;
        Check(!epoch.Status().published && !epoch.Status().preparation.result.complete);
        Check(!epoch.Close().complete && epoch.Close().complete && handles.empty());
    }
    {
        framework::ModelEpoch epoch(TestModelServices(refs,lifetime,renderers)); unload_on_assignment=true;
        epoch.PrepareAndApply(model_component,&game_object,&game_object,services,scene,input);
        Check(epoch.Status().release_seen && !epoch.Status().published && !epoch.Status().publication.complete);
        Check(renderer.mesh==&source_mesh && renderer.materials==&source_materials && renderer.bones==&source_bones);
        Check(!epoch.Close().complete && epoch.Close().complete && handles.empty());
    }
    Check(model_unloads==4);
    {
        framework::ModelEpoch epoch(TestModelServices(refs,lifetime,renderers)); renderer.alive=true;
        epoch.PrepareAndApply(model_component,&game_object,&game_object,services,scene,input);
        Check(epoch.Status().published); epoch.OwnerGone();
        const auto before=writes, destroyed_before=destroys;
        Check(!epoch.Close().complete && writes==before && destroys==destroyed_before);
        renderer.alive=false;
        Check(!epoch.Close().complete && epoch.Status().detached && writes==before);
        Check(epoch.Close().complete && handles.empty() && writes==before);
        // Simulate a different official incarnation only after old references
        // are gone; the framework never wrote these reset values.
        renderer.mesh=&source_mesh; renderer.materials=&source_materials;
        renderer.bones=&source_bones; renderer.alive=true;
    }
    api::EntityAccessMethods entity_methods{M(RuntimeId),M(OwnerWorld),M(EntityLoaded),M(ModelComponent),{},M(Kill),M(Die)};
    api::ModelAccessMethods model_methods{M(Art),{},{},M(Changing),{},M(LoadedArt)};
    api::NpcCreateInput create{npc_world,reinterpret_cast<api::EntityManager*>(&manager_storage),
        reinterpret_cast<api::MapNpcDefinition*>(&definition_storage),reinterpret_cast<api::NpcDataRow*>(&row_storage)};
    api::WorldFrame frame; frame.world=npc_world; frame.disposing={{false},{true,0}};
    auto payload=std::make_shared<const framework::ModelAssetInput>(input);
    {
        api::EntityCreationSession npc(api::EntityOwner{},M(CreateNpc),entity_methods,refs); npc.Create(create);
        framework::NpcModels models(npc,npc_world,payload,TestModelServices(refs,lifetime,renderers,entity_methods,api::ModelAccess(model_methods)));
        models.Service(frame,services,scene); Check(models.Status().incarnations==0);
        model_loaded=true; models.Service(frame,services,scene);
        Check(models.Active() && models.Active()->published && models.Status().incarnations==1);
        models.Service(frame,services,scene); Check(models.Status().incarnations==1); // no rebuild per frame
        models.SetEnabled(false); models.Service(frame,services,scene);
        Check(models.InputsDetached() && !models.Active() && renderer.mesh==&source_mesh);
        models.SetEnabled(true); models.Service(frame,services,scene);
        Check(models.Active()->published && models.Status().incarnations==2);
        auto other_world=frame; other_world.world=nullptr; const auto reads=entity_reads;
        models.Service(other_world,services,scene); Check(entity_reads==reads);
        Unload(model_component,false,&info); model_loaded=false;
        models.Service(frame,services,scene); Check(!models.Active() && models.Status().retiring==1);
        model_loaded=true; models.Service(frame,services,scene);
        Check(models.Active() && models.Active()->published && models.Status().incarnations==3);
        npc.Remove(); models.Service(frame,services,scene); npc.Service(frame);
        Check(npc.Status().closed);
        models.Service(other_world,services,scene); Check(models.Status().closed && handles.empty());
    }
    {
        api::EntityCreationSession npc(api::EntityOwner{},M(CreateNpc),entity_methods,refs); npc.Create(create);
        framework::NpcModels models(npc,npc_world,payload,TestModelServices(refs,lifetime,renderers,entity_methods,api::ModelAccess(model_methods)));
        models.Service(frame,services,scene); Check(models.Active()->published);
        Unload(model_component,false,&info);
        api::TrackedEntities().Disposed(api::TrackedEntities().Current(npc_entity));
        auto next_world=frame; next_world.world=nullptr;
        npc.Service(next_world); // NPC forgets its dead generation before model service
        const auto reads=entity_reads;
        models.Service(next_world,services,scene); models.Service(next_world,services,scene);
        Check(models.Status().closed && npc.Status().closed && entity_reads==reads && handles.empty());
    }
    {
        framework::ModelEpoch old(TestModelServices(refs,lifetime,renderers));
        old.PrepareAndApply(model_component,&game_object,&game_object,services,scene,input);
        const auto before=destroys; free_fault=true;
        Check(!old.Close().complete && old.Status().detached && destroys==before+3);
        // Unknown GC Free outcome stays unresolved, but cannot block native
        // disposal or registration of a new model incarnation after restoration.
        framework::ModelEpoch next_epoch(TestModelServices(refs,lifetime,renderers));
        next_epoch.PrepareAndApply(model_component,&game_object,&game_object,services,scene,input);
        Check(next_epoch.Status().published && !old.Close().complete);
        Check(!next_epoch.Close().complete && next_epoch.Close().complete && handles.empty());
        // The substitute removed the handle before throwing. Production cannot
        // know that; old remains explicitly unresolved, not "success by test".
    }
    // Complete private-appearance assembly using the SAME asset operations and
    // real local unload hooks, not a mocked orchestration service.
    visual_mode=true;
    source_renderer.mesh=&source_mesh; source_renderer.materials=&source_materials; source_renderer.bones=&source_bones;
    source_components.slots={&source_renderer};
    auto visual_refs=refs; visual_refs.load_asset=M(LoadPrefab); visual_refs.release_asset=M(ReleasePrefab);
    api::VisualObjects visuals({M(InitGameObject),M(Active),M(CloneVisual),{},M(Children)});
    api::AdventureConfig configs({M(LoadConfig),offsetof(Config,hide),offsetof(Config,events),true,true,{}});
    api::ArtAdmission admission({M(Supply),M(Unload),M(HideWeapons),offsetof(VisualModel,path),true});
    api::ModelAccess visual_access({M(PrivateArt)});
    auto* visual_component=reinterpret_cast<api::CharacterModelComponent*>(&visual_model);
    framework::Appearance appearance; appearance.template_prefab="independent.prefab";
    appearance.character_config=appearance.weapon_config="character.config";
    {
        framework::AppearanceModel owned(TestModelServices(visual_refs,lifetime,renderers,{},visual_access,admission));
        owned.Prepare(services,scene,visuals,configs,appearance,framework::VisualRoot::Prefab,&input);
        Check(owned.Status().prepared && renderer.mesh!=&source_mesh && source_renderer.mesh==&source_mesh);
        // This substitute's typed component query returns a pre-existing
        // support object; no serialization/Add should be required for it.
        api::ComponentConfig support_calls({});
        owned.PrepareLookSupport({objects_source,scene,arrays,visuals,support_calls},"unused.prefab");
        Check(owned.Look().existing && owned.Look().prepared);
        owned.Submit(objects_source,visual_component);
        Check(owned.Slot().adopted && owned.Slot().previous_art_releases==1 && weapon_defaults==1);
        Check(!owned.Close(visuals).complete && owned.Status().restoration.complete && source_releases==0);
        game_object.alive=renderer.alive=false;
        Check(owned.Close(visuals).complete && source_loads==source_releases && handles.empty());
    }
    {
        visual_model.current=&prefab; material_fault=true;
        framework::AppearanceModel failed(TestModelServices(visual_refs,lifetime,renderers,{},visual_access,admission));
        failed.Prepare(services,scene,visuals,configs,appearance,framework::VisualRoot::Prefab,&input); material_fault=false;
        Check(!failed.Status().prepared && !failed.Slot().supply_attempted);
        Check(!failed.Close(visuals).complete && failed.Close(visuals).complete && handles.empty());
    }
    {
        visual_model.current=&prefab; supply_fault=true;
        framework::AppearanceModel faulted(TestModelServices(visual_refs,lifetime,renderers,{},visual_access,admission));
        faulted.Prepare(services,scene,visuals,configs,appearance,framework::VisualRoot::Prefab,&input);
        faulted.Submit(objects_source,visual_component); supply_fault=false;
        Check(faulted.Slot().adopted && !faulted.Slot().supply.returned && weapon_defaults==1);
        Check(!faulted.Close(visuals).complete && faulted.Status().restoration.complete);
        game_object.alive=renderer.alive=false;
        Check(faulted.Close(visuals).complete && handles.empty());
    }
    {
        visual_model.current=&prefab;
        framework::AppearanceModel cancelled(TestModelServices(visual_refs,lifetime,renderers,{},visual_access,admission));
        cancelled.Prepare(services,scene,visuals,configs,appearance,framework::VisualRoot::Prefab,&input);
        cancelled.OwnerGone(); // owner ended between preparation and submission
        bool rejected=false;
        try { cancelled.Submit(objects_source,visual_component); } catch(const std::logic_error&) { rejected=true; }
        Check(rejected && !cancelled.Slot().supply_attempted);
        Check(!cancelled.Close(visuals).complete && cancelled.Close(visuals).complete && handles.empty());
    }
    {
        visual_model.current=&prefab;
        framework::AppearanceModel natural(TestModelServices(visual_refs,lifetime,renderers,{},visual_access,admission));
        natural.Prepare(services,scene,visuals,configs,appearance,framework::VisualRoot::Prefab,&input);
        natural.Submit(objects_source,visual_component);
        Unload(visual_component,true,&info); natural.OwnerGone();
        const auto before=visual_reads;
        Check(!natural.Close(visuals).complete && natural.Close(visuals).complete && handles.empty());
        Check(visual_reads==before && natural.Status().restoration.complete);
    }
    {
        visual_model.current=&prefab;
        framework::AppearanceModel uncertain(TestModelServices(visual_refs,lifetime,renderers,{},visual_access,admission));
        uncertain.Prepare(services,scene,visuals,configs,appearance,framework::VisualRoot::Prefab,&input);
        uncertain.Submit(objects_source,visual_component);
        restore_fault=true; Unload(visual_component,true,&info); restore_fault=false;
        Check(!uncertain.Status().restoration.complete);
        const auto before=writes; const auto leases_before=source_releases;
        // Parent Art gone does not prove a reparented renderer died.
        game_object.alive=false;
        Check(!uncertain.Close(visuals).complete && !uncertain.Status().payload_detached && source_releases==leases_before);
        Check(writes==before && renderer.alive);
        renderer.alive=false;
        Check(!uncertain.Close(visuals).complete && uncertain.Status().payload_detached && writes==before);
        Check(uncertain.Close(visuals).complete && handles.empty());
        renderer.mesh=&source_mesh; renderer.materials=&source_materials; renderer.bones=&source_bones;
    }
    {
        visual_model.current=&prefab; expect_payload=false;
        framework::AppearanceModel official(TestModelServices(visual_refs,lifetime,renderers,{},visual_access,admission));
        official.Prepare(services,scene,visuals,configs,appearance,framework::VisualRoot::Prefab,nullptr);
        official.Submit(objects_source,visual_component);
        Check(official.Slot().adopted && !official.Status().has_payload);
        Check(!official.Close(visuals).complete);
        game_object.alive=renderer.alive=false;
        Check(official.Close(visuals).complete && handles.empty() && source_loads==source_releases);
    }
    // Source selection, private appearance and extra events share one actual
    // api::EntityCreationSession. These checks exercise production coordinators, not an
    // independently mocked "actor ready" flag.
    {
        visual_model.current=&prefab; expect_payload=true; model_loaded=false;
        baseline_config.events=&baseline_paths;
        auto* shared=New("String"); shared->text="common.event";
        auto* extra=New("String"); extra->text="private.event";
        baseline_paths.slots={shared}; event_paths.slots={shared,extra}; config.events=&event_paths;
        const api::AnimationEventMethods event_methods{M(EventContext),M(EventCaster),M(EventRecords),
            M(RegisterEvent),M(DetachEvent),M(DisposeEvent),{},offsetof(EventComponent,consumer),true};
        const api::NpcEventBindings event_bindings{api::EntityAccess(entity_methods),
            {offsetof(Data,config),true,{}},configs,api::AnimationEvents(event_methods),arrays,
            api::ManagedReferenceList({M(Length),M(Get),{}}),scene};
        auto event_services=[=](api::EntityCreationSession& npc,api::GameWorld* owner_world) {
            return std::make_unique<api::NpcEventSupplement>(npc,owner_world,visual_refs,event_bindings);
        };
        const api::AnimationInputTypes types{{{true,0},&data_type},{{true,0},&event_type}};
        api::ComponentConfig component_config({});
        framework::AppearanceServices appearance_services{services,scene,visuals,configs,component_config};
        api::ModelAccessMethods owned_model{M(PrivateArt),{},{},M(Changing),{},M(LoadedArt)};
        std::vector<api::EntityOrigin> character_births;
        auto create_npc=[&,owner=api::EntityOwner{}]{return std::make_unique<api::EntityCreationSession>(owner,M(CreateNpc),entity_methods,visual_refs,
            [&](api::EntityOrigin origin){character_births.push_back(origin);});};
        framework::CharacterMethods methods{create_npc,TestModelServices(visual_refs,lifetime,renderers,entity_methods,api::ModelAccess(owned_model),admission)};
        framework::CharacterPackage package; package.source=framework::SourceKind::External; package.appearance=appearance;
        framework::CharacterInstance character(npc_world,package,payload,"unused.prefab",methods,event_services,framework::VisualRoot::Prefab);
        character.Create(create);
        Check(character_births.size()==1 && character_births[0].world==npc_world &&
            character_births[0].token.entity==npc_entity && character_births[0].token.generation!=0);
        const auto loads_before=source_loads;
        auto foreign=frame; foreign.world=nullptr;
        character.Service(foreign,appearance_services,types); Check(source_loads==loads_before);
        character.Service(frame,appearance_services,types); Check(!character.AppearanceConsumer()->Active());
        model_loaded=true; character.Service(frame,appearance_services,types);
        Check(character.AppearanceConsumer()->Active()->Slot().adopted && event_registers==1);
        Check(character.Events()->Active()->Files().size()==1 && character.Creation().entities.size()==1);
        character.Service(frame,appearance_services,types);
        Check(source_loads==loads_before+1 && event_registers==1); // no per-frame construction
        character.Remove(); character.Service(frame,appearance_services,types);
        Check(character.Creation().closed && !character.Status().closed && event_detaches==1 && event_disposals==1);
        game_object.alive=renderer.alive=false;
        const auto reads=entity_reads;
        character.Service(foreign,appearance_services,types);
        character.Service(foreign,appearance_services,types);
        Check(character.Status().closed && entity_reads==reads && handles.empty() && source_loads==source_releases);

        // A pure official package uses no visual copy or event supplement.
        package.source=framework::SourceKind::Official; package.appearance.reset();
        unsigned outfit_attempts=0;package.avatar_id=8005;
        methods.outfit=[&]()->std::unique_ptr<api::CharacterOutfit>{++outfit_attempts;throw std::runtime_error("fixture outfit source unavailable");};
        framework::CharacterInstance official(npc_world,package,nullptr,"",methods,event_services);
        official.Create(create); const auto prior=source_loads;
        Check(character_births.size()==2 && character_births[1].token.generation!=character_births[0].token.generation);
        api::AnimationControlMethods action_methods; action_methods.npc_motion=M(PlayNpcMotion);
        auto animations=[=](api::EntityToken owner){return std::make_unique<api::ActorAnimationSession>(owner,visual_refs,
            api::EntityAccess(entity_methods),api::ModelAccess(owned_model),api::AnimationControl(action_methods),api::ProfileInputs({}));};
        official.EnableActionPlayback(animations);
        official.QueueExplorationAction(framework::NpcMotion{17});
        official.Service(frame,appearance_services,types);
        Check(!official.AppearanceConsumer() && !official.Events() && source_loads==prior && npc_motion_calls==1);
        Check(outfit_attempts==1 && official.Status().errors.size()==1); // independent diagnostic, not actor/action gate
        official.QueueExplorationAction(framework::NpcMotion{17});
        official.SetExplorationInputsEnabled(false); official.Service(frame,appearance_services,types);
        Check(npc_motion_calls==1 && official.Actions()->Status().pending);
        official.SetExplorationInputsEnabled(true); official.Service(frame,appearance_services,types);
        Check(npc_motion_calls==2 && !official.Actions()->Status().pending);
        Check(outfit_attempts==1);package.avatar_id=0;methods.outfit={};
        official.QueueExplorationAction(framework::NpcMotion{17});
        official.Remove(); official.Service(frame,appearance_services,types);
        Check(official.Status().closed && handles.empty() && npc_motion_calls==2 && official.Actions()->Status().closed);

        // Official Player appearance uses the private source but no custom mesh.
        package.appearance=appearance; expect_payload=false; visual_model.current=&prefab;
        framework::CharacterInstance player_appearance(npc_world,package,nullptr,"",methods,event_services,framework::VisualRoot::Prefab);
        player_appearance.Create(create); player_appearance.Service(frame,appearance_services,types);
        Check(player_appearance.AppearanceConsumer()->Active()->Slot().adopted &&
            !player_appearance.AppearanceConsumer()->Active()->Status().has_payload);
        api::TrackedEntities().Disposed(api::TrackedEntities().Current(npc_entity));
        const auto before_entity=entity_reads,before_model=visual_reads;
        player_appearance.Service(foreign,appearance_services,types);
        player_appearance.Service(foreign,appearance_services,types);
        player_appearance.Service(foreign,appearance_services,types);
        Check(player_appearance.Status().closed && entity_reads==before_entity && visual_reads==before_model && handles.empty());
        // The official entity's disposal substitutes all component destruction.
        event_records.slots.clear(); config.events=nullptr;

        // A payload failure must neither register source events on the original
        // NPC Art nor delete that NPC, nor retry construction every frame.
        package.source=framework::SourceKind::External; expect_payload=true;
        visual_model.current=&prefab; material_fault=true;
        framework::CharacterInstance failed(npc_world,package,payload,"",methods,event_services,framework::VisualRoot::Prefab);
        failed.Create(create); failed.Service(frame,appearance_services,types); material_fault=false;
        const auto failed_loads=source_loads,failed_registrations=event_registers;
        failed.Service(frame,appearance_services,types);
        Check(failed.Creation().create_returned && !failed.Creation().removal_requested && !failed.Status().event_preparation_attempted);
        Check(source_loads==failed_loads && event_registers==failed_registrations && visual_model.current==&prefab);
        failed.Remove(); failed.Service(frame,appearance_services,types);
        failed.Service(foreign,appearance_services,types); failed.Service(foreign,appearance_services,types);
        Check(failed.Status().closed && handles.empty() && source_loads==source_releases);

        // Action handoff pauses source adoption without ending the NPC. Native
        // Art destruction may remain deferred after payload/slot detachment.
        visual_model.current=&prefab;
        api::EntityCreationSession paused_npc(api::EntityOwner{},M(CreateNpc),entity_methods,visual_refs); paused_npc.Create(create);
        framework::NpcAppearance paused(paused_npc,npc_world,appearance,payload,framework::VisualRoot::Prefab,"",
            methods.models);
        paused.Service(frame,appearance_services); Check(paused.Active()->Slot().adopted && !paused.InputsDetached());
        paused.SetEnabled(false); paused.Service(frame,appearance_services);
        Check(!paused.Active() && paused.InputsDetached() && paused.Status().retiring==1 && game_object.alive);
        const auto paused_loads=source_loads;
        paused.Service(frame,appearance_services); Check(source_loads==paused_loads && !paused.Status().closed);
        game_object.alive=renderer.alive=false; paused.Service(frame,appearance_services);
        Check(paused.Status().retiring==0 && !paused_npc.Status().removal_requested);
        visual_model.current=&prefab; paused.SetEnabled(true); paused.Service(frame,appearance_services);
        Check(paused.Status().incarnations==2 && paused.Active()->Slot().adopted && source_loads==paused_loads+1);
        paused.RequestClose(); paused_npc.Remove(); paused.Service(frame,appearance_services); paused_npc.Service(frame);
        game_object.alive=renderer.alive=false; paused.Service(foreign,appearance_services); paused.Service(foreign,appearance_services);
        Check(paused.Status().closed && paused_npc.Status().closed && handles.empty() && source_loads==source_releases);

        config.events=&event_paths; visual_model.current=&prefab;
        framework::CharacterInstance handoff(npc_world,package,payload,"",methods,event_services,framework::VisualRoot::Prefab);
        handoff.Create(create); handoff.Service(frame,appearance_services,types);
        handoff.EnableActionPlayback(animations);
        Check(!handoff.ExplorationInputsDetached() && event_records.slots.size()==1);
        handoff.SetExplorationInputsEnabled(false); handoff.Service(frame,appearance_services,types);
        Check(handoff.ExplorationInputsDetached() && event_records.slots.empty() && game_object.alive && !handoff.Creation().closed);
        game_object.alive=renderer.alive=false; handoff.Service(frame,appearance_services,types);
        handoff.SetExplorationInputsEnabled(true); handoff.Service(frame,appearance_services,types);
        Check(event_records.slots.empty()); // no Art yet: don't put Player events on an unadopted model
        handoff.QueueExplorationAction(framework::NpcMotion{17}); handoff.Service(frame,appearance_services,types);
        Check(npc_motion_calls==2 && handoff.Actions()->Status().pending);
        visual_model.current=&prefab; handoff.Service(frame,appearance_services,types);
        Check(handoff.AppearanceConsumer()->Active()->Slot().adopted && event_records.slots.size()==1 && npc_motion_calls==3);
        // Alternate Art is loaded by the separate official model request.
        // Only payload adoption is selected here; no simultaneous Art hooks.
        handoff.SetAlternatePayloadEnabled(true); handoff.Service(frame,appearance_services,types);
        Check(handoff.ExplorationInputsDetached() && !handoff.AppearanceConsumer()->Active() && event_records.slots.empty());
        game_object.alive=renderer.alive=false;
        handoff.Service(frame,appearance_services,types); handoff.Service(frame,appearance_services,types);
        Check(handoff.AppearanceConsumer()->Status().retiring==0 && !handoff.AlternatePayload()->Active());
        // Substitute the official loader completing a new incarnation. Reuse
        // pointer values only after the previous incarnation fully ended.
        game_object.alive=renderer.alive=true; visual_model.current=&game_object;
        handoff.Service(frame,appearance_services,types);
        Check(handoff.AlternatePayload()->Active()->published && !handoff.AppearanceConsumer()->Active() && event_records.slots.empty());
        handoff.SetAlternatePayloadEnabled(false); handoff.Service(frame,appearance_services,types);
        Check(handoff.AlternatePayloadDetached() && renderer.mesh==&source_mesh && !handoff.Creation().removal_requested);
        handoff.Service(frame,appearance_services,types);
        visual_model.current=&prefab; game_object.alive=renderer.alive=false;
        handoff.SetExplorationInputsEnabled(true); handoff.Service(frame,appearance_services,types);
        Check(handoff.AppearanceConsumer()->Active()->Slot().adopted && event_records.slots.size()==1 && !handoff.AlternatePayload()->Active());
        handoff.Remove(); handoff.Service(frame,appearance_services,types);
        game_object.alive=renderer.alive=false; handoff.Service(foreign,appearance_services,types); handoff.Service(foreign,appearance_services,types);
        Check(handoff.Status().closed && handles.empty() && source_loads==source_releases);

        // Whole request -> official load -> alternate payload -> event input
        // -> play -> restore -> private Art -> event supplements, same NPC.
        visual_model.current=&prefab; model_loaded=true;
        original_path.text="npc.prefab"; original_config.text="npc.config";
        profile_path=&original_path; profile_config=&original_config;
        event_records.event_files=&baseline_paths; event_records.excluded_layers=&profile_layers;
        auto complete_model=owned_model; complete_model.animator=M(ModelAnimator); complete_model.load_state=M(ModelState);
        complete_model.root_offset=offsetof(VisualModel,root); complete_model.has_root_field=true;
        auto complete_methods=methods;
        complete_methods.models=TestModelServices(visual_refs,lifetime,renderers,entity_methods,api::ModelAccess(complete_model),admission);
        framework::CharacterInstance complete(npc_world,package,payload,"",complete_methods,event_services,framework::VisualRoot::Prefab);
        complete.Create(create);
        const api::ProfileInputs profile_input({M(ProfilePath),M(ProfileConfig),M(OperationCtor),M(Ready),{}});
        const api::ProfileEventFields fields{offsetof(Node,event_files),offsetof(Node,excluded_layers),true,true,{}};
        auto inputs=std::make_unique<api::ModelProfileInputs>(visual_refs);
        inputs->Prepare(objects_source,arrays,profile_input,fields,&visual_model,&event_records,{"battle.prefab",{"battle.event"},{}});
        Check(inputs->Status().prepared);
        api::ModelMethods profile_methods; profile_methods.begin=M(BeginProfile); profile_methods.cancel=M(CancelProfile);
        profile_methods.events=M(SetProfileEvents); profile_methods.can_change=M(CanChangeProfile); profile_methods.restore=M(RestoreProfile);
        api::ModelRequestServices profile_services{api::ModelProfile(profile_methods),
            api::ModelAccess(complete_model),profile_input,scene,api::AnimationEvents(event_methods),api::ModelLoadCompleted};
        auto token=api::TrackedEntities().Current(npc_entity);
        auto request=std::make_unique<api::ModelProfileRequest>(token,npc_world,std::move(inputs),visual_refs,profile_services);
        action_methods.hash=M(StateHash); action_methods.has_state=M(HasState); action_methods.free_style_state=M(PlayBattle);
        api::AnimationControl animation(action_methods); api::EntityAccess entity_access(entity_methods);
        api::ControllerBindingServices control_services{animation,entity_access,scene,arrays,{},{},{}};
        auto transition=[=](api::EntityToken owner,api::GameWorld* world,std::unique_ptr<api::ModelProfileRequest> input) {
            return std::make_unique<api::ModelTransition>(owner,world,std::move(input),
                api::ModelTransitionServices{visual_refs,entity_access,api::ModelAccess(complete_model),animation,control_services,fields});
        };
        auto complete_animations=[=](api::EntityToken owner){return std::make_unique<api::ActorAnimationSession>(owner,visual_refs,
            entity_access,api::ModelAccess(complete_model),animation,profile_input);};
        framework::HandoffServices handoff_services{complete_animations,transition};
        framework::ModelActionHandoff coordinated(complete,token,npc_world,std::move(request),handoff_services);
        using Domain=framework::ActionDomain; using Stage=framework::HandoffPhase;
        coordinated.Queue(Domain::Exploration,framework::NpcMotion{17});
        coordinated.Service(frame,appearance_services,types);
        Check(coordinated.Status().phase==Stage::Ready && event_records.slots.size()==1 && complete.AppearanceConsumer()->Active()->Slot().adopted);
        coordinated.Queue(Domain::Alternate,framework::FreeStyleState{"AnyBattleState"});
        coordinated.Service(frame,appearance_services,types);
        Check(coordinated.Status().phase==Stage::Loading && profile_begins==1 && battle_motion_calls==0 && event_records.slots.empty());
        game_object.alive=renderer.alive=false; coordinated.Service(frame,appearance_services,types);
        Check(complete.AppearanceConsumer()->Status().retiring==0);
        // Official async loader now supplies a distinct incarnation. Test
        // storage reused only after the previous native object was retired.
        game_object.alive=renderer.alive=true; visual_model.current=&game_object; model_loaded=true;
        coordinated.Service(frame,appearance_services,types);
        if(coordinated.Status().phase!=Stage::Ready || battle_motion_calls!=1) {
            std::cerr<<"handoff phase="<<static_cast<int>(coordinated.Status().phase)<<" error="<<coordinated.Status().error
                <<" battle="<<battle_motion_calls<<" event_sets="<<profile_events<<'\n';
            if(const auto* model_request=coordinated.ModelRequest()) for(const auto& e:model_request->errors) std::cerr<<e<<'\n';
            for(const auto& e:complete.AlternatePayload()->Status().errors) std::cerr<<e<<'\n';
            if(coordinated.Playback().last) std::cerr<<"action="<<coordinated.Playback().last->detail<<'\n';
        }
        Check(coordinated.Status().phase==Stage::Ready && battle_motion_calls==1 && profile_events==1 && complete.AlternatePayload()->Active()->published);
        Check(!complete.AppearanceConsumer()->Active() && !complete.Creation().removal_requested);
        coordinated.Queue(Domain::Exploration,framework::NpcMotion{17}); coordinated.Service(frame,appearance_services,types);
        Check(coordinated.Status().phase==Stage::Loading && profile_restores==1 && renderer.mesh==&source_mesh);
        game_object.alive=renderer.alive=false; coordinated.Service(frame,appearance_services,types);
        visual_model.current=&prefab; model_loaded=true;
        coordinated.Service(frame,appearance_services,types);
        Check(coordinated.Status().phase==Stage::Ready && profile_events==2 && event_records.slots.size()==1 &&
            event_records.event_files==&baseline_paths && complete.AppearanceConsumer()->Active()->Slot().adopted && !complete.AlternatePayload()->Active());
        coordinated.Remove(); coordinated.Service(frame,appearance_services,types);
        game_object.alive=renderer.alive=false;
        coordinated.Service(foreign,appearance_services,types); coordinated.Service(foreign,appearance_services,types);
        Check(coordinated.Status().closed && handles.empty() && source_loads==source_releases);

        // Same external path, now assembled from the source plan rather than
        // manually capturing profile inputs and constructing the coordinator.
        visual_model.current=&prefab; model_loaded=true;
        profile_path=&original_path; profile_config=&original_config;
        event_records.event_files=&baseline_paths; event_records.excluded_layers=&profile_layers;
        framework::PreparedSource prepared; prepared.character=package; prepared.finished=true;
        prepared.presentation=framework::BattlePresentation{{"battle.prefab",{"battle.event"},{}},{}};
        prepared.battle={framework::SourceResolution::Ready,{{77,"battle.config","battle.prefab"}}};
        auto plan=framework::CharacterPlan::FromPrepared(prepared,payload);
        plan.visual_root=framework::VisualRoot::Prefab;
        Check(plan.action_sources.size()==2 && plan.action_sources[1].source.path=="battle.config");
        auto baseline=[=](api::EntityToken owner,api::GameWorld* owner_world,const api::ProfileDescription& profile) {
            return std::make_unique<api::ModelBaselineCapture>(owner,owner_world,visual_refs,entity_access,profile_services,fields,profile);
        };
        framework::CharacterSession session(npc_world,std::move(plan),{complete_methods,event_services,handoff_services,baseline});
        const auto plays=battle_motion_calls;
        session.Create(create); session.Queue(Domain::Exploration,framework::NpcMotion{17});
        session.Service(frame,appearance_services,types);
        Check(session.Status().alternate_available && session.Character().AppearanceConsumer()->Active()->Slot().adopted);
        session.Queue(Domain::Alternate,framework::FreeStyleState{"AnyBattleState"});
        session.Service(frame,appearance_services,types);
        Check(session.Handoff()->Status().phase==Stage::Loading && event_records.slots.empty());
        game_object.alive=renderer.alive=false; session.Service(frame,appearance_services,types);
        game_object.alive=renderer.alive=true; visual_model.current=&game_object; model_loaded=true;
        session.Service(frame,appearance_services,types);
        Check(session.Handoff()->Status().phase==Stage::Ready && battle_motion_calls==plays+1 && session.Character().AlternatePayload()->Active()->published);
        session.Queue(Domain::Exploration,framework::NpcMotion{17}); session.Service(frame,appearance_services,types);
        game_object.alive=renderer.alive=false; session.Service(frame,appearance_services,types);
        visual_model.current=&prefab; model_loaded=true; session.Service(frame,appearance_services,types);
        Check(session.Handoff()->Status().phase==Stage::Ready && profile_config==&original_config &&
            event_records.event_files==&baseline_paths && event_records.slots.size()==1 && session.Character().AppearanceConsumer()->Active()->Slot().adopted);
        session.Remove(); session.Service(frame,appearance_services,types);
        game_object.alive=renderer.alive=false;
        session.Service(foreign,appearance_services,types); session.Service(foreign,appearance_services,types);
        Check(session.Status().closed && handles.empty() && source_loads==source_releases);
    }
    api::RemoveModelReleaseHooksAtQuiescentPoint();
    std::cout<<"Model asset assembly passed: typed payload to private objects, palette/material arrays, complete preparation and detached destruction.\n";
    std::cout<<"Model epochs passed: local discovery, publication, official pre-unload hook restoration and same-component reload isolation.\n";
    std::cout<<"NPC model consumer passed: owned entity to loaded Art, reload, explicit retirement and natural disposal across world frames.\n";
    std::cout<<"Appearance assembly passed: inactive payload preparation, old/new Art separation, defaults after admission, partial failures and renderer-death cleanup.\n";
    std::cout<<"Character source integration passed: official NPC/private appearance/external payload, owned events, retirement order, partial failure and foreign-world cleanup.\n";
    std::cout<<"External model/action handoff passed: private Art/payload/events -> official model and payload -> battle play -> baseline/config/private Art/supplements, same NPC; local only.\n";
}
