#include <hksr/framework/assets/ReferenceArrays.hpp>
#include <limits>
namespace hksr::framework {
ReferenceArrayPreparation PrepareReferenceArray(api::ManagedRoots& roots,const api::ManagedArrays& arrays,
    void* type,const std::vector<void*>& values) {
    ReferenceArrayPreparation r;
    if(!type || values.size()>static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)())) {
        r.error="reference array type/count invalid"; return r;
    }
    r.array=arrays.Create(type,static_cast<std::int32_t>(values.size()));
    if(!r.array.call.returned || !r.array.object) { r.error="reference array creation failed"; return r; }
    r.root=roots.Keep(r.array.object);
    if(!roots.Status(r.root).ready) { r.error="reference array retention failed"; return r; }
    r.ready=true;
    for(std::size_t i=0;i<values.size();++i) {
        const auto written=arrays.Set(r.array.object,values[i],static_cast<std::int32_t>(i));
        r.writes.push_back(written); r.ready=written.returned && r.ready;
    }
    if(!r.ready) r.error="one or more reference array writes failed";
    return r;
}
BonePalettePreparation PrepareBonePalette(api::ManagedRoots& roots,const api::ManagedArrays& arrays,
    const api::HierarchyAccess& hierarchy,void* art,void* type,const std::vector<void*>& paths) {
    BonePalettePreparation r;
    if(!art || !type || paths.empty()) { r.error="bone palette requires instance Art, Transform type and ordered paths"; return r; }
    std::vector<void*> targets; targets.reserve(paths.size()); bool complete=true;
    for(auto* path:paths) {
        const auto found=path?hierarchy.Find(art,path):api::ObjectResult{};
        r.bones.push_back(found); targets.push_back(found.object);
        complete=found.call.returned && found.object && complete;
    }
    if(!complete) { r.error="bone palette contains unresolved paths; all path results retained"; return r; }
    r.array=PrepareReferenceArray(roots,arrays,type,targets); r.ready=r.array.ready; r.error=r.array.error;
    return r;
}
}
