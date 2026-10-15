#include <hksr/framework/assets/ModelTargets.hpp>
#include <stdexcept>
#include <set>

namespace hksr::framework {
void* ModelTargets::Keep(api::ObjectResult value) {
    if(!value.call.returned || !value.object) throw std::runtime_error("object read/allocation did not return a non-null object");
    ids_.reserve(ids_.size()+1);
    const auto id=roots_->Keep(value.object); ids_.push_back(id);
    if(!roots_->Status(id).ready) throw std::runtime_error("managed object could not be retained");
    return value.object;
}
ModelTargetReceipt ModelTargets::Discover(api::ManagedObjects& objects,const api::SceneObjects& scene,
    const api::ManagedArrays& arrays,const api::RendererAccess& renderers,
    const ModelAssetInput& input,void* instance_art,void* source_art) {
    const auto& manifest=input.manifest;
    ModelTargetReceipt out;
    if(started_ || closed_) { out.result.errors.push_back("target discovery epoch already used or closed"); return out; }
    started_=true;
    try {
        if(!instance_art || !source_art || manifest.meshes.empty()) throw std::runtime_error("model art/source/roles are missing");
        Keep({{true,0},instance_art}); if(source_art!=instance_art) Keep({{true,0},source_art});
        out.art_transform=Keep(scene.Transform(instance_art));
        auto* type=Keep(objects.ManagedType("UnityEngine","SkinnedMeshRenderer"));
        std::vector<void*> wanted;
        std::set<std::string> roles;
        for(const auto& description:manifest.meshes) {
            if(description.renderer.empty() || !roles.insert(description.renderer).second)
                throw std::runtime_error("renderer role is empty or duplicated in the package");
            wanted.push_back(Keep(objects.NewStringUtf8(description.renderer.c_str())));
        }
        const auto scan=[&](void* art,const char* side) {
            std::vector<std::vector<void*>> matches(wanted.size());
            try {
            auto* list=Keep(scene.Components(art,type,true));
            const auto count=arrays.Length(list);
            if(!count.read.returned || count.value<0) throw std::runtime_error(std::string(side)+": component array length unavailable");
            std::set<void*> seen;
            for(std::int32_t i=0;i<count.value;++i) {
                try {
                    const auto component=arrays.Get(list,i);
                    if(!component.call.returned) throw std::runtime_error("component read failed");
                    if(!component.object || !seen.insert(component.object).second) continue;
                    auto* name=Keep(scene.Name(component.object));
                    for(std::size_t role=0;role<wanted.size();++role) {
                        const auto equal=scene.EqualNames(name,wanted[role]);
                        if(!equal.read.returned) {
                            out.result.errors.push_back(std::string(side)+": name comparison failed for "+manifest.meshes[role].renderer);
                        } else if(equal.value) matches[role].push_back(component.object);
                    }
                } catch(const std::exception& e) { out.result.errors.push_back(std::string(side)+" component "+std::to_string(i)+": "+e.what()); }
            }
            for(std::size_t role=0;role<matches.size();++role)
                if(matches[role].size()!=1) out.result.errors.push_back(std::string(side)+": "+manifest.meshes[role].renderer+" matches="+std::to_string(matches[role].size()));
            } catch(const std::exception& e) { out.result.errors.push_back(std::string(side)+": "+e.what()); }
            return matches;
        };
        const auto targets=scan(instance_art,"instance");
        const auto sources=source_art==instance_art?targets:scan(source_art,"source");
        for(std::size_t role=0;role<manifest.meshes.size();++role) {
            if(targets[role].size()!=1 || sources[role].size()!=1) continue;
            try {
                auto* materials=Keep(renderers.Materials(sources[role].front()));
                // Fresh meshes do not consume a source mesh. Do not require a
                // getter or an unrelated source topology for that path.
                void* mesh=nullptr;
                if(input.shapes==ShapeData::MatchingTemplate) mesh=Keep(renderers.Mesh(sources[role].front()));
                out.targets.push_back({manifest.meshes[role].renderer,targets[role].front(),mesh,materials});
            } catch(const std::exception& e) { out.result.errors.push_back(manifest.meshes[role].renderer+": "+e.what()); }
        }
        out.result.complete=out.result.errors.empty() && out.targets.size()==manifest.meshes.size();
    } catch(const std::exception& e) { out.result.errors.push_back(e.what()); }
    // A partial discovery is diagnostic only, never a usable replacement set.
    if(!out.result.complete) out.targets.clear();
    return out;
}
BindingResult ModelTargets::Close() {
    closed_=true;
    BindingResult out; out.complete=true;
    for(auto& id:ids_) {
        if(!id) continue;
        const auto released=roots_->Release(id);
        if(released.complete) { roots_->Forget(id); id=0; }
        else { out.complete=false; out.errors.push_back("target discovery GC root release unresolved"); }
    }
    return out;
}
}
