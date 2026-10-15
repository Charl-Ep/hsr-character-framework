#include <hksr/framework/assets/MaterialObjects.hpp>
#include <set>
#include <cmath>
#include <stdexcept>

namespace hksr::framework {
MaterialPreparation PrepareMaterial(api::OwnedUnityObjects& owner, const api::MaterialAccess& material,
    const api::ShaderAccess& shader, const api::TextureAccess& textures, void* allocated,
    void* source, const std::vector<TextureBindingInput>& inputs) {
    if(allocated && allocated==source) throw std::invalid_argument("source template cannot be adopted as the output material");
    MaterialPreparation r;
    r.owned=owner.Adopt(allocated);
    if(!owner.Status(r.owned).root.ready) { r.error="material retention failed"; return r; }
    if(!source) { r.error="material needs an independent source template"; return r; }
    r.initialize=material.InitializeCopy(allocated,source);
    if(!r.initialize.returned) { r.error="material copy failed; owned object retained"; return r; }
    if(inputs.empty()) { r.ready=true; return r; } // Template-only packages need no property queries.
    r.shader=shader.Inspect(allocated);
    std::set<std::int32_t> assigned;
    r.ready=true;
    for(const auto& input:inputs) {
        TextureBindingResult b;
        auto apply=[&]() {
            if(!input.managed_property_name) { b.error="missing managed property name"; return; }
            b.property_id=material.PropertyId(input.managed_property_name);
            if(!b.property_id.read.returned) { b.error="property name resolution failed"; return; }
            if(!assigned.insert(b.property_id.value).second) { b.error="duplicate texture property assignment"; return; }
            const api::ShaderProperty* found=nullptr;
            for(const auto& p:r.shader.properties) if(p.id.read.returned && p.id.value==b.property_id.value) {
                if(found) { b.error="ambiguous shader property identity"; return; } found=&p;
            }
            if(!found) { b.error=r.shader.complete?"property absent from shader":"property unresolved in partial shader inspection"; return; }
            if(!found->type.read.returned || found->type.value!=4) { b.error="requested property is not a verified texture input"; return; }
            if(input.texture) {
                b.texture_dimension=textures.Dimension(input.texture);
                if(!b.texture_dimension.read.returned || !found->texture_dimension.read.returned ||
                    b.texture_dimension.value!=found->texture_dimension.value) { b.error="texture and shader dimensions do not match"; return; }
            }
            if(!std::isfinite(input.scale.x) || !std::isfinite(input.scale.y) || !std::isfinite(input.offset.x) || !std::isfinite(input.offset.y)) {
                b.error="non-finite texture transform"; return;
            }
            b.texture=material.Texture(allocated,b.property_id.value,input.texture);
            b.scale=material.Scale(allocated,b.property_id.value,input.scale);
            b.offset=material.Offset(allocated,b.property_id.value,input.offset);
            b.ready=b.texture.returned && b.scale.returned && b.offset.returned;
            if(!b.ready) b.error="one or more material texture writes failed";
        };
        apply(); r.ready=r.ready && b.ready; r.bindings.push_back(std::move(b));
    }
    if(!r.ready) r.error="material has unresolved texture bindings; not publishable";
    return r;
}
}
