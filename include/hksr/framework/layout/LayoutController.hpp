#pragma once
#include "LayoutState.hpp"
#include "LayoutIo.hpp"
#include "../catalog/CatalogBrowser.hpp"
#include <chrono>

namespace hksr::framework {
// Official-frame policy. IO runs through a value mailbox; source preparation,
// ownership drainage and reconstruction stay on their existing official paths.
class LayoutController {
public:
    LayoutController(SceneCharacters&,CatalogBrowser&,std::shared_ptr<LayoutIo>,
        std::function<void(std::uint64_t)> removed={},bool private_models=true);
    void Advance(const std::optional<SceneAddress>&);
    void Capture(bool force=false); // call after instance service, BEFORE Shutdown disables entries
    void Execute(const LayoutCommand&,const std::string& scene,std::uint64_t command);
    bool Replacing(const std::string&) const;
    LayoutSnapshot Snapshot() const;
    void Close();
private:
    enum class Phase {Reading,Preparing,Removing,Applying,Ready,Failed};
    struct Map {
        MapLayoutStatus status;
        Phase phase=Phase::Reading;
        std::uint64_t io=0;
        std::optional<SceneLayout> incoming;
        std::shared_ptr<const SceneLayout> captured;
        std::vector<std::uint64_t> sources,old,added;
        std::vector<CharacterPlan> plans;
        std::vector<std::optional<PlacedAction>> actions;
    };
    void Begin(Map&,SceneLayout);
    void Prepare(Map&);
    void Install(Map&);
    void ObserveApplied(Map&);
    void Release(Map&);
    void Fail(Map&,std::string);
    void CaptureMap(const std::string&,Map&);
    SceneCharacters& characters_;
    CatalogBrowser& catalog_;
    std::shared_ptr<LayoutIo> io_;
    std::function<void(std::uint64_t)> removed_;
    bool private_models_=true,closed_=false;
    std::map<std::string,Map> maps_;
    std::string current_;
    std::chrono::steady_clock::time_point captured_at_{};
    mutable LayoutSnapshot published_;
};
}
