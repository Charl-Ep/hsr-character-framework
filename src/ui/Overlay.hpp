#pragma once
#include <filesystem>
#include <functional>
#include <string>
struct IDXGISwapChain;
namespace hksr::framework {
class ApplicationService;
}
namespace hksr::framework::ui {
class Overlay {
  public:
    Overlay();
    // No game calls. Draws only the menu, using the borrowed DXGI swap chain.
    // Errors are reported independently; they don't stop character lifecycle.
    // The application and mouse receiver outlive Draw/Stop; the receiver only
    // publishes a request and must not call the game from a render/window thread.
    void Draw(IDXGISwapChain* chain, ApplicationService&, std::function<void(bool)> request_mouse);
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
