#pragma once
#include "OperationStatus.hpp"
#include "../catalog/CatalogSnapshot.hpp"
#include "../character/CharacterPlan.hpp"
#include "ApplicationEvents.hpp"
#include "../layout/LayoutState.hpp"
#include <hksr/animation/AnimatorCatalog.hpp>
#include <deque>
#include <functional>
#include <mutex>
#include <variant>
#include <condition_variable>
#include <chrono>

namespace hksr::framework {
struct AddCharacter { CharacterPlan plan; std::string scene; std::optional<PlacedAction> action; CharacterPlacement placement; };
struct SelectCharacterAction {std::uint64_t id;PlacedAction action;};
struct SelectCatalogAction {std::uint64_t id;CatalogActionSelection action;};
struct EnableCharacter {std::uint64_t id;bool enabled;};
struct RebuildCharacter {std::uint64_t id;};
struct DeleteCharacter {std::uint64_t id;};
struct IdleCharacter {std::uint64_t id;};
struct RequestCatalog {bool refresh=false;};
struct PrepareSource {BrowseSource source;};
struct RetrySource {std::uint64_t request;};
struct ReleaseSource {std::uint64_t request;};
struct RefreshActionDocument {ActionSourceKey source;};
struct RefreshAbilitySource {std::string ability;};
struct AddPreparedCharacter {
    std::uint64_t source_request=0;
    std::string scene;
    std::optional<PlacedAction> action;
    CharacterPlacement placement;
    std::shared_ptr<const ModelAssetInput> payload;
    std::string npc_look_source;
    bool near_player=false;
    api::Position3 player_world_offset{2,0,0};
};
struct ReadCharacterNpcActions {std::uint64_t id;};
struct ReplacePreparedCharacter {
    std::uint64_t id=0,source_request=0;
    std::shared_ptr<const ModelAssetInput> payload;
    std::string npc_look_source;
};
struct BrowseCharacterActions {std::uint64_t id;};
struct PlaceCharacter {std::uint64_t id;CharacterPlacement pose;bool relative=false;};
struct LookCharacter {std::uint64_t id;LookIntent intent;};
struct ReadCharacterControllerActions {std::uint64_t id;}; // explicit development query
struct ObserveCharacter {std::uint64_t id;}; // development observation only; zero clears
using CharacterCommand=std::variant<AddCharacter,SelectCharacterAction,EnableCharacter,RebuildCharacter,
    RequestCatalog,PrepareSource,RetrySource,ReleaseSource,RefreshActionDocument,RefreshAbilitySource,AddPreparedCharacter,ReplacePreparedCharacter,ReadCharacterNpcActions,BrowseCharacterActions,PlaceCharacter,LookCharacter,ReadCharacterControllerActions,SelectCatalogAction,ObserveCharacter,DeleteCharacter,IdleCharacter,LayoutCommand>;
struct CommandReply {std::uint64_t serial=0,character=0;bool accepted=false;std::string error;std::uint64_t source_request=0,action_intent=0,operation=0;};
struct CharacterNpcActions {std::uint64_t creation=0;api::NpcMotionSources source;};
struct CharacterControllerActions {
    std::uint64_t creation=0;
    std::map<ActionDomain,api::ControllerCatalog> domains;
};
struct ApplicationSnapshot {
    bool prepared=false,closing=false,closed=false;
    std::optional<SceneAddress> scene;
    std::vector<SceneCharacterStatus> characters;
    std::vector<std::string> binding_errors;
    std::string map_error;
    std::shared_ptr<const CatalogSnapshot> catalog;
    std::map<std::uint64_t,std::shared_ptr<const CharacterNpcActions>> npc_actions;
    std::map<std::uint64_t,std::shared_ptr<const CharacterControllerActions>> controller_actions;
    std::string controller_catalog_error;
    OperationSnapshot operations;
    LayoutSnapshot layouts;
};
struct ApplicationUpdates {
    ApplicationChangeLog changes;
    ApplicationSnapshot snapshot;
};

// Shared value boundary for UI, development control and future HTTP. It owns
// no engine objects, metadata, hooks, renderer, network or game-thread runner.
// Producers may Post/read concurrently. The coordinator drains one batch on
// the official frame and publishes copied values; callbacks execute unlocked.
class ApplicationService {
public:
    explicit ApplicationService(std::size_t retained_events=256):events_(retained_events){}
    using ReplySink=std::function<void(const CommandReply&)>;
    struct PendingCommand {std::uint64_t serial;CharacterCommand command;ReplySink reply;};
    std::uint64_t Post(CharacterCommand,ReplySink reply={});
    std::vector<CommandReply> TakeReplies(); // menu-owned replies only
    ApplicationSnapshot Snapshot() const;
    // One lock pairs a snapshot with its cursor. Initial/out-of-retention
    // cursors request a full reset. Events invalidate resources in this latest
    // snapshot; they do not promise every intermediate animation state.
    ApplicationUpdates ReadUpdates(std::optional<ApplicationCursor> after={}) const;
    // Transport workers only; never call from the official-frame coordinator.
    ApplicationUpdates WaitUpdates(ApplicationCursor after,std::chrono::milliseconds timeout) const;
    // Lightweight timed wait for streaming transports checking their own stop
    // state. Does not copy unchanged actors/catalogs on every timeout. After a
    // wake, ReadUpdates(after) atomically obtains the latest values and cursor.
    bool WaitForChange(const ApplicationCursor& after,std::chrono::milliseconds timeout) const;

    // Coordinator side. Closing is irreversible; it rejects future posts but
    // returns already queued work for explicit cancellation replies.
    std::deque<PendingCommand> TakeCommands(bool closing);
    void Publish(ApplicationSnapshot);
    void Deliver(const PendingCommand&,CommandReply); // callback exceptions reach coordinator
private:
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    std::uint64_t next_=0;
    bool closing_=false;
    std::deque<PendingCommand> commands_;
    std::vector<CommandReply> replies_;
    ApplicationSnapshot snapshot_;
    ApplicationEvents events_;
};
}
