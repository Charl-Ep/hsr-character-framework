#pragma once
#include <hksr/presentation/events/AnimationEventPolicy.hpp>
#include "RuntimeServices.hpp"
#include "../character/SceneIdentity.hpp"
#include "../catalog/CatalogBrowser.hpp"
#include "../application/ApplicationCoordinator.hpp"
#include <hksr/runtime/RuntimeHost.hpp>
#include <hksr/control/MouseMode.hpp>
#include <hksr/animation/AnimatorCatalog.hpp>
#include <atomic>
#include <chrono>

namespace hksr::framework {
// Host client owns all frame consumers. UI exchanges value commands/snapshots
// only; no game pointer or callback gets queued. No file IO on world updates.
class FrameworkHost final:public api::RuntimeClient {
public:
    explicit FrameworkHost(std::function<void(const std::string&)> diagnostic={}):diagnostic_(std::move(diagnostic)){}
    ApplicationService& Application() {return application_;}
    const ApplicationService& Application() const {return application_;}
    void SetMenuMouse(bool requested) {menu_mouse_.store(requested);}
    void SetOperationTracing(bool enabled) {operation_tracing_.store(enabled);}
    // Set once before registering with the SDK runtime. Pump this mailbox on
    // the bootstrap worker; the host only exchanges values with it.
    void SetLayoutIo(std::shared_ptr<LayoutIo> io) {layout_io_=std::move(io);}
    void SetPresentationProfiles(std::shared_ptr<const PresentationProfiles> profiles) {profiles_=std::move(profiles);}
    // Authored inputs, loaded off the engine thread before Prepare. An empty
    // registry does not install the optional program observation machinery.
    void SetVisualPrograms(std::map<std::string,std::shared_ptr<const VisualProgramInput>> programs) {programs_=std::move(programs);}
    void SetObservedCharacter(std::uint64_t id) {observed_character_.store(id);}
    // Optional synchronous observer of the already-validated selected model.
    // Borrowed references are valid only during this callback; never queued to UI.
    using ModelObserver=std::function<void(std::uint64_t,std::uint64_t,ActionDomain,api::GameEntity*,void*,void*)>;
    void SetModelObserver(ModelObserver observer) {model_observer_=std::move(observer);}
    void Prepare(api::RuntimeMetadata& metadata,api::RuntimeCapabilities capabilities) override;
    bool Frame(const api::WorldFrame& frame,bool stop_requested) override;
    // Same frame consumer can be composed with explicit local services. The
    // caller owns objects until closure; no bootstrap or game acceptance implied.
    void Prepare(api::ManagedObjects& objects,api::PresentationBindings bindings,
        api::RuntimeCapabilities capabilities,SceneCharacters::Create create,CatalogReaders catalog={});
private:
    struct PerformanceRow {std::uint64_t calls=0,total_us=0,max_us=0;};
    std::map<std::string,PerformanceRow> performance_;
    std::chrono::steady_clock::time_point performance_since_=std::chrono::steady_clock::now();
    void RecordPerformance(const std::string& stage,std::uint64_t elapsed) noexcept;
    void PublishPerformance();
    template<class F> auto Measure(const std::string& stage,F&& action)->decltype(action()) {
        struct End {
            FrameworkHost& host;const std::string& stage;std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
            ~End(){host.RecordPerformance(stage,static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count()));}
        } end{*this,stage};
        return action();
    }
    void Trace(const std::string& value) const noexcept;
    std::function<void(const std::string&)> diagnostic_;
    ModelObserver model_observer_;
    std::map<std::uint64_t,std::string> last_character_trace_;
    std::shared_ptr<const api::CharacterCatalog> traced_catalog_;
    std::map<std::uint64_t,std::string> source_traces_;
    void Assemble(api::ManagedObjects& objects,std::unique_ptr<RuntimeServices>,api::RuntimeCapabilities capabilities);
    void PrepareTypes();
    void PrepareApplication(std::unique_ptr<CatalogBrowser>);
    void Publish(const std::optional<SceneAddress>& scene);
    void ReadNpcActions(std::uint64_t id,const api::WorldFrame& frame);
    void ReadControllerActions(const api::WorldFrame& frame);
    CatalogBrowser& Browser();
    ApplicationService application_;
    std::shared_ptr<LayoutIo> layout_io_;
    std::shared_ptr<const PresentationProfiles> profiles_;
    std::map<std::string,std::shared_ptr<const VisualProgramInput>> programs_;
    std::vector<std::string> binding_errors_;
    bool closing_=false,types_attempted_=false;
    bool effects_closed_=false;
    api::RuntimeCapabilities capabilities_;
    api::ManagedObjects* objects_=nullptr;
    std::unique_ptr<RuntimeServices> services_;
    std::unique_ptr<api::AnimationEventPolicy> event_policy_;
    std::unique_ptr<SceneIdentity> identity_;
    std::unique_ptr<api::ManagedRoots> roots_;
    api::AnimationInputTypes types_;
    std::vector<api::ManagedRootId> type_roots_;
    std::unique_ptr<SceneCharacters> characters_;
    std::unique_ptr<api::OfficialCatalog> official_catalog_;
    std::unique_ptr<ApplicationCoordinator> coordinator_;
    std::unique_ptr<api::MouseModeLease> mouse_;
    std::atomic<bool> menu_mouse_{false};
    std::atomic<bool> operation_tracing_{false};
    std::atomic<std::uint64_t> observed_character_{0};
    std::string mouse_trace_;
    std::map<std::uint64_t,std::shared_ptr<const CharacterNpcActions>> npc_actions_;
    std::unique_ptr<api::AnimatorCatalog> animator_catalog_;
    api::RuntimeMetadata* catalog_metadata_=nullptr;
    struct ControllerStamp {std::uint64_t creation;ActionDomain domain;void* animator;void* controller;};
    std::map<std::uint64_t,ControllerStamp> controller_stamps_;
    std::map<std::uint64_t,std::shared_ptr<const CharacterControllerActions>> controller_actions_;
    std::string controller_catalog_error_;
    std::optional<std::uint64_t> controller_query_;
    std::map<std::uint64_t,std::string> controller_read_errors_;
};
}
