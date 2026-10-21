#include <hksr/framework/application/ApplicationCoordinator.hpp>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace hksr::framework {
namespace {
std::optional<OperationKind> OperationFor(const CharacterCommand& command) {
    return std::visit([](const auto& value)->std::optional<OperationKind> {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,PrepareSource> || std::is_same_v<T,RetrySource>)return OperationKind::PrepareSource;
        else if constexpr(std::is_same_v<T,AddCharacter> || std::is_same_v<T,AddPreparedCharacter>)return OperationKind::Create;
        else if constexpr(std::is_same_v<T,ReplacePreparedCharacter>)return OperationKind::ReplaceSource;
        else if constexpr(std::is_same_v<T,RebuildCharacter>)return OperationKind::Rebuild;
        else if constexpr(std::is_same_v<T,DeleteCharacter>)return OperationKind::Delete;
        else if constexpr(std::is_same_v<T,EnableCharacter>)return value.enabled?OperationKind::Enable:OperationKind::Disable;
        else if constexpr(std::is_same_v<T,SelectCharacterAction> || std::is_same_v<T,SelectCatalogAction> || std::is_same_v<T,IdleCharacter>)return OperationKind::Action;
        else if constexpr(std::is_same_v<T,PlaceCharacter>)return OperationKind::Placement;
        else if constexpr(std::is_same_v<T,LookCharacter>)return OperationKind::Gaze;
        else return {};
    },command);
}
}
ApplicationCoordinator::ApplicationCoordinator(ApplicationService& application,SceneCharacters& characters,
    std::unique_ptr<CatalogBrowser> catalog,api::RuntimeCapabilities capabilities,ApplicationRuntimePorts runtime,
    std::function<void(const std::string&)> diagnostic)
    :application_(application),characters_(characters),catalog_(std::move(catalog)),capabilities_(capabilities),
     runtime_(std::move(runtime)),diagnostic_(std::move(diagnostic)) {}
