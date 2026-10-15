#include <hksr/framework/assets/TextureObjects.hpp>
#include <limits>
#include <cmath>
#include <stdexcept>

namespace hksr::framework {
TexturePreparation PrepareTexture(api::OwnedUnityObjects& owner, const api::TextureAccess& access,
    void* allocated, const TextureDescription& d, const std::vector<std::uint8_t>& payload) {
    TexturePreparation r;
    // Ownership misuse is a programming error and is not caught as an upload
    // failure; Adopt rejects duplicates. The caller establishes private origin.
    r.owned=owner.Adopt(allocated);
    if(!owner.Status(r.owned).root.ready) { r.error="texture retention failed; initialization not attempted"; return r; }
    constexpr auto max=static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)());
    if(!d.width || !d.height || !d.mip_count || d.width>max || d.height>max || d.mip_count>max || d.format>max ||
        payload.empty() || payload.size()>max || payload.size()!=d.payload_bytes || !std::isfinite(d.mip_bias)) {
        r.error="texture description/payload does not fit the native upload contract"; return r;
    }
    r.initialize=access.Initialize(allocated,{static_cast<int>(d.width),static_cast<int>(d.height),static_cast<int>(d.format),static_cast<int>(d.mip_count),d.linear});
    if(!r.initialize.returned) { r.error="texture constructor failed; owned object retained"; return r; }
    r.load=access.LoadRaw(allocated,payload.data(),static_cast<int>(payload.size()));
    if(!r.load.read.returned || !r.load.value) { r.error="raw texture data was not accepted"; return r; }
    r.sampling=access.Sampling(allocated,{d.filter,d.anisotropy,d.wrap[0],d.wrap[1],d.wrap[2],d.mip_bias});
    if(!r.sampling.Returned()) { r.error="one or more texture sampling operations failed"; return r; }
    // The payload already includes its authored mip chain. Do not regenerate it.
    r.apply=access.Apply(allocated,false,true);
    r.ready=r.apply.returned;
    if(!r.ready) r.error="texture Apply failed";
    return r;
}
}
