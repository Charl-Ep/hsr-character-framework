#include <hksr/framework/http/HttpControl.hpp>
#include "../../src/http/HttpProtocol.hpp"
#include <httplib.h>
#include <future>
#include <fstream>
#include <iostream>

namespace {
using namespace hksr::framework;
using Json=nlohmann::json;
void Check(bool value,const char* what) {if(!value)throw std::runtime_error(what);}
unsigned rejection_case=0;
template<class F> void Reject(F f) {++rejection_case;bool failed=false;try{f();}catch(const std::exception&){failed=true;}if(!failed)throw std::runtime_error("expected rejection case "+std::to_string(rejection_case));}
Json Decode(const httplib::Result& r,int status=200) {
    Check(bool(r),"HTTP response missing");Check(r->status==status,"HTTP status mismatch");return Json::parse(r->body);
}
void Protocol() {
    ApplicationSnapshot control_snapshot;SceneCharacterStatus actor;actor.id=9;actor.controls.emplace();
    actor.controls->placement_phase=hksr::api::PlacementPhase::Applied;
    actor.controls->gaze_phase=hksr::api::GazePhase::OutOfRange;
    control_snapshot.characters.push_back(actor);
    const auto controls=http::Snapshot(control_snapshot).at("characters").at("9").at("controls");
    Check(controls.at("placement_phase")=="applied" && controls.at("gaze_phase")=="out_of_range","stable control state codes");
    Check(!controls.contains("look_note") && !controls.contains("placement_note"),"no translated status as identity");
    control_snapshot.characters.front().controls->gaze_phase=hksr::api::GazePhase::Restoring;
    Check(http::Snapshot(control_snapshot).at("characters").at("9").at("controls").at("gaze_phase")=="restoring","pending gaze restoration state");
    auto& branches=control_snapshot.characters.front().branch_state;branches.phase="enhanced";
    branches.values={{"","variant",std::string("a")},{"scene","count",3.0f}};
    const auto branch_json=http::Snapshot(control_snapshot).at("characters").at("9").at("branch_state");
    Check(branch_json.at("phase")=="enhanced" && branch_json.at("values")[0].at("value")=="a" &&
        branch_json.at("values")[1].at("value")==3.0,"typed presentation branch inputs");
    Check(http::Id("18446744073709551615")==UINT64_MAX,"string IDs preserve 64 bits");
    Reject([]{http::Id(Json(9007199254740992ULL));});Reject([]{http::Id(-1);});Reject([]{http::Id(1.5);});Reject([]{http::Id("1x");});
    const auto npc=http::Command({{"op","prepare_source"},{"source",{{"kind","npc"},{"npc_id","100"}}}});
    Check(std::get<BrowseNpc>(std::get<PrepareSource>(npc).source).id==100,"NPC source");
    const auto player=http::Command({{"op","prepare_source"},{"source",{{"kind","player"},{"player_id","20"},{"skin",2},{"enhanced",1}}}});
    Check(std::get<BrowsePlayer>(std::get<PrepareSource>(player).source).key.skin==2,"player source variant");
    const auto pose=Json{{"position",{{"x",1},{"y",2},{"z",3}}},{"rotation",{{"x",0},{"y",0},{"z",0},{"w",1}}}};
    auto place=http::Command({{"op","place"},{"character","99"},{"placement",pose},{"relative",true}});
    Check(std::get<PlaceCharacter>(place).relative,"relative placement");
    Check(!std::get<PlaceCharacter>(place).pose.snap_to_ground,"omitted grounding preserves policy");
    auto ground_pose=pose;ground_pose["snap_to_ground"]=false;
    auto unground=http::Command({{"op","place"},{"character","99"},{"placement",ground_pose}});
    Check(std::get<PlaceCharacter>(unground).pose.snap_to_ground==false,"grounding false preserved");
    ground_pose["snap_to_ground"]="false";
    Reject([&]{http::Command({{"op","place"},{"character","99"},{"placement",ground_pose}});});
    auto bad=pose;bad["rotation"]["w"]=0;
    Reject([&]{http::Command({{"op","place"},{"character",1},{"placement",bad}});});
    Reject([]{http::Command({{"op","catalog"},{"refersh",true}});});
    Reject([]{http::Command({{"op","observe_character"},{"character",1}});});
    Reject([]{http::Command({{"op","prepare_source"},{"source",{{"kind","package"},{"path","C:/secret"}}}});});
    auto act=http::Command({{"op","action"},{"character","5"},{"revision","9"},
        {"action",{{"kind","configured"},{"domain","battle"},{"state","Skill01"}}}});
    Check(std::get<SelectCatalogAction>(act).action.revision==9,"source revision");
    auto ready=http::Command({{"op","action"},{"character","5"},{"revision","9"},
        {"action",{{"kind","ready"},{"source","config"},{"pose","2"}}}});
    Check(std::get<ReadyActionKey>(std::get<SelectCatalogAction>(ready).action.key).pose==2,"ready selection");
    auto reaction=http::Command({{"op","action"},{"character",5},{"revision",9},
        {"action",{{"kind","visual_behavior"},{"source","config"},{"flag",28},{"recover",true}}}});
    Check(std::get<VisualActionKey>(std::get<SelectCatalogAction>(reaction).action.key).recover,"recovery selection");
    for(const Json flag:{Json(1.5),Json(true),Json(4294967295ULL)})Reject([&]{http::Command({{"op","action"},{"character",5},{"revision",9},
        {"action",{{"kind","visual_behavior"},{"source","config"},{"flag",flag},{"recover",false}}}});});
    const auto program=http::Command({{"op","action"},{"character","5"},
        {"action",{{"kind","program"},{"program","fixture"},{"action","alternative"},{"choices",{{"branch","b"}}}}}});
    const auto& program_request=std::get<ProgramAction>(std::get<SelectCharacterAction>(program).action.request);
    Check(program_request.program=="fixture" && program_request.action=="alternative" && program_request.choices.at("branch")=="b","explicit program identity");
    Reject([]{http::Command({{"op","action"},{"character",5},{"action",{{"kind","program"},{"program","fixture"},{"action",""}}}});});
    auto look=http::Command({{"op","look"},{"character",1},{"mode","point"},{"point",{{"x",1},{"y",2},{"z",3}}}});
    Check(std::get<LookCharacter>(look).intent.point.z==3,"point gaze");
    for(const auto* name:{"rebuild","browse_actions","browse_npc_actions","delete","idle"})http::Command({{"op",name},{"character",1}});
    for(const auto* name:{"retry_source","release_source"})http::Command({{"op",name},{"source_request",1}});
    http::Command({{"op","enable"},{"character",1},{"enabled",false}});
    http::Command({{"op","replace_source"},{"character",1},{"source_request",2}});
    auto create=http::Command({{"op","create"},{"source_request",2},{"near_player",true}});
    Check(std::get<AddPreparedCharacter>(create).near_player,"near player creation");
    http::Command({{"op","action"},{"character",1},{"action",{{"kind","npc"},{"id",9}}}});
    const auto layout=http::Command({{"op","layout"},{"operation","save"},{"name","布置"},{"scene","map-A"},{"overwrite",true}});
    Check(std::get<LayoutCommand>(layout).scene=="map-A" && std::get<LayoutCommand>(layout).overwrite,"map-bound layout command");
    Reject([]{http::Command({{"op","layout"},{"operation","auto_select"}});});

    ApplicationService app(1);ApplicationSnapshot state;state.prepared=true;
    state.characters.resize(1);state.characters[0].id=UINT64_MAX;state.characters[0].package.display_name="藿藿\nname";
    app.Publish(state);auto first=app.ReadUpdates();const auto event=http::Event(first);
    Check(!app.WaitForChange(first.changes.cursor,std::chrono::milliseconds(0)),"idle lightweight wait");
    Check(event.find("event: reset\n")!=std::string::npos && event.find("\\nname")!=std::string::npos,"SSE frames escape data");
    Check(http::ParseCursor(http::Cursor(first.changes.cursor)).sequence==1,"cursor round trip");
    Reject([]{http::ParseCursor("bad\nevent: injection");});
    auto snapshot=http::Updates(first);Check(snapshot["snapshot"]["characters"].contains("18446744073709551615"),"exact wire IDs");
    state.characters.clear();app.Publish(state);auto delta=http::Updates(app.ReadUpdates(first.changes.cursor));
    Check(app.WaitForChange(first.changes.cursor,std::chrono::milliseconds(0)),"changed lightweight wait");
    Check(delta["characters"]["18446744073709551615"].is_null() && !delta.contains("snapshot"),"delta removal not full snapshot");
    const auto before_layout=app.ReadUpdates().changes.cursor;
    state.layouts.available=true;state.layouts.sequence=1;state.layouts.maps["map-A"].phase="preparing";app.Publish(state);
    auto layout_update=http::Updates(app.ReadUpdates(before_layout));
    Check(layout_update["runtime"]["layouts"]["maps"]["map-A"]["phase"]=="preparing","layout-only SSE update missing");
    auto catalog=std::make_shared<CatalogSnapshot>();auto actions=std::make_shared<CharacterActionCatalog>();
    actions->revision=4;actions->actions.finished=true;actions->actions.proposals.push_back({ActionDomain::Alternate,"StandBy",{}, {}});
    auto set=std::make_shared<ReadyActions>();set->source="battle/config";set->poses.resize(1);set->poses[0].label="Skill ready";
    actions->actions.ready_actions.push_back(set);catalog->characters[3]=actions;state.catalog=catalog;state.characters.resize(1);state.characters[0].id=3;
    VisualState visual;visual.character_config="battle/config";visual.source="global";visual.value.flag={true,28,{}};
    visual.value.occur.resolved_state={true,"Stagger",{}};visual.value.loop.resolved_state={true,"Stagger_Loop",{}};
    visual.value.recover.resolved_state={true,"Stagger_Recover",{}};
    visual.value.occur.transition=visual.value.loop.transition=visual.value.recover.transition={true,0,{}};
    visual.expression={true,"Emo_Stun",{}};visual.expression_transition={true,0.2f,{}};
    actions->actions.visual_states.push_back(visual);
    const auto list=http::Actions(state,3);Check(list["revision"]=="4" && list["ready"].size()==1 && list["configured"].size()==1,"per-character action view");
    Check(list["domain_ready"]["exploration"]==true && list["domain_ready"]["battle"]==true,"settled action domains");
    Check(list["reactions"].size()==2 && list["reactions"][0]["available"]==true &&
        list["reactions"][0]["expression"]=="Emo_Stun","shared reactions exposed");
    const auto reaction_command=http::Command({{"op","action"},{"character",3},{"revision",4},{"action",list["reactions"][1]["action"]}});
    const auto resolved=ResolveCatalogAction(*actions,std::get<SelectCatalogAction>(reaction_command).action);
    Check(std::get<VisualState>(resolved.request).recover,"published reaction round trip through actual resolver");
    state.characters[0].visual_program="fixture";state.characters[0].program_actions={{"first","SameBody"},{"second","SameBody"}};
    const auto program_list=http::Actions(state,3).at("program");
    Check(program_list.size()==2 && program_list[0]["action"]["action"]=="first" && program_list[1]["action"]["action"]=="second","shared body retains both program entries");
    const auto selection=http::Command({{"op","action"},{"character",3},{"action",program_list[1]["action"]}});
    Check(std::get<ProgramAction>(std::get<SelectCharacterAction>(selection).action.request).action=="second","published selection round trip");
    actions->actions.finished=false;actions->actions.pending_domains.insert(ActionDomain::Alternate);
    const auto partial_list=http::Actions(state,3);
    Check(partial_list["domain_ready"]["exploration"]==true && partial_list["domain_ready"]["battle"]==false,"independent domain readiness");
    actions->actions.pending_domains.clear();actions->actions.finished=true;
    auto motions=std::make_shared<CharacterNpcActions>();motions->creation=1;motions->source.motions.resize(1);motions->source.motions[0].id=17;
    state.npc_actions[3]=motions;state.characters[0].creations=2;
    Check(http::Actions(state,3)["npc"].empty(),"do not expose a prior creation's NPC motions");
    state.characters[0].creations=1;Check(http::Actions(state,3)["npc"].size()==1,"matching NPC motion generation");
    Reject([&]{http::Actions(state,4);});
    state.characters[0].active=true;state.characters[0].battle_idle_state="StandBy";
    auto task_view=std::make_shared<CharacterTaskStatus>();state.characters[0].tasks=task_view;
    auto& actor_tasks=*task_view;actor_tasks.domain=ActionDomain::Alternate;
    state.characters[0].task_catalog="full";state.characters[0].task_catalog_revision=7;
    TaskDefinition task;task.key={"full","modifier-followup"};task.entry="followup";
    task.sources={{"fixture","Modifier.OnExit"}};actor_tasks.definitions.emplace(task.key.name,task);
    actor_tasks.control.catalog.emplace(task.key.name,TaskQueryResult{});
    auto unknown=task;unknown.key.name="unmapped";unknown.entry.clear();actor_tasks.definitions.emplace(unknown.key.name,unknown);
    actor_tasks.control.catalog[unknown.key.name]={TaskAvailability::Unknown,{},{{TaskReasonKind::Unimplemented,"fixture","no visual entry"}}};
    const auto directory=http::Tasks(state,3);std::set<std::string> backends;
    for(const auto& entry:directory.at("entries")) {
        backends.insert(entry.at("backend").get<std::string>());
        const auto parsed=http::Command(entry.at("command"));
        if(entry.at("backend")=="sequence") {
            const auto& selected=std::get<ControlCharacterTask>(parsed);
            Check(selected.id==3 && selected.creation==1 && selected.input.selection->task.catalog=="full","task identity roundtrip");
        }
    }
    Check(backends==std::set<std::string>{"npc","configured_clip","ready","reaction","program","sequence","idle"},"complete available backend directory");
    auto command=Json{{"op","task_select"},{"character","3"},{"creation","1"},{"catalog","full"},{"task","modifier-followup"},
        {"inputs",{{"charged",true},{"count",3}}},{"targets",{{"Target","9007199254740999"}}}};
    const auto task_command=std::get<ControlCharacterTask>(http::Command(command));
    Check(task_command.input.selection->visual.targets.at("Target")==9007199254740999ULL,"task target IDs preserve precision");
    command["targets"]["Target"]=9007199254740999ULL;Reject([&]{http::Command(command);});
    CommandReply task_reply;task_reply.task=actor_tasks.control;
    Check(http::Reply(task_reply)["task"]["catalog"].contains("modifier-followup"),"query response included");
    app.Publish(state);const auto task_cursor=app.ReadUpdates().changes.cursor;
    auto updated_tasks=std::make_shared<CharacterTaskStatus>(actor_tasks);
    updated_tasks->control.contexts.push_back({{1},"enhanced",{{"charged",true}},"enhanced",{}, {}});
    state.characters[0].tasks=updated_tasks;
    app.Publish(state);const auto task_update=http::Updates(app.ReadUpdates(task_cursor));
    Check(task_update["characters"]["3"]["tasks"]["control"]["contexts"].size()==1,"task-only context update published over SSE");
}
void Configuration() {
    auto path=std::filesystem::temp_directory_path()/("hksr-http-"+std::to_string(GetCurrentProcessId())+".cfg");
    struct Remove {std::filesystem::path path;~Remove(){std::error_code e;std::filesystem::remove(path,e);}} cleanup{path};
    Check(!ReadHttpSettings(path).enabled,"missing config does not enable network");
    {std::ofstream file(path);file<<"[http]\nenabled=true\naddress=127.0.0.1\nport=9123\ntoken=abcdefghijklmnopqrstuvwxyz012345\norigins=http://localhost:8080, https://example.test\n";}
    const auto settings=ReadHttpSettings(path);Check(settings.enabled && settings.port==9123 && settings.origins.size()==2,"config fields");
    {std::ofstream file(path);file<<"[http]\nenabled=true\nenabled=false\n";}Reject([&]{ReadHttpSettings(path);});
    {std::ofstream file(path);file<<"[http]\nport=65536\n";}Reject([&]{ReadHttpSettings(path);});
    {std::ofstream file(path);file<<"[http]\nlisten=true\n";}Reject([&]{ReadHttpSettings(path);});
}
void Network() {
    ApplicationService app;HttpControl server(app);server.Start({});Check(!server.Status().listening,"default off");
    httplib::Server reserve;const int port=reserve.bind_to_any_port("127.0.0.1");Check(port>0,"reserve test port");reserve.stop();
    HttpSettings settings;settings.enabled=true;settings.port=static_cast<unsigned>(port);
    settings.token="abcdefghijklmnopqrstuvwxyz012345";settings.origins={"http://localhost:8080"};
    auto invalid=settings;invalid.address="localhost";Reject([&]{server.Start(invalid);});
    invalid=settings;invalid.token="short";Reject([&]{server.Start(invalid);});
    server.Start(settings);Check(server.Status().listening,"explicit listener");Reject([&]{server.Start(settings);});
    HttpControl collision(app);Reject([&]{collision.Start(settings);});
    httplib::Client client("127.0.0.1",port);client.set_connection_timeout(2,0);client.set_read_timeout(3,0);
    httplib::Headers headers={{"Authorization","Bearer "+settings.token}};
    Decode(client.Get("/v1/snapshot"),401);
    auto bad=headers;bad.emplace("Origin","http://evil.test");Decode(client.Get("/v1/snapshot",bad),403);
    auto allowed=headers;allowed.emplace("Origin","http://localhost:8080");
    auto response=client.Get("/v1/snapshot",allowed);Decode(response);Check(response->get_header_value("Access-Control-Allow-Origin")=="http://localhost:8080","CORS");
    auto preflight=client.Options("/v1/commands",{{"Origin","http://localhost:8080"},{"Access-Control-Request-Method","POST"}});
    Check(preflight && preflight->status==204,"allowed browser preflight without token");
    auto initial=Decode(client.Get("/v1/snapshot",headers));Check(initial["reset"]==true,"snapshot synchronization");
    Decode(client.Post("/v1/commands",headers,"{}","text/plain"),415);
    Decode(client.Post("/v1/commands",headers,"{","application/json"),400);
    Decode(client.Post("/v1/commands",headers,std::string(20,'[')+"0"+std::string(20,']'),"application/json"),400);
    Decode(client.Post("/v1/commands",headers,std::string(65537,'x'),"application/json"),413);
    Decode(client.Post("/v1/commands",headers,R"({"op":"write_memory"})","application/json"),400);
    auto post=Decode(client.Post("/v1/commands",headers,R"({"op":"catalog"})","application/json"),202);
    const auto location=post.at("request_url").get<std::string>();
    Check(Decode(client.Get(location,headers))["state"]=="queued","asynchronous queued receipt");
    auto batch=app.TakeCommands(false);Check(batch.size()==1 && std::holds_alternative<RequestCatalog>(batch[0].command),"typed application command");
    app.Deliver(batch[0],{batch[0].serial,0,true,{}});
    Check(Decode(client.Get(location,headers))["reply"]["accepted"]==true,"dispatch receipt independent from menu");
    Check(app.TakeReplies().empty(),"HTTP does not steal or populate menu replies");
    // Both clients enqueue independently; no selected menu, shared request
    // slot or lost acknowledgement. One failed dispatch remains distinguishable.
    auto second=Decode(client.Post("/v1/commands",headers,R"({"op":"browse_actions","character":"40"})","application/json"),202);
    auto third=Decode(client.Post("/v1/commands",headers,R"({"op":"browse_actions","character":"41"})","application/json"),202);
    batch=app.TakeCommands(false);Check(batch.size()==2 && batch[0].serial!=batch[1].serial,"independent submissions");
    app.Deliver(batch[0],{batch[0].serial,40,false,"absent"});app.Deliver(batch[1],{batch[1].serial,41,true,{}});
    Check(Decode(client.Get(second["request_url"].get<std::string>(),headers))["reply"]["accepted"]==false,"failed reply");
    Check(Decode(client.Get(third["request_url"].get<std::string>(),headers))["reply"]["character"]=="41","unrelated successful reply");
    auto malformed=headers;malformed.emplace("Last-Event-ID","broken");Decode(client.Get("/v1/events",malformed),400);

    // Real independent SSE connections and a normal HTTP request at capacity.
    std::vector<std::future<void>> streams;std::vector<std::future<void>> opened;
    for(unsigned i=0;i<8;++i) {
        auto signal=std::make_shared<std::promise<void>>();opened.push_back(signal->get_future());
        streams.push_back(std::async(std::launch::async,[port,headers,signal] {
            httplib::Client cli("127.0.0.1",port);cli.set_read_timeout(5,0);bool announced=false;
            cli.Get("/v1/events",headers,[&](const char* text,std::size_t length) {
                if(!announced){Check(std::string(text,length).find("event: reset")!=std::string::npos,"SSE initial reset");announced=true;signal->set_value();}
                return true;
            });
            if(!announced)signal->set_exception(std::make_exception_ptr(std::runtime_error("SSE not opened")));
        }));
    }
    for(auto& ready:opened){Check(ready.wait_for(std::chrono::seconds(3))==std::future_status::ready,"stream startup");ready.get();}
    Decode(client.Get("/v1/events",headers),429);Decode(client.Get("/v1/catalog",headers));
    // Stopping an HTTP server leaves accepted application commands queued.
    post=Decode(client.Post("/v1/commands",headers,R"({"op":"catalog"})","application/json"),202);
    // Hold a partial header, not a cooperating httplib client. Stop must not
    // depend on a peer finishing a request or on an idle/read timeout.
    const auto slow=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);Check(slow!=INVALID_SOCKET,"slow socket");
    sockaddr_in endpoint{};endpoint.sin_family=AF_INET;endpoint.sin_port=htons(static_cast<u_short>(port));
    InetPtonA(AF_INET,"127.0.0.1",&endpoint.sin_addr);
    Check(::connect(slow,reinterpret_cast<sockaddr*>(&endpoint),sizeof(endpoint))==0,"slow connection");
    const std::string partial="POST /v1/commands HTTP/1.1\r\nHost: local\r\n";
    ::send(slow,partial.data(),static_cast<int>(partial.size()),0);
    // Exercise an in-flight partial request as well as queued connections.
    // Immediate Stop mostly tests a not-yet-dispatched socket instead.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto begin=std::chrono::steady_clock::now();server.Stop();
    ::closesocket(slow);
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
    if(elapsed>=1000)throw std::runtime_error("stop took "+std::to_string(elapsed)+" ms with incomplete requests, SSE and HTTP workers");
    for(auto& stream:streams)stream.get();
    batch=app.TakeCommands(false);Check(batch.size()==1,"stop does not cancel accepted commands");
    app.Deliver(batch[0],{batch[0].serial,0,true,{}}); // receiver valid after server destruction
    server.Start(settings);
    ApplicationSnapshot state;state.prepared=true;app.Publish(state);
    auto replay=headers;replay.emplace("Last-Event-ID",initial.at("cursor").get<std::string>());std::string event;
    client.Get("/v1/events",replay,[&](const char* data,std::size_t size){event.append(data,size);return event.find("\n\n")==std::string::npos;});
    Check(event.find("event: update")!=std::string::npos && event.find("\"prepared\":true")!=std::string::npos,"resume after listener restart");
    auto other=headers;other.emplace("Last-Event-ID","00000000000000000000000000000000:0");event.clear();
    client.Get("/v1/events",other,[&](const char* data,std::size_t size){event.append(data,size);return event.find("\n\n")==std::string::npos;});
    Check(event.find("event: reset")!=std::string::npos,"foreign stream resets across real HTTP");
    // Backpressure only covers this transport's un-dispatched requests, not
    // character count. Draining makes room and bounded terminal history expires.
    for(unsigned i=0;i<256;++i)Decode(client.Post("/v1/commands",headers,R"({"op":"catalog"})","application/json"),202);
    Decode(client.Post("/v1/commands",headers,R"({"op":"catalog"})","application/json"),429);
    batch=app.TakeCommands(false);Check(batch.size()==256,"pending HTTP work retained");
    const auto oldest=batch.front().serial;
    for(const auto& command:batch)app.Deliver(command,{command.serial,0,true,{}});
    post=Decode(client.Post("/v1/commands",headers,R"({"op":"catalog"})","application/json"),202);
    batch=app.TakeCommands(false);app.Deliver(batch.front(),{batch.front().serial,0,true,{}});
    Decode(client.Get("/v1/requests/"+std::to_string(oldest),headers),404);
    app.TakeCommands(true);Decode(client.Post("/v1/commands",headers,R"({"op":"catalog"})","application/json"),503);
    server.Stop();Check(!server.Status().listening,"stopped");
}
}
int main() {
    try {Protocol();Configuration();Network();std::cout<<"HTTP protocol, configuration, lifecycle and SSE tests passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