void ApplicationCoordinator::Trace(const std::string& value) const noexcept {
    if(diagnostic_)try {diagnostic_(value);}catch(...) {}
}
CatalogBrowser& ApplicationCoordinator::RequireCatalog() {
    if(!catalog_)throw std::runtime_error("official catalog reader unavailable");
    return *catalog_;
}
void ApplicationCoordinator::CloseCatalog() {
    if(catalog_ && catalog_->CancelPending())catalog_.reset();
}
void ApplicationCoordinator::ConfigureLayouts(std::shared_ptr<LayoutIo> io) {
    if(!catalog_ || !capabilities_.entity_lifecycle)throw std::runtime_error("layout restoration requires catalog and entity lifecycle support");
    layouts_=std::make_unique<LayoutController>(characters_,*catalog_,std::move(io),runtime_.removed,capabilities_.model_release);
}
void ApplicationCoordinator::AdvanceLayouts(const std::optional<SceneAddress>& scene) {if(layouts_)layouts_->Advance(scene);}
void ApplicationCoordinator::CaptureLayouts(bool closing) {if(layouts_){if(closing)layouts_->Close();else layouts_->Capture();}}
void ApplicationCoordinator::Execute(CharacterCommand command,CommandReply& reply,const std::optional<SceneAddress>& scene,const api::WorldFrame& frame,
    std::shared_ptr<const ControlSubmission>& control) {
    std::visit([&](auto&& value) {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,LayoutCommand>) {
            if(!layouts_ || !scene)throw std::runtime_error("map layout storage is unavailable");
            if(!value.scene.empty() && value.scene!=scene->key)throw std::runtime_error("map changed before layout command was dispatched");
            layouts_->Execute(value,scene->key,reply.serial);
        }
        else if constexpr(std::is_same_v<T,RequestCatalog>) RequireCatalog().Request(value.refresh);
        else if constexpr(std::is_same_v<T,PrepareSource>) reply.source_request=RequireCatalog().Prepare(std::move(value.source));
        else if constexpr(std::is_same_v<T,RetrySource>) reply.source_request=RequireCatalog().RetrySource(value.request);
        else if constexpr(std::is_same_v<T,ReleaseSource>) RequireCatalog().ReleaseSource(value.request);
        else if constexpr(std::is_same_v<T,RefreshActionDocument>) RequireCatalog().RefreshDocument(value.source);
        else if constexpr(std::is_same_v<T,RefreshAbilitySource>) RequireCatalog().RefreshAbility(value.ability);
        else if constexpr(std::is_same_v<T,ReadCharacterNpcActions>) {reply.character=value.id;runtime_.npc_actions(value.id,frame);}
        else if constexpr(std::is_same_v<T,ObserveCharacter>) {
            if(value.id)(void)characters_.Plan(value.id);
            reply.character=value.id;runtime_.observe_character(value.id);
        }
        else if constexpr(std::is_same_v<T,SetCameraPresentations>) {
            if(!runtime_.camera_presentations)throw std::runtime_error("camera presentation visibility unavailable");
            runtime_.camera_presentations(value.enabled);camera_presentations_=value.enabled;++camera_presentations_revision_;
        }
        else if constexpr(std::is_same_v<T,ControlCharacterTask>) {
            reply.character=value.id;reply.task=characters_.ControlTask(value.id,value.creation,std::move(value.input),frame);
        }
        else if constexpr(std::is_same_v<T,ReadCharacterControllerActions>) {
            (void)characters_.Plan(value.id);reply.character=value.id;runtime_.controller_actions(value.id);
        }
        else if constexpr(std::is_same_v<T,BrowseCharacterActions>) {reply.character=value.id;RequireCatalog().InspectActions(value.id,characters_.Plan(value.id).action_sources);}
        else if constexpr(std::is_same_v<T,ReplacePreparedCharacter>) {
            auto plan=RequireCatalog().Plan(value.source_request,std::move(value.payload),std::move(value.npc_look_source));
            if(!capabilities_.entity_lifecycle) throw std::runtime_error("NPC replacement requires its actual entity disposal observer");
            if((plan.package.appearance || plan.payload) && !capabilities_.model_release)
                throw std::runtime_error("private model inputs require the pre-release restoration observer");
            characters_.ReplacePlan(value.id,std::move(plan));reply.character=value.id;
            if(runtime_.source_replaced)runtime_.source_replaced(value.id);
            RequireCatalog().InspectActions(value.id,characters_.Plan(value.id).action_sources);
        }
        else if constexpr(std::is_same_v<T,AddPreparedCharacter>) {
            if(value.near_player) {
                const auto player=runtime_.player_placement(frame);
                value.placement={{player.position.x+value.player_world_offset.x,
                    player.position.y+value.player_world_offset.y,player.position.z+value.player_world_offset.z},player.rotation,value.placement.snap_to_ground};
            }
            auto plan=RequireCatalog().Plan(value.source_request,std::move(value.payload),std::move(value.npc_look_source));
            if(value.transient)plan.persistence=CharacterPersistence::Session;
            if(!value.task_catalog.empty())characters_.ConfigureTaskPreview(plan,value.task_catalog);
            Execute(AddCharacter{std::move(plan),std::move(value.scene),std::move(value.action),value.placement},reply,scene,frame,control);
            RequireCatalog().InspectActions(reply.character,characters_.Plan(reply.character).action_sources);
        } else if constexpr(std::is_same_v<T,AddCharacter>) {
            if(!capabilities_.entity_lifecycle) throw std::runtime_error("NPC creation requires its actual entity disposal observer");
            if((value.plan.package.appearance || value.plan.payload) && !capabilities_.model_release)
                throw std::runtime_error("private model inputs require the pre-release restoration observer");
            if(value.scene.empty()) {
                if(!scene) throw std::runtime_error("current map identity unavailable; supply a saved scene or retry after it is observed");
                value.scene=scene->key;
            }
            reply.character=characters_.Add(std::move(value.scene),std::move(value.plan),std::move(value.action),value.placement);
            if(const auto action=characters_.Status(reply.character).action)reply.action_intent=action->id;
        } else {
            reply.character=value.id;
            if constexpr(std::is_same_v<T,SelectCharacterAction>) reply.action_intent=characters_.Select(value.id,std::move(value.action));
            else if constexpr(std::is_same_v<T,SelectCatalogAction>) {
                RequireCatalog().InspectActions(value.id,characters_.Plan(value.id).action_sources);
                reply.action_intent=characters_.Select(value.id,RequireCatalog().ResolveAction(value.id,value.action));
            }
            else if constexpr(std::is_same_v<T,EnableCharacter>) characters_.SetEnabled(value.id,value.enabled);
            else if constexpr(std::is_same_v<T,IdleCharacter>) {
                const auto idle=characters_.Status(value.id).battle_idle_state;
                if(idle.empty())
                    throw std::runtime_error("official battle idle is unavailable for this source");
                reply.action_intent=characters_.Select(value.id,{ActionDomain::Alternate,BattleIdle{}});
            }
            else if constexpr(std::is_same_v<T,DeleteCharacter>) {
                characters_.Delete(value.id);
                if(catalog_)catalog_->ForgetCharacter(value.id);
                if(runtime_.removed)runtime_.removed(value.id);
            }
            else if constexpr(std::is_same_v<T,PlaceCharacter>) control=characters_.Place(value.id,value.pose,value.relative);
            else if constexpr(std::is_same_v<T,LookCharacter>) {
                if(value.intent.mode==LookMode::Instance &&
                    characters_.Status(value.id).scene!=characters_.Status(value.intent.target).scene)
                    throw std::runtime_error("gaze target must belong to the same map");
                control=characters_.Look(value.id,value.intent);
            }
            else characters_.Rebuild(value.id);
        }
    },std::move(command));
    reply.accepted=true; // intent accepted, NOT actor created or animation seen
}

