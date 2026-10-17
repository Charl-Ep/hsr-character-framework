#include <hksr/framework/layout/LayoutController.hpp>
#include <hksr/presentation/PresentationBindings.hpp>
#include <hksr/framework/character/SceneCharacters.hpp>
#include <hksr/framework/host/RuntimeServices.hpp>
#include <hksr/framework/presentation/VisualProgramRuntime.hpp>
#include "../../src/host/StopDrain.hpp"
#include <Windows.h>
#include <fstream>
#include <stdexcept>

namespace {
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct NoObjects final:hksr::api::ManagedObjects {
    hksr::api::ObjectResult NewStringUtf8(const char*) override {throw std::runtime_error("unexpected engine call");}
    hksr::api::ObjectResult AllocateObject(const char*,const char*) override {throw std::runtime_error("unexpected engine call");}
    hksr::api::ObjectResult ManagedType(const char*,const char*) override {throw std::runtime_error("unexpected engine call");}
};
struct DelayedAbilities final:hksr::api::AbilitySourceRead {
    bool& ready;
    explicit DelayedAbilities(bool& value):ready(value){}
    void Advance(std::uint32_t) override {}
    void Cancel() override {ready=true;}
    bool Done() const override {return ready;}
    hksr::api::AbilitySources Take() override {return {};}
};
}
void TestLayoutController() {
    using namespace hksr::framework;namespace api=hksr::api;
    const auto folder=std::filesystem::temp_directory_path()/
        ("hksr-layout-controller-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code e;std::filesystem::remove_all(path,e);}} cleanup{folder};
    NoObjects objects;api::PresentationBindings bindings;int info=0;
    const auto retain=+[](void*,int,void*)->int{throw std::runtime_error("unexpected retain");};
    const auto target=+[](int,void*)->void*{throw std::runtime_error("unexpected target");};
    const auto release=+[](int,void*){throw std::runtime_error("unexpected free");};
    bindings.references={{reinterpret_cast<void*>(retain),&info},{reinterpret_cast<void*>(target),&info},
        {reinterpret_cast<void*>(release),&info},{},{}};
    RuntimeServices runtime(objects,bindings);
    CatalogReaders readers;
    readers.characters=[] {
        api::CharacterCatalog c;c.npcs.enumerated=c.players.enumerated=c.avatars.enumerated=true;
        c.npcs.rows={{77,"npc/config","npc/model"}};c.players.rows={{11,91,0,0,"player/config","player.prefab"}};
        c.avatars.rows={{91,"battle/config","battle.prefab"}};return c;
    };
    readers.identity=[](const auto&){return api::TextInput{true,"A",{}};};
    readers.document=[](auto kind,const auto& path){
        api::ActionConfigInput a;a.kind=kind;a.path=path;a.loaded=true;
        a.presentation={path,{true,"OfficialIdle",{}},{true,{},{}},{true,{},{}}};
        a.states={{kind==api::ActionConfigKind::Battle?"OfficialIdle":"IdleShow","declared"}};
        if(kind==api::ActionConfigKind::Battle)a.abilities={{"SlowUnrelatedAttack","AbilityList[0]"}};
        return a;
    };
    bool abilities_ready=false;
    readers.begin_abilities=[&](const auto&){return std::make_unique<DelayedAbilities>(abilities_ready);};
    SceneLayout saved;saved.scene="mapA";
    SavedCharacter a;a.key=90;a.source=BrowseNpc{77};a.enabled=false;a.placement={{1,2,3},{0,0,0,1}};a.action=NpcMotion{123};
    auto b=a;b.key=91;b.look={LookMode::Instance,90,{}};b.source=BrowsePlayer{{11,0,0},77};
    b.action=SavedConfiguredAction{ActionDomain::Alternate,"OfficialIdle"};
    VisualContextSnapshot visual;visual.name="phase";visual.inputs.targets["Target"]=90;
    b.visual_state=VisualProgramSnapshot{"saved-program",{}, {visual}};saved.characters={a,b};
    LayoutStorage store(folder);store.SaveCurrent(saved);store.SaveNamed("first",saved);
    auto io=std::make_shared<LayoutIo>(folder);
    CatalogBrowser catalog(readers);SceneCharacters registry(runtime.Sessions(false),SceneCharacters::Create{});
    auto visual_input=std::make_shared<VisualProgramInput>();
    visual_input->program=std::make_shared<VisualProgram>(ParseVisualProgram(R"({
        "format":"hksr.visual-program","id":"saved-program","source":"battle/config","scope":"scene","effects":{},
        "contexts":{"phase":{"restore":[]}},"actions":{"idle":{"state":"OfficialIdle","restart":true}}})"));
    registry.ConfigurePrograms({{"battle/config",visual_input}});
    unsigned removed=0;LayoutController controller(registry,catalog,io,[&](auto){++removed;});
    const SceneAddress mapA{"mapA",1},mapB{"mapB",2};
    auto tick=[&](const SceneAddress& scene) {
        io->Pump();controller.Advance(scene);catalog.Advance(0);
        registry.Service({},scene,runtime.Appearance(),{});controller.Capture(true);
    };
    controller.Advance(mapA);controller.Capture(true);io->Pump();
    Check(store.Current("mapA")->characters.size()==2,"startup empty state overwrote saved layout");
    for(unsigned i=0;i<32;++i)tick(mapA);
    Check(registry.Ids().empty(),"default idle bypassed the same domain completion required by other actions");
    Check(store.Current("mapA")->characters.size()==2,"pending action preparation overwrote saved layout");
    abilities_ready=true;
    for(unsigned i=0;i<32;++i)tick(mapA);
    auto ids=registry.Ids();Check(ids.size()==2 && ids[0]!=90,"layout identities were not remapped");
    Check(registry.Status(ids[1]).look.target==ids[0],"layout gaze target was not remapped");
    Check(registry.CaptureVisualProgram(ids[1])->contexts[0].inputs.targets.at("Target")==ids[0],"visual context target was not remapped");
    Check(controller.Snapshot().maps.at("mapA").phase=="ready","disabled layout did not finish");
    Check(std::get<NpcMotion>(registry.DesiredAction(ids[0])->request).id==123,"saved action missing");
    Check(registry.Plan(ids[1]).alternate && registry.Plan(ids[1]).package.appearance &&
        std::get<FreeStyleState>(registry.DesiredAction(ids[1])->request).name=="OfficialIdle","official player battle layout not re-resolved");
    io->Pump();controller.Advance(mapA);
    auto status=controller.Snapshot().maps.at("mapA");
    Check(status.saved_revision==status.revision && status.revision>0,"write acknowledgement missing");
    for(unsigned refresh=0;refresh<2;++refresh) {
        controller.Execute({LayoutOperation::Refresh},"mapA",20+refresh);io->Pump();controller.Advance(mapA);
        Check(controller.Snapshot().maps.at("mapA").names==std::vector<std::string>{"first"},"refresh consumed its list twice");
        Check(store.Names("mapA")==std::vector<std::string>{"first"},"refresh changed snapshot files");
    }
    const auto locked=CreateFileW((folder/"map-mapA"/"current.json").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    Check(locked!=INVALID_HANDLE_VALUE,"could not simulate write rejection");
    registry.Select(ids[0],{ActionDomain::Exploration,NpcMotion{321}});controller.Capture(true);io->Pump();controller.Advance(mapA);
    status=controller.Snapshot().maps.at("mapA");
    CloseHandle(locked);
    Check(!status.storage_error.empty() && status.saved_revision<status.revision,"write failure was reported as saved");
    Check(std::get<NpcMotion>(*store.Current("mapA")->characters[0].action).id==123,"failed atomic write destroyed prior state");
    controller.Execute({LayoutOperation::RetryCurrent},"mapA",9);io->Pump();controller.Advance(mapA);
    status=controller.Snapshot().maps.at("mapA");
    Check(status.storage_error.empty() && status.saved_revision==status.revision,"explicit save retry failed");
    tick(mapB);tick(mapB);tick(mapA);tick(mapA);
    Check(registry.Ids()==ids,"same-process map return rebuilt logical instances from disk");

    // Fully prepare an invalid source BEFORE deleting any current actors.
    auto invalid=saved;invalid.characters[1].source=BrowseNpc{999};store.SaveNamed("invalid",invalid);
    controller.Execute({LayoutOperation::Load,"invalid"},"mapA",10);
    for(unsigned i=0;i<10;++i)tick(mapA);
    Check(registry.Ids()==ids && removed==0,"failed source preparation destroyed old layout");
    Check(!controller.Snapshot().maps.at("mapA").error.empty(),"failed restore has no diagnostic");
    invalid=saved;invalid.characters[1].visual_state->program="not-configured";store.SaveNamed("invalid-program",invalid);
    controller.Execute({LayoutOperation::Load,"invalid-program"},"mapA",12);
    for(unsigned i=0;i<10;++i)tick(mapA);
    Check(registry.Ids()==ids && removed==0 && !controller.Snapshot().maps.at("mapA").error.empty(),
        "unavailable visual program destroyed the current layout");
    controller.Execute({LayoutOperation::Load,"first"},"mapA",11);
    io->Pump();controller.Advance(mapB);
    for(unsigned i=0;i<6;++i)tick(mapB);
    Check(registry.Ids()==ids,"noncurrent map replacement committed in wrong world");
    for(unsigned i=0;i<32;++i)tick(mapA);
    Check(registry.Ids().size()==2 && registry.Ids()!=ids && removed==2,"explicit snapshot replacement failed");
    const auto replacement=registry.Ids();
    for(auto id:replacement)registry.Delete(id);
    tick(mapA);tick(mapA);
    Check(store.Current("mapA")->characters.empty(),"all-deleted layout was not saved as empty");
    Check(store.Named("mapA","first").characters.size()==2,"deletion modified a named snapshot");
    CharacterPlan closing_plan;closing_plan.package.npc_id=77;closing_plan.source_intent=BrowseNpc{77};
    registry.Add("mapB",closing_plan,{},a.placement);
    // Worker observes stop BEFORE the final official capture. Pumping an
    // empty queue must not let it exit; Close submits and seals on that frame.
    Check(!detail::PumpStoppingLayouts(api::RuntimeHostPhase::Stopping,*io),"writer exited before final frame");
    Check(!io->Status().sealed,"worker sealed a still-running producer");
    controller.Close();registry.Shutdown();
    Check(io->Status().sealed && !io->Status().Drained(),"last capture not retained before seal");
    Check(!detail::PumpStoppingLayouts(api::RuntimeHostPhase::Stopping,*io),"writer claimed client closed during drain");
    Check(detail::PumpStoppingLayouts(api::RuntimeHostPhase::Stopped,*io),"writer did not finish after client closure");
    Check(store.Current("mapB")->characters.size()==1 && store.Current("mapB")->characters[0].enabled,
        "host shutdown was persisted as user disable");

    // A new controller/registry is a new process: deleted actors stay deleted.
    CatalogBrowser fresh_catalog(readers);SceneCharacters fresh(runtime.Sessions(false),SceneCharacters::Create{});
    auto fresh_io=std::make_shared<LayoutIo>(folder);LayoutController fresh_control(fresh,fresh_catalog,fresh_io);
    fresh_control.Advance(mapA);fresh_io->Pump();fresh_control.Advance(mapA);fresh_control.Capture(true);fresh_io->Pump();
    Check(fresh.Ids().empty() && fresh_control.Snapshot().maps.at("mapA").initialized,"empty map restored stale actors");
    fresh_control.Close();fresh.Shutdown();

    // Corrupt input remains untouched until the user explicitly keeps current.
    std::filesystem::create_directories(folder/"map-corrupt");
    {std::ofstream f(folder/"map-corrupt"/"current.json");f<<"broken";}
    CatalogBrowser broken_catalog(readers);SceneCharacters broken(runtime.Sessions(false),SceneCharacters::Create{});
    auto broken_io=std::make_shared<LayoutIo>(folder);LayoutController broken_control(broken,broken_catalog,broken_io);
    broken_control.Advance(SceneAddress{"corrupt",3});broken_io->Pump();broken_control.Advance(SceneAddress{"corrupt",3});
    broken_control.Capture(true);broken_io->Pump();
    Check(!broken_control.Snapshot().maps.at("corrupt").initialized,"corrupt input treated as empty map");
    {std::ifstream f(folder/"map-corrupt"/"current.json");std::string text;f>>text;Check(text=="broken","corrupt original silently overwritten");}
    broken_control.Execute({LayoutOperation::KeepCurrent},"corrupt",12);broken_io->Pump();
    Check(store.Current("corrupt")->characters.empty(),"explicit keep current failed");
    broken_control.Close();broken.Shutdown();

    // Background automatic writes coalesce, explicit snapshots keep their value.
    auto queue=std::make_shared<LayoutIo>(folder);auto one=std::make_shared<SceneLayout>(saved);
    const auto w=queue->Submit({LayoutIoKind::WriteCurrent,"mapA",{},one,false,1});
    auto two=std::make_shared<SceneLayout>(saved);two->characters[0].placement.position.x=9;
    Check(w==queue->Submit({LayoutIoKind::WriteCurrent,"mapA",{},two,false,2}),"automatic writes did not coalesce");
    queue->Submit({LayoutIoKind::Save,"mapA","captured",one});
    queue->Submit({LayoutIoKind::WriteCurrent,"mapA",{},one,false,3});queue->Pump();
    const auto results=queue->Take();Check(results.size()==3 && results[0].request.revision==2,"write ordering broken");
    Check(store.Named("mapA","captured").characters[0].placement.position.x==1,"snapshot did not retain accepted values");
    {
        // An exploration action must not wait for a battle ability batch,
        // including a batch shared with other characters in the layout.
        abilities_ready=false;
        auto exploration=saved;exploration.scene="exploration-only";exploration.characters={b};
        exploration.characters[0].look={};
        exploration.characters[0].visual_state.reset();
        exploration.characters[0].action=SavedConfiguredAction{ActionDomain::Exploration,"IdleShow"};
        store.SaveCurrent(exploration);
        auto isolated_io=std::make_shared<LayoutIo>(folder);
        CatalogBrowser isolated_catalog(readers);
        SceneCharacters isolated_registry(runtime.Sessions(false),SceneCharacters::Create{});
        LayoutController isolated(isolated_registry,isolated_catalog,isolated_io);
        const SceneAddress address{exploration.scene,7};
        for(unsigned i=0;i<32;++i) {
            isolated_io->Pump();isolated.Advance(address);isolated_catalog.Advance(0);
            isolated_registry.Service({},address,runtime.Appearance(),{});
        }
        Check(!abilities_ready && isolated_registry.Ids().size()==1,"exploration restore waited for battle discovery");
        const auto& restored=isolated_registry.DesiredAction(isolated_registry.Ids()[0]);
        Check(restored && restored->domain==ActionDomain::Exploration &&
            std::get<AnimatorState>(restored->request).name=="IdleShow","exploration action was replaced to bypass discovery");
        isolated.Close();isolated_registry.Shutdown();
    }
}
