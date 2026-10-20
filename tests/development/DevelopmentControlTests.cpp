#include "../../tools/DevelopmentControl.hpp"
#include "../../tools/DevelopmentProtocol.hpp"
#include <stdexcept>
#include <thread>

using namespace hksr::framework;
using namespace hksr::research;
namespace {
void Check(bool value){if(!value)throw std::runtime_error("development control test failed");}
}
void TestDevelopmentControl() {
    using Json=nlohmann::json;
    struct Pending {CharacterCommand command;ApplicationService::ReplySink reply;};
    std::vector<Pending> pending;
    ApplicationSnapshot snapshot;snapshot.prepared=true;
    SceneCharacterStatus actor;actor.id=7;actor.active=true;actor.creations=3;actor.placement.position={1,2,3};
    actor.outfit.emplace();actor.outfit->avatar=8005;actor.outfit->gender=1;actor.outfit->base_type=4;actor.outfit->applicable=actor.outfit->applied=true;
    actor.action=ActionIntentStatus{17,3,ActionIntentPhase::Returned,ActionOutcome::Returned,"official call returned"};
    snapshot.characters.push_back(actor);
    OperationStatus operation;operation.id=91;operation.character=7;operation.kind=OperationKind::Disable;operation.phase=OperationPhase::Running;
    snapshot.operations.sequence=4;snapshot.operations.entries.emplace(91,operation);
    operation.id=92;operation.kind=OperationKind::Placement;snapshot.operations.entries.emplace(92,operation);
    operation.id=93;operation.kind=OperationKind::Gaze;snapshot.operations.entries.emplace(93,operation);
    DevelopmentControl control(42,{
        [&](auto command,auto reply){pending.push_back({std::move(command),std::move(reply)});return static_cast<std::uint64_t>(pending.size());},
        [&]{return snapshot;}});
    auto drain=[&]{std::vector<Json> rows;for(const auto& text:control.Drain())rows.push_back(Json::parse(text));return rows;};
    control.Submit("");control.Submit(" \t\r\n");
    Check(drain().empty() && pending.empty());
    control.Submit("{");
    const auto malformed=drain();
    Check(malformed.size()==1 && malformed[0]["event"]=="request_error" && pending.empty());
    Json request={{"format","hksr.development-commands"},{"process_id",42},{"request_id","first"},{"commands",Json::array({
        {{"op","select"},{"character",7}},
        {{"op","state"},{"character",7},{"domain","battle"},{"name","StandBy"}},
        {{"op","snapshot"}}})}};
    control.Submit(request.dump());Check(pending.size()==2);
    Check(std::get<BrowseCharacterActions>(pending[0].command).id==7);
    const auto& action=std::get<SelectCharacterAction>(pending[1].command);
    Check(action.id==7 && action.action.domain==ActionDomain::Alternate && std::get<AnimatorState>(action.action.request).name=="StandBy");
    auto rows=drain();Check(rows.size()==3 && rows[0]["event"]=="queued" && rows[2]["snapshot"]["characters"][0]["creations"]==3);
    Check(rows[2]["snapshot"]["characters"][0]["outfit"]["avatar"]==8005 && rows[2]["snapshot"]["characters"][0]["outfit"]["base_type"]==4);
    Check(rows[2]["snapshot"]["characters"][0]["action_intent"]["id"]==17 && rows[2]["snapshot"]["characters"][0]["action_intent"]["phase"]=="returned");
    Check(rows[2]["snapshot"]["operations"]["sequence"]==4 && rows[2]["snapshot"]["operations"]["entries"]["91"]["phase"]=="running");
    Check(rows[2]["snapshot"]["operations"]["entries"]["92"]["kind"]=="placement" && rows[2]["snapshot"]["operations"]["entries"]["93"]["kind"]=="gaze");
    std::thread frame([&]{pending[0].reply({1,7,true,"",0,17});pending[1].reply({2,7,false,"missing state",0});});frame.join();
    rows=drain();Check(rows.size()==2 && rows[0]["accepted"]==true && rows[1]["error"]=="missing state");
    Check(rows[0]["action_intent"]==17);
    control.Submit(request.dump());Check(pending.size()==2 && drain()[0]["event"]=="duplicate_ignored");
    request["request_id"]="wrong-process";request["process_id"]=43;control.Submit(request.dump());
    Check(pending.size()==2 && drain()[0]["event"]=="request_error");
    request["process_id"]=42;request["request_id"]="independent-errors";
    request["commands"]=Json::array({{{"op","state"},{"character",7},{"domain","wrong"},{"name","StandBy"}},
        {{"op","enable"},{"character",7},{"enabled",false}}});
    control.Submit(request.dump());rows=drain();Check(rows.size()==2 && rows[0]["event"]=="command_error" && pending.size()==3);
    Check(!std::get<EnableCharacter>(pending.back().command).enabled);
    pending.back().reply({3,7,true,"",0});drain();
    control.Submit("{broken");Check(drain()[0]["event"]=="request_error" && pending.size()==3);
    Check(std::get<BrowsePlayer>(std::get<PrepareSource>(ParseDevelopmentCommand({{"op","source_player"},{"player_id",1217},{"npc_id",77}})).source).key.id==1217);
    const auto create=std::get<AddPreparedCharacter>(ParseDevelopmentCommand({{"op","create_prepared"},{"source_request",12}}));
    Check(create.source_request==12 && create.near_player);
    Check(!create.transient && create.task_catalog.empty());
    const auto preview_creation=std::get<AddPreparedCharacter>(ParseDevelopmentCommand({{"op","create_prepared"},
        {"source_request",12},{"task_catalog","fixture"}}));
    Check(preview_creation.transient && preview_creation.task_catalog=="fixture");
    Check(std::get<AddPreparedCharacter>(ParseDevelopmentCommand({{"op","create_prepared"},
        {"source_request",12},{"transient",true}})).transient);
    bool invalid_preview=false;
    try {ParseDevelopmentCommand({{"op","create_prepared"},{"source_request",12},{"task_catalog",""}});}
    catch(const std::invalid_argument&){invalid_preview=true;}Check(invalid_preview);
    const auto placed=std::get<AddPreparedCharacter>(ParseDevelopmentCommand({{"op","create_prepared"},{"source_request",12},
        {"near_player",false},{"position",{{"x",1},{"y",2},{"z",3}}},{"rotation",{{"x",0},{"y",0},{"z",0},{"w",1}}}}));
    Check(!placed.near_player && placed.placement.position.z==3 && placed.placement.rotation.w==1);
    const auto replace=std::get<ReplacePreparedCharacter>(ParseDevelopmentCommand({{"op","replace_prepared"},{"character",7},{"source_request",12}}));
    Check(replace.id==7 && replace.source_request==12 && !replace.payload);
    Check(std::get<RetrySource>(ParseDevelopmentCommand({{"op","retry_source"},{"source_request",12}})).request==12);
    Check(std::get<ReleaseSource>(ParseDevelopmentCommand({{"op","release_source"},{"source_request",12}})).request==12);
    const auto layout=std::get<LayoutCommand>(ParseDevelopmentCommand({{"op","layout"},{"operation","load"},{"name","test"},{"scene","mapA"}}));
    Check(layout.operation==LayoutOperation::Load && layout.name=="test" && layout.scene=="mapA");
    Check(std::get<ReadCharacterControllerActions>(ParseDevelopmentCommand({{"op","controllers"},{"character",7}})).id==7);
    Check(std::get<ObserveCharacter>(ParseDevelopmentCommand({{"op","observe"},{"character",7}})).id==7);
    Check(std::get<ObserveCharacter>(ParseDevelopmentCommand({{"op","observe"},{"character",0}})).id==0);
    const auto free_state=std::get<SelectCharacterAction>(ParseDevelopmentCommand({{"op","free_state"},{"character",7},{"domain","battle"},{"name","Skill02_V1"}}));
    Check(free_state.action.domain==ActionDomain::Alternate && std::get<FreeStyleState>(free_state.action.request).name=="Skill02_V1");
    const auto program=std::get<SelectCharacterAction>(ParseDevelopmentCommand({{"op","program_action"},{"character",7},
        {"program","fixture"},{"action","first"},{"choices",{{"branch","a"}}}}));
    Check(program.action.domain==ActionDomain::Alternate && std::get<ProgramAction>(program.action.request).program=="fixture" &&
        std::get<ProgramAction>(program.action.request).action=="first" && std::get<ProgramAction>(program.action.request).choices.at("branch")=="a");
    Check(std::get<PlaceCharacter>(ParseDevelopmentCommand({{"op","place"},{"character",7},{"relative",true},
        {"position",{{"x",1},{"y",0},{"z",0}}},{"rotation",{{"x",0},{"y",0},{"z",0},{"w",1}}}})).relative);
    Check(std::get<PlaceCharacter>(ParseDevelopmentCommand({{"op","place"},{"character",7},{"snap_to_ground",false},
        {"position",{{"x",0},{"y",1},{"z",0}}},{"rotation",{{"x",0},{"y",0},{"z",0},{"w",1}}}})).pose.snap_to_ground==false);
    auto catalog=std::make_shared<CatalogSnapshot>();auto sources=std::make_shared<hksr::api::CharacterCatalog>();
    sources->players.rows.push_back({17,23,0,1,"character.json","character.prefab"});
    catalog->rows=sources;auto preparation=std::make_shared<SourceRequestStatus>();preparation->id=12;
    preparation->phase=SourceRequestPhase::Unresolved;preparation->input=BrowsePlayer{{17,0,1},{}};
    preparation->result.finished=true;preparation->result.foundation.candidates={31,32};catalog->sources[12]=preparation;snapshot.catalog=catalog;
    request["request_id"]="catalog-snapshot";request["commands"]=Json::array({{{"op","snapshot"},{"catalog",true}}});
    control.Submit(request.dump());rows=drain();
    Check(rows[0]["snapshot"]["catalog"]["rows"]["players"][0]["id"]==17 && rows[0]["snapshot"]["catalog"]["rows"]["players"][0]["avatar"]==23);
    Check(rows[0]["snapshot"]["catalog"]["sources"]["12"]["npc_candidates"].size()==2 && rows[0]["snapshot"]["catalog"]["sources"]["12"]["phase"]=="unresolved");
    auto actor_catalog=std::make_shared<CharacterActionCatalog>();actor_catalog->revision=18;
    catalog->characters[7]=actor_catalog;
    ActionProposal proposal;proposal.domain=ActionDomain::Alternate;proposal.state="AnyState";
    proposal.retirements.push_back({"AnyState",{}});actor_catalog->actions.proposals.push_back(proposal);
    request["request_id"]="configured";request["commands"]=Json::array({{{"op","configured_state"},{"character",7},{"revision",18},{"domain","battle"},{"name","AnyState"}}});
    control.Submit(request.dump());rows=drain();Check(rows[0]["event"]=="queued");
    const auto& configured=std::get<SelectCatalogAction>(pending.back().command);
    Check(configured.action.revision==18 && std::get<ConfiguredActionKey>(configured.action.key).state=="AnyState");
    Check(std::get<FreeStyleState>(ResolveCatalogAction(*actor_catalog,configured.action).request).retirements.size()==1);
    request["request_id"]="configured-other-owner";request["commands"][0]["character"]=9;
    control.Submit(request.dump());Check(drain()[0]["event"]=="queued");
    Check(std::get<SelectCatalogAction>(pending.back().command).id==9); // actor validation belongs to the application frame
    auto ready=std::make_shared<ReadyActions>();ready->source="Original/Character.json";
    ReadyPose pose;pose.skill_index=9;pose.variant_index=2;pose.state="UnrelatedPoseName";
    pose.entry_ability={true,"ActualAbilityKey",{}};pose.child_skills={true,{"ChildKey"},{}};
    ready->poses.push_back(pose);actor_catalog->actions.ready_actions.push_back(ready);
    request["request_id"]="ready-exact";request["commands"]=Json::array({
        {{"op","ready_pose"},{"character",7},{"revision",18},{"source",ready->source},{"pose",0}},{{"op","snapshot"},{"actions",true}}});
    control.Submit(request.dump());rows=drain();Check(rows[0]["event"]=="queued");
    const auto& ready_request=std::get<ReadyActionKey>(std::get<SelectCatalogAction>(pending.back().command).action.key);
    Check(ready_request.source==ready->source && ready_request.pose==0);
    const auto& captured=rows[1]["snapshot"]["catalog"]["character_actions"]["7"];
    Check(captured["ready"][0]["poses"][0]["skill_index"]==9 && captured["ready"][0]["poses"][0]["entry_ability"]["value"]=="ActualAbilityKey");
    Check(captured["proposals"][0]["state"]=="AnyState");
    request["request_id"]="ready-out-of-range";request["commands"]=Json::array({{{"op","ready_pose"},{"character",7},{"revision",18},{"source",ready->source},{"pose",3}}});
    control.Submit(request.dump());Check(drain()[0]["event"]=="queued");
    bool absent=false;try{ResolveCatalogAction(*actor_catalog,std::get<SelectCatalogAction>(pending.back().command).action);}catch(const std::runtime_error&){absent=true;}Check(absent);
    // Two independent clients can target two catalogs without selecting either
    // in a menu. The worker queues identity; frame-time resolution owns data.
    auto other_catalog=std::make_shared<CharacterActionCatalog>();other_catalog->revision=19;
    other_catalog->actions.proposals.push_back({ActionDomain::Exploration,"OtherState",{}});
    catalog->characters[9]=other_catalog;
    request["request_id"]="two-actors";request["commands"]=Json::array({
        {{"op","configured_state"},{"character",9},{"revision",19},{"domain","exploration"},{"name","OtherState"}},
        {{"op","configured_state"},{"character",7},{"revision",18},{"domain","battle"},{"name","AnyState"}}});
    control.Submit(request.dump());rows=drain();Check(rows.size()==2 && rows[0]["event"]=="queued" && rows[1]["event"]=="queued");
    Check(std::get<SelectCatalogAction>(pending[pending.size()-2].command).id==9 && std::get<SelectCatalogAction>(pending.back().command).id==7);
    const auto saved_selection=std::get<SelectCatalogAction>(pending.back().command).action;
    actor_catalog->revision=20;bool changed=false;
    try{ResolveCatalogAction(*actor_catalog,saved_selection);}catch(const std::runtime_error&){changed=true;}Check(changed);
    bool rejected=false;try{ParseDevelopmentCommand({{"op","enable"},{"character",-1},{"enabled",true}});}catch(...){rejected=true;}Check(rejected);
    // A queued result owns only copied data and its mailbox, never the adapter.
    ApplicationService::ReplySink late;
    request["commands"]=Json::array({{{"op","select"},{"character",7}}});
    {DevelopmentControl temporary(42,{[&](auto,auto reply){late=std::move(reply);return 1;},[&]{return snapshot;}});
        temporary.Submit(request.dump());}
    late({1,7,true,"",0});
    std::map<std::uint64_t,VisualProgramParameters> overrides;
    std::vector<std::pair<std::uint64_t,VisualProgramSignal>> signals;
    DevelopmentControl preview(42,{
        [&](auto command,auto reply){pending.push_back({std::move(command),std::move(reply)});return 99;},
        [&]{return snapshot;},
        [&](auto character,auto inputs){overrides[character]=std::move(inputs);},
        [&](auto character,auto signal){signals.emplace_back(character,std::move(signal));}});
    const auto queued_before=pending.size();
    request["request_id"]="visual-values";
    request["commands"]=Json::array({
        {{"op","visual_inputs"},{"character",7},{"targets",{{"Target",9}}},{"choices",{{"prize","grand"}}},
            {"anchors",{{"center",{{"position",{6,0,58}}}}}}},
        {{"op","visual_inputs"},{"character",8},{"choices",{{"prize","minor"}}}}});
    preview.Submit(request.dump());
    Check(preview.Drain().size()==2 && pending.size()==queued_before && overrides.size()==2);
    Check(overrides[7].targets.at("Target")==9 && overrides[8].choices.at("prize")=="minor");
    Check(overrides[7].anchors.at("center").position.z==58 && overrides[7].anchors.at("center").rotation.w==1);
    request["request_id"]="visual-invalid-independent";
    request["commands"]=Json::array({
        {{"op","visual_inputs"},{"character",7},{"targets",{{"Caster",9}}}},
        {{"op","visual_inputs"},{"character",7},{"choices",{{"prize",5}}}},
        {{"op","visual_inputs"},{"character",8}}});
    preview.Submit(request.dump());const auto results=preview.Drain();
    Check(results.size()==3 && Json::parse(results[0])["event"]=="command_error" &&
        Json::parse(results[1])["event"]=="command_error" && Json::parse(results[2])["applies"]=="next_program_action");
    Check(overrides[7].choices.at("prize")=="grand" && overrides[8].choices.empty());
    request["request_id"]="view-events";
    request["commands"]=Json::array({
        {{"op","visual_signal"},{"character",7},{"generation",11},{"intent",3},{"name","enter_view"}},
        {{"op","visual_signal"},{"character",7},{"generation",0},{"intent",3},{"name","exit_view"}},
        {{"op","visual_signal"},{"character",7},{"generation",11},{"intent",3},{"name","exit_view"}}});
    preview.Submit(request.dump());const auto signal_results=preview.Drain();
    Check(signal_results.size()==3 && signals.size()==2 && pending.size()==queued_before);
    Check(signals[0].first==7 && signals[0].second.generation==11 && signals[0].second.intent==3 && signals[0].second.name=="enter_view");
    Check(Json::parse(signal_results[0])["event"]=="visual_signal_queued" && Json::parse(signal_results[1])["event"]=="command_error");
    request["request_id"]="task-control";
    request["commands"]=Json::array({
        {{"op","task_select"},{"character",7},{"creation",3},{"catalog","test"},{"task","attack"},{"inputs",{{"power",2},{"enhanced",true}}}},
        {{"op","task_query"},{"character",7},{"creation",3}},
        {{"op","task_finish"},{"character",7},{"creation",3}},
        {{"op","task_cancel"},{"character",7},{"creation",0}}});
    preview.Submit(request.dump());auto task_rows=preview.Drain();
    Check(task_rows.size()==4 && Json::parse(task_rows.back())["event"]=="command_error");
    const auto& task=std::get<ControlCharacterTask>(pending[pending.size()-3].command);
    Check(task.id==7 && task.creation==3 && task.input.selection->task.name=="attack");
    Check(std::get<std::int64_t>(task.input.selection->query.inputs.at("power"))==2);
    Check(std::get<bool>(task.input.selection->query.inputs.at("enhanced")));
    CommandReply task_reply;task_reply.accepted=true;task_reply.task.emplace();
    task_reply.task->status.current=TaskKey{"test","attack"};
    task_reply.task->status.execution=TaskExecutionStatus{};
    task_reply.task->contexts.push_back({{3},"phase",{{"enhanced",true}},std::string("boosted")});
    TaskRequestSnapshot receipt;receipt.name="body";receipt.source="task/body";
    receipt.receipt={{2},3,5,{"body"},7};receipt.state.performance_complete=true;receipt.state.progress=0.99;
    task_reply.task->requests.push_back(receipt);
    pending.back().reply(task_reply);task_rows=preview.Drain();
    Check(task_rows.size()==1 && Json::parse(task_rows[0])["task"]["current"]["name"]=="attack");
    Check(Json::parse(task_rows[0])["task"]["contexts"][0]["values"]["enhanced"]==true);
    const auto request_value=Json::parse(task_rows[0])["task"]["requests"][0];
    Check(request_value["name"]=="body" && request_value["execution"]==2 && request_value["model_generation"]==5 &&
        request_value["entity_generation"]==7 && request_value["performance_complete"]==true && request_value["progress"]==0.99);
}
