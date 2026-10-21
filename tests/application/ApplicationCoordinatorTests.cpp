#include <hksr/framework/application/ApplicationCoordinator.hpp>
#include <hksr/presentation/PresentationBindings.hpp>
#include <hksr/framework/host/RuntimeServices.hpp>
#include <hksr/framework/tasks/TaskCatalog.hpp>
#include <stdexcept>

namespace {
void Check(bool value) {if(!value)throw std::runtime_error("application coordinator test failed");}
struct NoObjects final:hksr::api::ManagedObjects {
    hksr::api::ObjectResult NewStringUtf8(const char*) override {throw std::runtime_error("unexpected engine call");}
    hksr::api::ObjectResult AllocateObject(const char*,const char*) override {throw std::runtime_error("unexpected engine call");}
    hksr::api::ObjectResult ManagedType(const char*,const char*) override {throw std::runtime_error("unexpected engine call");}
};
}
void TestApplicationCoordinator() {
    using namespace hksr::framework;
    namespace api=hksr::api;
    ApplicationService service;
    NoObjects objects;api::PresentationBindings bindings;
    int info=0;
    const auto retain=+[](void*,int,void*)->int {throw std::runtime_error("unexpected retain call");};
    const auto target=+[](int,void*)->void* {throw std::runtime_error("unexpected target call");};
    const auto release=+[](int,void*) {throw std::runtime_error("unexpected free call");};
    bindings.references={{reinterpret_cast<void*>(retain),&info},{reinterpret_cast<void*>(target),&info},
        {reinterpret_cast<void*>(release),&info},{},{}};
    RuntimeServices runtime(objects,bindings);
    SceneCharacters characters(runtime.Sessions(false),SceneCharacters::Create{});
    unsigned reads=0,player_reads=0,observations=0,invalidations=0,diagnostics=0;
    CatalogReaders readers;
    readers.characters=[&]{
        ++reads;api::CharacterCatalog out;out.npcs.enumerated=true;
        out.npcs.rows={{77,"Npc/config","Npc/model"}};
        out.players.enumerated=true;out.players.rows={{11,0,0,0,"Player/config","Player.prefab"}};return out;
    };
    readers.document=[](auto kind,const std::string& path){
        api::ActionConfigInput out;out.kind=kind;out.path=path;out.loaded=true;return out;
    };
    ApplicationRuntimePorts ports;
    ports.player_placement=[&](const auto&){++player_reads;return CharacterPlacement{{10,20,30},{0,1,0,0}};};
    ports.observe_character=[&](auto){++observations;};
    ports.source_replaced=[&](auto){++invalidations;};
    ports.npc_actions=[](auto,const auto&){throw std::runtime_error("query failure");};
    bool camera=false,camera_failure=false;
    ports.camera_presentations=[&](bool enabled){if(camera_failure)throw std::runtime_error("view policy unavailable");camera=enabled;};
    ApplicationCoordinator coordinator(service,characters,std::make_unique<CatalogBrowser>(readers),
        {true,true,false},std::move(ports),[&](const auto&){++diagnostics;throw std::runtime_error("log failure");});
    const api::WorldFrame frame{};
    const SceneAddress scene{"saved-map",1};
    auto dispatch=[&](bool closing=false){
        auto batch=service.TakeCommands(closing);
        for(auto& pending:batch)coordinator.Dispatch(pending,scene,frame,closing);
        return service.TakeReplies();
    };
    auto snapshot=[&](bool closing=false){
        ApplicationSnapshot out;out.closing=closing;coordinator.Populate(out);return out;
    };
    Check(!snapshot().camera_presentations);
    service.Post(SetCameraPresentations{true});Check(dispatch().at(0).accepted && camera && snapshot().camera_presentations);
    camera_failure=true;service.Post(SetCameraPresentations{false});
    Check(!dispatch().at(0).accepted && snapshot().camera_presentations && snapshot().camera_presentations_revision==1);camera_failure=false;
    service.Post(SetCameraPresentations{false});Check(dispatch().at(0).accepted && !camera && !snapshot().camera_presentations && snapshot().camera_presentations_revision==2);
    service.Post(PrepareSource{BrowseNpc{77}});
    auto replies=dispatch();const auto source=replies.at(0).source_request;
    Check(source && replies[0].accepted && reads==0 && player_reads==0);
    coordinator.Catalog()->Advance();coordinator.Catalog()->Advance();
    Check(snapshot().catalog->Source(source)->phase==SourceRequestPhase::Ready);
    Check(snapshot().operations.entries.at(replies[0].serial).phase==OperationPhase::Returned);

    AddPreparedCharacter add;add.source_request=source;add.near_player=true;add.player_world_offset={-2,3,4};
    service.Post(add);replies=dispatch();const auto id=replies.at(0).character;
    Check(id && replies[0].accepted && player_reads==1 && observations==0);
    const auto state=characters.Status(id);
    Check(state.scene==scene.key && state.placement.position.x==8 && state.placement.position.y==23 &&
        state.placement.position.z==34 && state.placement.rotation.y==1);
    Check(!state.active && state.creations==0); // intent only; no engine session serviced
    Check(snapshot().operations.entries.at(replies[0].serial).phase==OperationPhase::Waiting);

    auto input=std::make_shared<VisualProgramInput>();
    input->tasks=std::make_shared<const TaskCatalog>(ParseTaskCatalog(R"({
      "format":"hksr.task-catalog","id":"test-preview","domain":"exploration",
      "visual":{"id":"preview-scene","source":"Player/config","scope":"preview","effects":{}},
      "sequences":{"idle":{"entry":{"op":"animation","request":"body","participant":"body","state":"StandBy"}}},
      "tasks":{"idle":{"entry":"idle"}}})"));
    input->program=input->tasks->visual;
    characters.ConfigurePrograms({{},{{"test-preview",input}}});
    service.Post(PublishTaskCatalogs{{{"test-preview",input}}});replies=dispatch();
    Check(replies[0].accepted && replies[0].task_catalog_revision==2 && !replies[0].operation);
    service.Post(PublishTaskCatalogs{{{"wrong-key",input}}});replies=dispatch();
    Check(!replies[0].accepted && !replies[0].task_catalog_revision);
    service.Post(PrepareSource{BrowsePlayer{{11,0,0},{}}});replies=dispatch();
    const auto player_source=replies[0].source_request;coordinator.Catalog()->Advance(0);
    Check(snapshot().catalog->Source(player_source)->phase==SourceRequestPhase::Ready);
    auto preview=add;preview.source_request=player_source;preview.near_player=false;preview.task_catalog="test-preview";
    service.Post(preview);replies=dispatch();
    Check(replies[0].accepted && characters.Plan(replies[0].character).visual_program->tasks==input->tasks);
    Check(characters.Status(replies[0].character).transient && !characters.Status(id).transient &&
        !characters.Plan(id).visual_program);
    const auto preview_id=replies[0].character;
    preview.task_catalog="missing";const auto count=characters.Ids().size();
    service.Post(preview);replies=dispatch();
    Check(!replies[0].accepted && characters.Ids().size()==count);
    service.Post(ReplacePreparedCharacter{preview_id,source,{},{}});replies=dispatch();
    Check(replies[0].accepted && characters.Status(preview_id).transient);
    service.Post(DeleteCharacter{preview_id});dispatch();
    invalidations=0; // the next assertions concern the original source replacement

    service.Post(ReadCharacterNpcActions{id}); // diagnostic failure cannot block ordinary control
    const auto old_action=service.Post(SelectCharacterAction{id,{ActionDomain::Exploration,NpcMotion{1}}});
    const auto new_action=service.Post(SelectCharacterAction{id,{ActionDomain::Exploration,NpcMotion{2}}});
    replies=dispatch();Check(replies.size()==3 && !replies[0].accepted && replies[0].error=="query failure");
    Check(replies[1].accepted && replies[2].accepted && observations==0);
    auto out=snapshot();
    Check(out.operations.entries.at(old_action).phase==OperationPhase::Superseded);
    Check(out.operations.entries.at(new_action).phase==OperationPhase::Waiting);
    service.Post(ObserveCharacter{id});dispatch();Check(observations==1);

    // Source replacement is application policy; runtime query caches are
    // invalidated only after the actual replacement, not on rejected input.
    service.Post(ReplacePreparedCharacter{id,999,{},{}});replies=dispatch();
    Check(!replies[0].accepted && invalidations==0);
    service.Post(ReplacePreparedCharacter{id,source,{},{}});replies=dispatch();
    Check(replies[0].accepted && invalidations==1 && !characters.Status(id).action);
    service.Post(ReleaseSource{source});dispatch();
    Check(!snapshot().catalog->Source(source) && characters.Plan(id).package.npc_id==77);
    auto battle_plan=characters.Plan(id);battle_plan.alternate=BattlePresentation{};
    battle_plan.alternate->initial_state={true,"OfficialIdle",{}};
    characters.ReplacePlan(id,std::move(battle_plan));
    service.Post(IdleCharacter{id});replies=dispatch();Check(replies[0].accepted);
    Check(characters.DesiredAction(id)->domain==ActionDomain::Alternate &&
        std::holds_alternative<BattleIdle>(characters.DesiredAction(id)->request));

    // Callbacks are outside the command batch and may fail or enqueue work;
    // neither failure nor logging exceptions replay already-applied intent.
    service.Post(RebuildCharacter{id},[&](const auto& reply){
        Check(reply.accepted);service.Post(EnableCharacter{id,false});throw std::runtime_error("receiver failure");
    });
    Check(dispatch().empty() && characters.Status(id).enabled);
    replies=dispatch();Check(replies.size()==1 && replies[0].accepted && !characters.Status(id).enabled);
    Check(diagnostics>0);
    service.Post(RebuildCharacter{id});characters.Shutdown();replies=dispatch(true);
    Check(!replies[0].accepted && snapshot(true).operations.entries.at(replies[0].serial).phase==OperationPhase::Cancelled);
    coordinator.CloseCatalog();coordinator.CloseCatalog();Check(!coordinator.Catalog() && characters.Closed());
    Check(!snapshot(true).catalog);

    // Missing optional directory/inspection support does not prevent a saved
    // source from being added. Only the creation ownership capability matters.
    ApplicationService direct;
    SceneCharacters saved(runtime.Sessions(false),SceneCharacters::Create{});
    ApplicationCoordinator plain(direct,saved,nullptr,{true,false,false},{});
    CharacterPlan plan;plan.package.npc_id=77;
    direct.Post(AddCharacter{plan,"saved",{}, {}});direct.Post(RequestCatalog{});
    auto batch=direct.TakeCommands(false);
    for(auto& pending:batch)plain.Dispatch(pending,{},frame,false);
    replies=direct.TakeReplies();Check(replies[0].accepted && !replies[1].accepted && saved.Ids().size()==1);
    saved.Shutdown();plain.CloseCatalog();Check(saved.Closed());
}
