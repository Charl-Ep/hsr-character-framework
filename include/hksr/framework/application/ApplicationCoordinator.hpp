#pragma once
#include "ApplicationService.hpp"
#include "OperationHistory.hpp"
#include "../layout/LayoutController.hpp"
#include "../character/SceneCharacters.hpp"
#include <hksr/runtime/RuntimeHost.hpp>

namespace hksr::framework {
// Runtime-only queries remain with the host that owns their managed roots.
// These synchronous ports are never queued or published to external clients.
struct ApplicationRuntimePorts {
    std::function<CharacterPlacement(const api::WorldFrame&)> player_placement;
    std::function<void(std::uint64_t,const api::WorldFrame&)> npc_actions;
    std::function<void(std::uint64_t)> controller_actions;
    std::function<void(std::uint64_t)> observe_character;
    std::function<void(std::uint64_t)> source_replaced;
    std::function<void(std::uint64_t)> removed;
};

// Official-thread application logic. Owns source preparation and operation
// history, but not engine preparation, hooks, managed roots or scene sessions.
// The service and character registry must outlive this coordinator. Explicit
// CloseCatalog drains pending readers; destruction does not call the engine.
class ApplicationCoordinator {
public:
    ApplicationCoordinator(ApplicationService&,SceneCharacters&,std::unique_ptr<CatalogBrowser>,
        api::RuntimeCapabilities,ApplicationRuntimePorts,
        std::function<void(const std::string&)> diagnostic={});
    void Dispatch(ApplicationService::PendingCommand&,const std::optional<SceneAddress>&,
        const api::WorldFrame&,bool closing);
    void Populate(ApplicationSnapshot&); // character/catalog/operation values only
    CatalogBrowser* Catalog() const {return catalog_.get();}
    CatalogBrowser& RequireCatalog();
    void CloseCatalog();
    void ConfigureLayouts(std::shared_ptr<LayoutIo>);
    void AdvanceLayouts(const std::optional<SceneAddress>&);
    void CaptureLayouts(bool closing=false);
private:
    void ObserveOperations(bool closing);
    void Execute(CharacterCommand,CommandReply&,const std::optional<SceneAddress>&,
        const api::WorldFrame&,std::shared_ptr<const ControlSubmission>&);
    void Trace(const std::string&) const noexcept;
    ApplicationService& application_;
    SceneCharacters& characters_;
    std::unique_ptr<CatalogBrowser> catalog_;
    api::RuntimeCapabilities capabilities_;
    ApplicationRuntimePorts runtime_;
    std::function<void(const std::string&)> diagnostic_;
    OperationHistory operations_;
    std::unique_ptr<LayoutController> layouts_;
};
}
