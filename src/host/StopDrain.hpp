#pragma once
#include <hksr/framework/layout/LayoutIo.hpp>
#include <hksr/runtime/RuntimeHost.hpp>

namespace hksr::framework::detail {
// A step on the IO worker, never a wait on the engine/UI/loader thread.
// With no further official frame a running client stays pending. Empty or
// failed subscriptions are terminal too, and cannot submit another capture.
inline bool PumpStoppingLayouts(api::RuntimeHostPhase phase,LayoutIo& io) {
    const bool terminal=phase==api::RuntimeHostPhase::Stopped || phase==api::RuntimeHostPhase::Failed;
    if(terminal)io.Seal();
    io.Pump();
    return terminal && io.Status().Drained();
}
}
