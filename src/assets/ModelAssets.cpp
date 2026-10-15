#include <hksr/framework/assets/ModelAssets.hpp>
#include <map>
#include <set>
#include <stdexcept>

namespace hksr::framework {
namespace {
void* Object(api::ObjectResult result) {
    if(!result.call.returned || !result.object) throw std::runtime_error("managed object operation did not return an object");
    return result.object;
}
}
ModelAssets::ModelAssets(std::shared_ptr<const api::ModelServices> services)
    :roots_(services->CreateRoots()),native_(services->CreateNativeObjects()),bindings_(std::move(services)){}
void* ModelAssets::Keep(api::ObjectResult object) {
    auto* value=Object(object); root_ids_.push_back(0);
    const auto id=roots_->Keep(value); root_ids_.back()=id;
    if(!roots_->Status(id).ready) throw std::runtime_error("managed asset input retention failed");
    return value;
}
void* ModelAssets::String(api::ManagedObjects& source,const std::string& text) { return Keep(source.NewStringUtf8(text.c_str())); }
ModelAssetReceipt ModelAssets::Prepare(ModelAssetServices s,const ModelAssetInput& in,void* art,const std::vector<ModelRendererTarget>& targets) {
    if(started_ || closing_) throw std::logic_error("one ModelAssets owner describes one live model epoch");
    started_=true; ModelAssetReceipt out;
    auto& errors=out.result.errors;
    try {
        if(!art || in.manifest.meshes.empty() || in.meshes.size()!=in.manifest.meshes.size())
            throw std::invalid_argument("model asset/Art inputs incomplete");
        std::map<std::string,const ModelRendererTarget*> roles;
        std::set<void*> renderers;
        for(const auto& target:targets)
            if(!target.renderer || !roles.emplace(target.role,&target).second || !renderers.insert(target.renderer).second)
                throw std::invalid_argument("model targets must identify unique roles and renderers");
        if(in.manifest.materials==MaterialSource::Package && in.materials.textures.size()!=in.texture_payloads.size())
            throw std::invalid_argument("texture description/payload count mismatch");
        std::map<std::pair<std::size_t,std::size_t>,const MaterialDescription*> descriptions;
        if(in.manifest.materials==MaterialSource::Package) for(const auto& material:in.materials.materials) {
            if(material.renderer_role>=in.manifest.meshes.size() || material.slot>=in.manifest.meshes[material.renderer_role].submeshes.size() ||
                !descriptions.emplace(std::make_pair(material.renderer_role,material.slot),&material).second)
                throw std::invalid_argument("material role/slot duplicate or outside mesh description");
        }
        // These roots are input/scratch only; native resources have a separate
        // owner. A failure never conflates FreeHandle with Unity Destroy.
        auto* transform_type=Keep(s.objects.ManagedType("UnityEngine","Transform"));
        auto* material_type=Keep(s.objects.ManagedType("UnityEngine","Material"));
        std::vector<void*> textures(in.materials.textures.size(),nullptr);
        if(in.manifest.materials==MaterialSource::Package) for(std::size_t i=0;i<textures.size();++i) {
            try {
                auto* object=Object(s.objects.AllocateObject("UnityEngine","Texture2D"));
                auto result=PrepareTexture(*native_,s.texture,object,in.materials.textures[i],in.texture_payloads[i]);
                if(result.ready) textures[i]=object;
                else errors.push_back("texture "+std::to_string(i)+": "+result.error);
                out.textures.push_back(std::move(result));
            } catch(const std::exception& e) { errors.push_back("texture "+std::to_string(i)+": "+e.what()); }
        }
        std::vector<RendererOverride> replacements;
        for(std::size_t role=0;role<in.manifest.meshes.size();++role) {
            const auto& d=in.manifest.meshes[role];
            try {
                const auto found=roles.find(d.renderer);
                if(found==roles.end()) throw std::runtime_error("no target for authored renderer role");
                const auto& target=*found->second;
                MeshPreparation mesh;
                void* mesh_object=nullptr;
                if(in.shapes==ShapeData::MatchingTemplate) {
                    mesh=PrepareTemplateMesh(*native_,s.mesh,s.skinning,s.mesh_templates,target.template_mesh,d,in.meshes[role]);
                    mesh_object=mesh.clone.object;
                } else {
                    mesh_object=Object(s.objects.AllocateObject("UnityEngine","Mesh"));
                    mesh=PrepareFreshMesh(*native_,s.mesh,s.skinning,mesh_object,d,in.meshes[role]);
                }
                const bool mesh_ready=mesh.ready; out.meshes.push_back(std::move(mesh));
                if(!mesh_ready) errors.push_back(d.renderer+": "+out.meshes.back().error);
                std::vector<void*> paths; paths.reserve(d.bone_paths.size());
                for(const auto& path:d.bone_paths) paths.push_back(String(s.objects,path));
                auto palette=PrepareBonePalette(*roots_,s.arrays,s.hierarchy,art,transform_type,paths);
                if(palette.array.root) root_ids_.push_back(palette.array.root);
                auto* bones=palette.array.array.object; const bool bones_ready=palette.ready;
                if(!bones_ready) errors.push_back(d.renderer+": "+palette.error);
                out.palettes.push_back(std::move(palette));
                if(!target.template_materials) throw std::runtime_error("independent material templates missing");
                const auto count=s.arrays.Length(target.template_materials);
                if(!count.read.returned || count.value<static_cast<int>(d.submeshes.size()))
                    throw std::runtime_error("source material slots unavailable or too short");
                std::vector<void*> slots(d.submeshes.size(),nullptr); bool slots_ready=true;
                for(std::size_t slot=0;slot<slots.size();++slot) {
                    try {
                        auto* source=Keep(s.arrays.Get(target.template_materials,static_cast<int>(slot)));
                        std::vector<TextureBindingInput> bindings;
                        if(in.manifest.materials==MaterialSource::Package) {
                            const auto md=descriptions.find({role,slot});
                            if(md==descriptions.end()) throw std::runtime_error("package material slot missing");
                            for(const auto& prop:md->second->properties) if(prop.kind==4) {
                                void* texture=nullptr;
                                if(prop.texture_index>=0) {
                                    const auto index=static_cast<std::size_t>(prop.texture_index);
                                    if(index>=textures.size() || !textures[index]) throw std::runtime_error("referenced package texture not ready");
                                    texture=textures[index];
                                }
                                bindings.push_back({String(s.objects,prop.name),texture,
                                    {prop.scale_offset[0],prop.scale_offset[1]},{prop.scale_offset[2],prop.scale_offset[3]}});
                            }
                        }
                        auto* material=Object(s.objects.AllocateObject("UnityEngine","Material"));
                        auto prepared=PrepareMaterial(*native_,s.material,s.shader,s.texture,material,source,bindings);
                        if(prepared.ready) slots[slot]=material;
                        else { slots_ready=false; errors.push_back(d.renderer+" material "+std::to_string(slot)+": "+prepared.error); }
                        out.materials.push_back(std::move(prepared));
                    } catch(const std::exception& e) { slots_ready=false; errors.push_back(d.renderer+" material "+std::to_string(slot)+": "+e.what()); }
                }
                if(slots_ready) {
                    auto array=PrepareReferenceArray(*roots_,s.arrays,material_type,slots);
                    if(array.root) root_ids_.push_back(array.root);
                    if(!array.ready) errors.push_back(d.renderer+": "+array.error);
                    if(mesh_ready && bones_ready && array.ready) replacements.push_back({target.renderer,mesh_object,array.array.object,bones});
                    out.material_arrays.push_back(std::move(array));
                }
            } catch(const std::exception& e) { errors.push_back(d.renderer+": "+e.what()); }
        }
        // Publish only a complete model override. Collecting independent role
        // failures is not the same as displaying a half-official character.
        if(closing_) errors.push_back("model epoch ended during resource preparation");
        if(errors.empty()) out.result=bindings_.Prepare(replacements);
        out.result.complete=out.result.errors.empty(); prepared_=out.result.complete;
    } catch(const std::exception& e) { errors.push_back(e.what()); out.result.complete=false; }
    return out;
}
BindingResult ModelAssets::Apply() {
    if(!prepared_) throw std::logic_error("model assets not prepared");
    return bindings_.Apply();
}
BindingResult ModelAssets::BeforeModelRelease() { closing_=true; return bindings_.BeforeModelRelease(); }
BindingResult ModelAssets::Detach() { closing_=true; return bindings_.Restore(); }
void ModelAssets::SealWithoutRestore() noexcept { closing_=true; bindings_.SealWithoutRestore(); }
BindingResult ModelAssets::ObserveReleasedRenderers() { return bindings_.ObserveReleasedRenderers(); }
BindingResult ModelAssets::Close() {
    auto result=Detach();
    if(!result.complete) return result; // still referenced; do NOT Destroy them
    const auto references=bindings_.Close();
    result.errors.insert(result.errors.end(),references.errors.begin(),references.errors.end());
    // Once renderer references are restored, GC-handle cleanup failures do not
    // make private native objects referenced again. Drain independent cleanup.
    for(const auto& state:native_->CloseAll())
        if(!state.complete) result.errors.push_back("native object "+std::to_string(state.id)+" destruction pending/unresolved");
    // Scratch arrays/strings may be freed after detachment even while native
    // destruction drains; OwnedUnityObjects retains its own independent roots.
    for(auto& id:root_ids_) if(id) {
        if(roots_->Release(id).complete) { roots_->Forget(id); id=0; }
        else result.errors.push_back("asset input root "+std::to_string(id)+" release unresolved");
    }
    result.complete=result.errors.empty(); return result;
}
}