void ApplicationCoordinator::ObserveOperations(bool closing) {
    std::vector<SceneCharacterStatus> characters;
    std::set<std::uint64_t> ids;
    for(const auto& item:operations_.Snapshot().entries) {
        const auto& operation=item.second;
        if(!operation.Terminal() && operation.character && operation.kind!=OperationKind::Placement && operation.kind!=OperationKind::Gaze)
            ids.insert(operation.character);
    }
    for(auto id:ids)if(characters_.Contains(id))characters.push_back(characters_.Status(id));
    const auto catalog=catalog_?catalog_->Snapshot():nullptr;
    operations_.Observe(characters,catalog.get(),closing);
}

void ApplicationCoordinator::Dispatch(ApplicationService::PendingCommand& pending,const std::optional<SceneAddress>& scene,
    const api::WorldFrame& frame,bool closing) {
    // Preserve receipts before a later command replaces the latest intent.
    ObserveOperations(closing);
    CommandReply reply;reply.serial=pending.serial;
    const auto kind=pending.command.index();
    const auto operation_kind=OperationFor(pending.command);
    std::shared_ptr<const ControlSubmission> control;
    try {
        if(closing) throw std::runtime_error("command cancelled by host shutdown");
        // A pending replacement and an edit of that SAME layout cannot both
        // own its contents. Discovery/diagnostics and other maps remain usable.
        if(layouts_ && operation_kind && *operation_kind!=OperationKind::PrepareSource) {
            std::string target=scene?scene->key:std::string{};
            std::visit([&](const auto& c){
                using T=std::decay_t<decltype(c)>;
                if constexpr(std::is_same_v<T,AddCharacter> || std::is_same_v<T,AddPreparedCharacter>) {if(!c.scene.empty())target=c.scene;}
                else if constexpr(std::is_same_v<T,SelectCharacterAction> || std::is_same_v<T,SelectCatalogAction> ||
                    std::is_same_v<T,EnableCharacter> || std::is_same_v<T,RebuildCharacter> || std::is_same_v<T,DeleteCharacter> ||
                    std::is_same_v<T,IdleCharacter> || std::is_same_v<T,ReplacePreparedCharacter> || std::is_same_v<T,PlaceCharacter> || std::is_same_v<T,LookCharacter>)
                    target=characters_.Status(c.id).scene;
            },pending.command);
            if(layouts_->Replacing(target))throw std::runtime_error("this map is restoring its layout; edit after replacement completes");
        }
        Execute(std::move(pending.command),reply,scene,frame,control);
    } catch(const std::exception& error) {reply.error=error.what();}
    catch(...) {reply.error="unknown framework command failure";}
    if(operation_kind) {
        OperationStatus operation;operation.id=reply.serial;operation.kind=*operation_kind;
        operation.character=reply.character;operation.source_request=reply.source_request;operation.action_intent=reply.action_intent;
        if(reply.accepted && reply.character)operation.revision=characters_.Status(reply.character).revision;
        if(!reply.accepted) {
            operation.phase=closing?OperationPhase::Cancelled:OperationPhase::Failed;operation.detail=reply.error;
        }
        operations_.Add(std::move(operation),std::move(control));reply.operation=reply.serial;
    }
    Trace("command="+std::to_string(reply.serial)+" kind="+std::to_string(kind)+" accepted="+
        std::to_string(reply.accepted)+" character="+std::to_string(reply.character)+" error="+reply.error);
    try {application_.Deliver(pending,std::move(reply));}
    catch(...) {Trace("command reply receiver failed; command is not retried");}
}
void ApplicationCoordinator::Populate(ApplicationSnapshot& out) {
    out.camera_presentations=camera_presentations_;
    out.camera_presentations_revision=camera_presentations_revision_;
    out.catalog=catalog_?catalog_->Snapshot():nullptr;
    out.characters.clear();
    for(auto id:characters_.Ids())out.characters.push_back(characters_.Status(id));
    operations_.Observe(out.characters,out.catalog.get(),out.closing);
    out.operations=operations_.Snapshot();
    if(layouts_)out.layouts=layouts_->Snapshot();
}
}
