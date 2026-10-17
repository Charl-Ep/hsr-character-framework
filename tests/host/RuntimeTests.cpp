#include "../support/ModelServicesFixture.hpp"
#include <hksr/framework/assets/RendererBindings.hpp>
#include <hksr/model/ModelRelease.hpp>
#include <Windows.h>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
using namespace hksr::api;
using namespace hksr::framework;
int original_mesh, original_materials, external_mesh, external_materials, metadata, model_storage;
int original_bones,external_bones;
struct Renderer { void* mesh = &original_mesh; void* materials = &original_materials; void* bones=&original_bones; };
Renderer first, second;
bool setter_fault = false, getter_fault = false, restore_fault = false;
bool bone_fault=false;
unsigned assignments = 0, releases = 0;
int next_handle = 0;
std::map<int, void*> roots;
RendererBindings* bindings = nullptr;
void Check(bool ok) { if (!ok) throw std::runtime_error("framework runtime test failed"); }
void* __fastcall Mesh(Renderer* renderer, void* info) { Check(info == &metadata); return renderer->mesh; }
void* __fastcall Materials(Renderer* renderer, void* info) {
    Check(info == &metadata);
    if (getter_fault && renderer == &second) RaiseException(0xE0490001, 0, 0, nullptr);
    return renderer->materials;
}
void __fastcall SetMesh(Renderer* renderer, void* mesh, void* info) { Check(info == &metadata); ++assignments; renderer->mesh = mesh; }
void* __fastcall Bones(Renderer* renderer,void* info) { Check(info==&metadata); return renderer->bones; }
void __fastcall SetBones(Renderer* renderer,void* value,void* info) {
    Check(info==&metadata); ++assignments; renderer->bones=value;
    if(bone_fault && value==&external_bones) RaiseException(0xE0490004,0,0,nullptr);
}
void __fastcall SetMaterials(Renderer* renderer, void* materials, void* info) {
    Check(info == &metadata); ++assignments; renderer->materials = materials;
    if (setter_fault && renderer == &second && materials == &external_materials) RaiseException(0xE0490002, 0, 0, nullptr);
    if (restore_fault && materials == &original_materials) RaiseException(0xE0490003, 0, 0, nullptr);
}
int __fastcall Retain(void* object, int handle, int kind, void*) { Check(!handle && kind == 2); roots[++next_handle] = object; return next_handle; }
void* __fastcall Target(int handle, void*) { return roots.at(handle); }
void __fastcall Free(int handle, void*) { roots.erase(handle); }
__declspec(noinline) void __fastcall ArtUnload(CharacterModelComponent*, bool option, void* info) {
    Check(option && info == &metadata); ++releases;
    Check(first.mesh == &original_mesh && first.materials == &original_materials);
    Check(second.mesh == &original_mesh && second.materials == &original_materials);
    Check(first.bones==&original_bones && second.bones==&original_bones);
}
__declspec(noinline) void __fastcall RootRelease(CharacterModelComponent*, void* info) {
    Check(info == &metadata); ++releases;
    Check(first.mesh == &original_mesh && first.materials == &original_materials);
}
template<class T> Method M(T function) { return {reinterpret_cast<void*>(function), &metadata}; }
struct Resolver : MethodResolver {
    Method Resolve(const char*, const char* owner, const char* name, const char* result, std::initializer_list<const char*> args, MethodKind kind) override {
        Check(kind == MethodKind::Instance);
        const std::string type = owner, method = name;
        if (method == "get_sharedMesh") { Check(type == "SkinnedMeshRenderer" && std::string(result) == "UnityEngine.Mesh" && args.size() == 0); return M(Mesh); }
        if (method == "set_sharedMesh") { Check(type == "SkinnedMeshRenderer" && args.size() == 1); return M(SetMesh); }
        if (method == "get_sharedMaterials") { Check(type == "Renderer" && std::string(result) == "UnityEngine.Material[]"); return M(Materials); }
        if (method == "set_sharedMaterials") { Check(type == "Renderer" && args.size() == 1); return M(SetMaterials); }
        if(method=="get_bones") { Check(type=="SkinnedMeshRenderer" && std::string(result)=="UnityEngine.Transform[]"); return M(Bones); }
        if(method=="set_bones") { Check(type=="SkinnedMeshRenderer" && args.size()==1); return M(SetBones); }
        if (method == "OnArtNodeUnload") return M(ArtUnload);
        if (method == "_ReleaseRootModel") return M(RootRelease);
        throw std::runtime_error("unexpected renderer dependency");
    }
};
void Before(const ModelReleaseEvent&, void*) { Check(bindings->BeforeModelRelease().complete); }
void Failure(const ModelReleaseEvent&, const char*, void*) noexcept { std::terminate(); }
}
void TestTextureObjects();
void TestMaterialObjects();
void TestMeshObjects(int argc,char** argv);
void TestReferenceArrays();
void TestModelAssets();
void TestModelTargets();
void TestPrivateVisuals();
void TestAppearanceInputs();
void TestModelProfileInputs();
void TestOperationHistory();
void TestApplicationService();
void TestApplicationCoordinator();
void TestApplicationEvents();
void TestActionPlayback();
void TestPresentationProfiles();
void TestVisualEffectContext();
void TestVisualProgram(const char* file);
void TestVisualPlayback();
void TestReadyPlayback();
void TestSourceCatalog();
void TestActionCatalog();
void TestCatalogBrowser();
void TestSceneIdentity();
void TestSceneLayout();
void TestLayoutController();
void TestCharacterControls();
void TestTimelinePolicy();
int main(int argc,char** argv) try {
    std::cout<<std::unitbuf;
    if(argc==3 && std::string(argv[1])=="--visual-program") {TestVisualProgram(argv[2]);return 0;}
    TestTimelinePolicy();
    TestSceneIdentity();
    TestSceneLayout();
    TestLayoutController();
    TestCharacterControls();
    TestActionCatalog();
    TestCatalogBrowser();
    TestSourceCatalog();
    TestActionPlayback();
    TestPresentationProfiles();
    TestVisualEffectContext();
    TestVisualProgram(nullptr);
    TestVisualPlayback();
    TestReadyPlayback();
    TestModelProfileInputs();
    TestOperationHistory();
    TestApplicationService();
    TestApplicationCoordinator();
    TestApplicationEvents();
    TestAppearanceInputs();
    TestPrivateVisuals();
    TestModelTargets();
    TestModelAssets();
    TestReferenceArrays();
    TestMeshObjects(argc,argv);
    TestMaterialObjects();
    TestTextureObjects();
    Resolver resolver;
    const auto renderers = ResolveRendererMethods(resolver);
    const ReferenceMethods references{M(Retain), M(Target), M(Free), {}, {}};
    const std::vector<RendererOverride> input{{&first, &external_mesh, &external_materials}, {&second, &external_mesh, &external_materials}};
    {
        RendererBindings failed(TestModelServices(references,{},renderers)); getter_fault = true;
        const auto prepared = failed.Prepare(input); Check(!prepared.complete && assignments == 0);
        getter_fault = false; Check(failed.Close().complete && roots.empty());
    }
    {
        RendererBindings partial(TestModelServices(references,{},renderers));
        Check(partial.Prepare(input).complete && assignments == 0); setter_fault = true;
        Check(!partial.Apply().complete);
        Check(first.mesh == &original_mesh && second.materials == &original_materials);
        Check(partial.Receipts()[1].apply_materials.exception == 0xE0490002);
        setter_fault = false; Check(partial.Close().complete && roots.empty());
    }
    {
        RendererBindings live(TestModelServices(references,{},renderers)); bindings = &live;
        InstallModelReleaseHooks(resolver);
        auto* model = reinterpret_cast<CharacterModelComponent*>(&model_storage);
        const auto token = RegisterModelRelease(model, {Before, Failure, nullptr});
        auto skinned=input; skinned[0].bones=&external_bones;
        Check(live.Prepare(skinned).complete && live.Apply().complete && first.bones==&external_bones);
        Check(first.mesh == &external_mesh && second.materials == &external_materials);
        ArtUnload(model, true, &metadata); // actual native hook restores BEFORE pooling
        const auto written = assignments;
        RootRelease(model, &metadata); // repeated/nested boundary is idempotent
        Check(assignments == written && releases == 2);
        Check(live.Close().complete && roots.empty());
        ForgetModelRelease(token); RemoveModelReleaseHooksAtQuiescentPoint(); bindings = nullptr;
    }
    {
        RendererBindings partial_bones(TestModelServices(references,{},renderers));
        Check(partial_bones.Prepare({{&first,&external_mesh,&external_materials,&external_bones}}).complete);
        bone_fault=true; Check(!partial_bones.Apply().complete); bone_fault=false;
        Check(first.bones==&original_bones && first.mesh==&original_mesh && first.materials==&original_materials);
        Check(partial_bones.Receipts()[0].apply_bones.exception==0xE0490004 && partial_bones.Close().complete && roots.empty());
    }
    {
        first.mesh = nullptr;
        RendererBindings nullable(TestModelServices(references,{},renderers));
        Check(nullable.Prepare({input.front()}).complete && nullable.Apply().complete && nullable.Close().complete);
        Check(first.mesh == nullptr && roots.empty()); first.mesh = &original_mesh;
    }
    {
        RendererBindings uncertain(TestModelServices(references,{},renderers));
        Check(uncertain.Prepare({input.front()}).complete && uncertain.Apply().complete);
        restore_fault = true; Check(!uncertain.BeforeModelRelease().complete); restore_fault = false;
        // Simulate reuse of the same renderer by another official model.
        first.materials = &external_materials;
        const auto before = assignments;
        Check(!uncertain.Close().complete && assignments == before && first.materials == &external_materials);
        first.materials = &original_materials;
        // Unresolved ownership remains recorded; no game-side retry is claimed.
    }
    std::cout << "Framework renderer ownership tests passed across SDK DLL and real local release hooks; no game acceptance.\n";
}catch(const std::exception& error) {
    std::cerr<<"Framework runtime test failure: "<<error.what()<<'\n';
    return 1;
}
