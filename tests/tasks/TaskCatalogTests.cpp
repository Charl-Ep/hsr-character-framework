#include <hksr/framework/tasks/TaskController.hpp>
#include <nlohmann/json.hpp>
#include <limits>
#include <stdexcept>

namespace {
using namespace hksr;
using namespace hksr::framework;
using Json=nlohmann::json;
void CheckAt(bool value,int line){if(!value)throw std::runtime_error("task catalog line "+std::to_string(line));}
#define Check(...) CheckAt((__VA_ARGS__),__LINE__)
template<class F> void Reject(F function) {
    bool rejected=false;try{function();}catch(const std::exception&){rejected=true;}Check(rejected);
}
Json Document() {
    return Json::parse(R"({
      "format":"hksr.task-catalog","id":"test","domain":"battle",
      "visual":{"id":"scene","source":"fixture","scope":"owned","effects":{},
        "contexts":{"enhanced":{"enter":[],"exit":[]}}},
      "contexts":{"enhanced":{"values":{"charged":true},"phase":"enhanced",
        "slots":[{"slot":1,"task":"hold"}],"disables":[{"tag":"normal","reason":"enhanced phase"}]}},
      "sequences":{
        "stand":{"entry":{"op":"animation","request":"pose","participant":"body","state":"StandBy"}},
        "attack":{"entry":{"op":"serial","children":[
          {"op":"animation","request":"body","participant":"body","state":"Attack"},
          {"op":"animation","request":"face","participant":"body","state":"Emo_Attack","layer":3},
          {"op":"parallel","children":[
            {"op":"wait_request","request":"body"},
            {"op":"wait_request","request":"face"}]}]},
          "interruption":{"op":"serial","children":[{"op":"wait_seconds","seconds":0.2},
            {"op":"animation","request":"exit","participant":"body","state":"Exit"}]}},
        "hold":{"entry":{"op":"serial","children":[
          {"op":"visual","request":"enter","instruction":{"op":"context_enter","name":"enhanced","source":"modifier/entry"}},
          {"op":"wait_request","request":"enter","signal":"accepted"},
          {"op":"repeat","until_finish":true,"child":{"op":"wait_frame"}},
          {"op":"visual","request":"exit","instruction":{"op":"context_exit","name":"enhanced","source":"modifier/exit"}},
          {"op":"wait_request","request":"exit","signal":"accepted"}]}}
      },
      "tasks":{
        "idle":{"entry":"stand","tags":["normal"]},
        "attack":{"entry":"attack","next":{"task":"idle"},
          "inputs":{"power":{"type":"integer","default":1}}},
        "hold":{"entry":"hold","sources":[{"document":"fixture","field":"Modifier"}]},
        "unmapped":{"requires":[{"op":"unresolved","source":"fixture/unknown","detail":"not translated"}]},
        "conditioned":{"entry":"stand","requires":[{"op":"value","name":"charged","is":true}]}
      }
    })");
}
struct Fixture {
    std::shared_ptr<const TaskCatalog> catalog;
    std::shared_ptr<VisualProgramPlayer> scene;
    std::unique_ptr<TaskController> controller;
    std::vector<std::pair<AnimationCommand,std::shared_ptr<AnimationRequestStatus>>> plays;
    bool accept=true,ready=true;
    std::function<void()> on_attach;
    VisualProgramParameters defaults,captured;
    std::uint64_t frame=0;
    explicit Fixture(Json document=Document()) {
        catalog=std::make_shared<const TaskCatalog>(ParseTaskCatalog(document.dump()));
        VisualProgramServices visual;
        visual.effects=std::make_shared<api::PresentationEffects>(api::EffectRequestServices{{},api::EffectAccess({}),{},{},{}});
        visual.create=[](auto&,const auto&,const auto&)->VisualEffectId{throw std::runtime_error("unexpected effect");};
        visual.attach=[&](auto){if(on_attach)on_attach();};
        visual.select_inputs=[&]{return defaults;};
        visual.available_targets=[](const auto& inputs) {
            std::vector<std::string> result{"Caster"};
            for(const auto& [name,id]:inputs.targets) {
                if(id==777)throw std::runtime_error("unrelated unresolved target");
                if(id==99)result.push_back(name);
            }
            return result;
        };
        visual.prepare_inputs=[&](auto,const auto& inputs,bool){captured=inputs;};
        scene=std::make_shared<VisualProgramPlayer>(catalog->visual,std::move(visual));
        TaskPresentationServices ports;
        ports.animation=[&](const auto& participant)->std::optional<TaskAnimationEndpoint> {
            if(participant.name!="body")throw std::invalid_argument("unknown participant");
            if(!ready)return {};
            return TaskAnimationEndpoint{7,[&](const auto& command) {
                auto value=std::make_shared<AnimationRequestStatus>();value->entity_generation=7;value->serial=plays.size()+1;
                value->model_generation=8;value->accepted=accept;plays.emplace_back(command,value);return value;
            },[&](auto serial){plays.at(serial-1).second->released=true;}};
        };
        controller=std::make_unique<TaskController>(7,catalog,scene,std::move(ports));
    }
    TaskSelectionResult Select(const char* name){return controller->Select({{"test",name}});}
    void Tick(double seconds=0){controller->Service(seconds,++frame);}
    ~Fixture(){controller->Close(TaskExitReason::Deleted);Tick();Tick();}
};
void Codec() {
    const auto input=Document();const auto parsed=ParseTaskCatalog(input.dump());
    Check(parsed.tasks.size()==5 && parsed.visual->actions.empty() && parsed.sequences.size()==3);
    Check(parsed.Resolve({"test","hold"}).definition.sources.at(0).field=="Modifier");
    Check(parsed.contexts.at("enhanced").disables.size()==1);
    auto configured=input;
    configured["tasks"]["attack"]["bindings"]={{"targets",{"Target"}},{"anchors",{"Destination"}},
        {"choices",{{"mode",{"low","high"}}}}};
    configured["tasks"]["attack"]["requires"]=Json::array({{{"op","target"},{"name","Target"}},
        {{"op","target"},{"name","Other"}},{{"op","target"},{"name","Caster"}},
        {{"op","choice"},{"name","branch"},{"is","yes"}}});
    const auto binding_catalog=ParseTaskCatalog(configured.dump());
    const auto& definition=binding_catalog.Resolve({"test","attack"}).definition;
    const auto bindings=TaskBindings(definition);
    Check(bindings.targets==std::vector<std::string>({"Target","Other"}) && bindings.anchors.at(0)=="Destination");
    Check(bindings.choices.at("mode").size()==2 && bindings.choices.at("branch").at(0)=="yes");
    TaskQuery query;query.targets={"Target","Other","Caster"};query.choices={{"mode","invalid"},{"branch","yes"}};
    Check(TaskContext{}.Query(definition,query).availability==TaskAvailability::Unavailable);
    query.choices["mode"]="low";Check(TaskContext{}.Query(definition,query).availability==TaskAvailability::Available);
    auto invalid_bindings=configured;invalid_bindings["tasks"]["attack"]["bindings"]["targets"]={"Caster"};
    Reject([&]{ParseTaskCatalog(invalid_bindings.dump());});
    invalid_bindings=configured;invalid_bindings["tasks"]["attack"]["bindings"]["choices"]["mode"]=Json::array();
    Reject([&]{ParseTaskCatalog(invalid_bindings.dump());});
    auto bad=input;bad["sequences"]["stand"]["entry"]["layer"]=-1;Reject([&]{ParseTaskCatalog(bad.dump());});
    bad=input;bad["tasks"]["attack"]["inputs"]["power"]["default"]=true;Reject([&]{ParseTaskCatalog(bad.dump());});
    bad=input;bad["tasks"]["attack"]["next"]["task"]="missing";Reject([&]{ParseTaskCatalog(bad.dump());});
    bad=input;bad["tasks"]["attack"]["next"]={{"task","attack"},{"inputs",{{"power","wrong"}}}};Reject([&]{ParseTaskCatalog(bad.dump());});
    bad=input;bad["sequences"]["attack"]["entry"]["children"][2]["children"][0]["request"]="missing";
    Reject([&]{ParseTaskCatalog(bad.dump());});
    bad=input;bad["contexts"]["missing"]=Json::object();Reject([&]{ParseTaskCatalog(bad.dump());});
    bad=input;bad["visual"]["actions"]={{"fake",{{"state","fake"}}}};Reject([&]{ParseTaskCatalog(bad.dump());});
    bad=input;bad["sequences"]["stand"]["entry"]["unexpected"]=true;Reject([&]{ParseTaskCatalog(bad.dump());});
    bad=input;bad["sequences"]["stand"]["entry"]={{"op","repeat"},{"child",{{"op","wait_frame"}}}};
    Reject([&]{ParseTaskCatalog(bad.dump());});
    bad=input;bad["tasks"]["attack"]["inputs"]["power"]["default"]=std::numeric_limits<std::uint64_t>::max();
    Reject([&]{ParseTaskCatalog(bad.dump());});
    // The same resource parser still serves the original action-clock format.
    auto legacy=input["visual"];legacy["format"]="hksr.visual-program";
    legacy["actions"]={{"idle",{{"state","StandBy"}}}};
    Check(ParseVisualProgram(legacy.dump()).actions.at("idle").state=="StandBy");
}
void SelectionAndInterrupt() {
    Fixture f;Check(f.Select("attack").intent==1);f.Tick();
    Check(f.plays.size()==2 && f.controller->Status().current->name=="attack");
    const auto denied=f.Select("unmapped");Check(!denied.intent && denied.query.availability==TaskAvailability::Unknown);
    Check(f.controller->Status().requested==1 && f.controller->Status().current->name=="attack");
    Check(f.Select("idle").intent==2);f.Tick(0.05);f.Tick(0.05);
    Check(f.plays.size()==2 && f.controller->Status().execution->phase==TaskExecutionPhase::Interrupting);
    // Re-selection does not abort the already-running interruption branch.
    Check(f.Select("idle").intent==3);f.Tick(0.05);Check(f.plays.size()==2);
    f.Tick(0.06);Check(f.plays.size()==4);
    Check(f.plays[2].first.state=="Exit" && f.plays[3].first.state=="StandBy");
    Check(f.controller->Status().started==3);f.Tick();Check(f.controller->Status().retiring==0);
}
void NaturalAndPending() {
    Fixture f;f.defaults.targets["Target"]=99;f.defaults.choices["mode"]="base";
    TaskSelection override_input{{"test","attack"}};override_input.visual.choices["mode"]="explicit";
    f.controller->Select(override_input);f.Tick();
    Check(f.captured.targets.at("Target")==99 && f.captured.choices.at("mode")=="explicit");
    f.plays[0].second->complete=true;f.Tick();Check(f.plays.size()==2);
    f.plays[1].second->complete=true;f.Tick();
    Check(f.plays.size()==3 && f.plays.back().first.state=="StandBy");
    Check(f.controller->Status().current->name=="idle");
    Check(f.controller->Selection()->task.name=="idle" &&
        f.controller->Selection()->visual.targets.at("Target")==99 &&
        f.controller->Selection()->visual.choices.at("mode")=="explicit");
    const auto saved=*f.controller->Selection();
    Fixture restored;restored.defaults.targets["Target"]=100;restored.defaults.choices["mode"]="new-default";
    restored.controller->Select(saved);restored.Tick();
    Check(saved.inputs_captured && restored.captured.targets.at("Target")==99 && restored.captured.choices.at("mode")=="explicit");
    f.controller->Cancel();Check(!f.controller->Selection());
    Fixture delayed;delayed.accept=false;delayed.Select("idle");delayed.Tick();delayed.Tick();
    Check(delayed.plays.size()==1 && !delayed.controller->Drained());
    delayed.plays[0].second->accepted=true;delayed.Tick();delayed.Tick();
    Check(delayed.controller->Drained());
    Fixture changed;auto overlay=changed.controller->Context().Enter("override");
    changed.controller->Context().SetSlot(overlay,{1},{"test","attack"});
    TaskSelection select{{"test","idle"}};select.query.slot=TaskSlot{1};
    Check(changed.controller->Select(select).resolved.name=="attack");
    changed.controller->Context().Exit(overlay);changed.Tick();
    Check(changed.plays.size()==1 && changed.plays[0].first.state=="StandBy"); // resolve from original slot baseline
}
void ContextsAndFinish() {
    Fixture f;auto base=f.controller->Context().Enter("user");
    f.controller->Context().SetValue(base,"charged",false);
    Check(f.controller->Query({{"test","conditioned"}}).query.availability==TaskAvailability::Unavailable);
    f.Select("hold");f.Tick();f.Tick();
    Check(f.controller->Context().Phase()=="enhanced");
    Check(f.controller->Query({{"test","idle"}}).query.availability==TaskAvailability::Unavailable);
    Check(f.controller->Query({{"test","conditioned"}}).query.availability==TaskAvailability::Available);
    TaskSelection slot{{"test","idle"}};slot.query.slot=TaskSlot{1};
    Check(f.controller->Query(slot).resolved.name=="hold");
    f.controller->RequestFinish();f.Tick();f.Tick();
    Check(!f.controller->Context().Phase());
    Check(std::get<bool>(f.controller->Context().Values().at("charged").value)==false);
    Check(f.controller->Drained());
    Fixture immediate;immediate.Select("hold");immediate.controller->RequestFinish();
    immediate.Tick();immediate.Tick();immediate.Tick();
    Check(immediate.controller->Drained() && !immediate.controller->Context().Phase());
}
void CloseAndReentrant() {
    Fixture f;f.ready=false;f.Select("attack");f.Tick();Check(f.plays.empty());
    f.controller->Close(TaskExitReason::WorldLeft);f.ready=true;f.Tick();f.Tick();
    Check(f.plays.empty() && f.controller->Drained());
    Reject([&]{f.Select("idle");});
    Fixture nested;nested.on_attach=[&]{nested.controller->Close(TaskExitReason::Deleted);};
    nested.Select("attack");nested.Tick();nested.Tick();
    Check(nested.plays.empty() && nested.controller->Drained());
    Fixture broken;broken.on_attach=[] {throw std::runtime_error("scope preparation failed");};
    broken.Select("attack");broken.Tick();broken.Tick();
    Check(broken.plays.empty() && broken.controller->Status().execution->reason==TaskExitReason::Failed);
    Check(broken.controller->Status().execution->issues.at(0).find("scope preparation failed")!=std::string::npos);
    Check(broken.controller->Drained());
}
void InputRequirementsAndPreparation() {
    auto document=Document();document["tasks"]["idle"]["requires"]={
        {{"op","target"},{"name","Target"}},{{"op","anchor"},{"name","Stage"}},
        {{"op","choice"},{"name","variant"},{"is","a"}}};
    Fixture f(document);
    const auto missing=f.controller->Query({{"test","idle"}});
    Check(missing.query.availability==TaskAvailability::Unavailable && missing.query.reasons.size()==3);
    f.defaults.targets["Unused"]=777;
    f.defaults.targets["Target"]=99;f.defaults.anchors["Stage"]={};f.defaults.choices["variant"]="a";
    Check(f.controller->Query({{"test","idle"}}).query.availability==TaskAvailability::Available);
    TaskSelection selected{{"test","idle"}};selected.visual.targets["Target"]=100;
    selected.query.targets={"Target"}; // caller cannot manufacture owned target availability
    Check(f.controller->Select(selected).query.availability==TaskAvailability::Unavailable);
    selected.visual.targets["Target"]=99;
    Check(f.controller->Select(selected).intent==1);
    f.controller->Service(3,++f.frame,false);Check(f.plays.empty() && f.controller->Status().pending);
    f.controller->Service(3,++f.frame,false);Check(f.plays.empty());
    f.Tick();Check(f.plays.size()==1 && f.captured.targets.at("Target")==99);
    Fixture changed(document);changed.defaults=f.defaults;changed.Select("idle");
    changed.defaults.targets["Target"]=0;changed.Tick();
    Check(changed.plays.empty() && changed.controller->Status().blocked->availability==TaskAvailability::Unavailable);
    auto branch_document=Document();
    branch_document["sequences"]["stand"]["entry"]={{"op","serial"},{"children",Json::array({
        {{"op","wait_frame"}},
        {{"op","branch"},{"condition",{{"op","choice"},{"name","mode"},{"is","a"}}},
         {"yes",{{"op","animation"},{"request","branch"},{"participant","body"},{"state","A"}}},
         {"no",{{"op","animation"},{"request","branch"},{"participant","body"},{"state","B"}}}}
    })}};
    Fixture frozen(branch_document);frozen.defaults.choices["mode"]="a";
    frozen.Select("idle");frozen.Tick();frozen.defaults.choices["mode"]="b";frozen.Tick();
    Check(frozen.plays.size()==1 && frozen.plays[0].first.state=="A" && frozen.captured.choices.at("mode")=="a");
}
}
void TestTaskCatalog(){Codec();SelectionAndInterrupt();NaturalAndPending();ContextsAndFinish();CloseAndReentrant();InputRequirementsAndPreparation();}
