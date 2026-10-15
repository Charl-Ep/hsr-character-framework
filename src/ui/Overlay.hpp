#pragma once
#include <hksr/framework/host/FrameworkHost.hpp>
#include <filesystem>
struct IDXGISwapChain;
namespace hksr::framework::ui {
class Overlay {
public:
    Overlay();
    // No game calls. Draws only the menu, using the borrowed DXGI swap chain.
    // Errors are reported independently; they don't stop character lifecycle.
    void Draw(IDXGISwapChain* chain,FrameworkHost& host);
    void Stop();
    std::string Error() const;
    // Optional research observation; no cursor changes or game calls.
    std::string InputState() const;
    // Call from the existing extension worker, never from Draw/Window.
    void ConfigurePreferences(std::filesystem::path file);
    std::string FlushPreferences();
private:
    struct State;
    State* state_; // process-resident while a window-procedure chain may refer to it
};
}
