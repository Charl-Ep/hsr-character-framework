#pragma once
#include <Windows.h>
#include <cstdint>

namespace hksr::framework::development {
// Optional development-only sink. Ordinary Framework builds have no research
// imports, source dependency, or automatic DLL loading. V1 is six borrowed
// values delivered synchronously on the official frame; never queued to a UI.
inline void ObserveModel(std::uint64_t actor,std::uint64_t generation,int domain,
    void* entity,void* model,void* animator) {
    const auto module=GetModuleHandleW(L"Hksr.RuntimeInspection.dll");
    if(!module)return;
    using Sink=void(__cdecl*)(std::uint64_t,std::uint64_t,int,void*,void*,void*);
    const auto sink=reinterpret_cast<Sink>(GetProcAddress(module,"HksrInspection_ObserveModelV1"));
    if(sink)sink(actor,generation,domain,entity,model,animator);
}
}
