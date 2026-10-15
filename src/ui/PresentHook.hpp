#pragma once
#include <functional>
#include <string>
struct IDXGISwapChain;
namespace hksr::framework::ui {
// Process-resident DXGI observer, independent of XXMI and of game callbacks.
// Install outside loader lock. Stop drains callbacks, not executable hooks;
// callers must keep this code loaded until process exit (no hot unload).
void StartPresentHook(std::function<void(IDXGISwapChain*)> draw,
                      std::function<void(const std::string&)> report);
void StopPresentHook() noexcept;
}
