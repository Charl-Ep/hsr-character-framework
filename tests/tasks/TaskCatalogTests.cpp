#include <hksr/framework/tasks/TaskController.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
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
    std::vector<VisualFace> faces;
    bool facing_ready=true;
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
        visual.face=[&](auto&,const VisualFace& command) {
            if(!facing_ready)return VisualStepResult{VisualStepState::Pending,"waiting for model"};
            faces.push_back(command);return VisualStepResult{VisualStepState::Done,{}};
        };
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
    auto sound=input;
    sound["sequences"]["stand"]["entry"]={{"op","serial"},{"children",Json::array({
        {{"op","sound"},{"request","voice"},{"participant","body"},{"event","voice_event"}},
        {{"op","wait_request"},{"request","voice"},{"signal","started"}},
        {{"op","wait_request"},{"request","voice"},{"signal","performance"}}})}};
    const auto with_sound=ParseTaskCatalog(sound.dump());
    const auto& sound_nodes=with_sound.sequences.at("stand")->nodes;
    Check(std::get<TaskSoundCommand>(std::get<TaskSubmit>(sound_nodes.at(0).operation).operation).event=="voice_event");
    Check(std::get<TaskWaitRequest>(sound_nodes.at(1).operation).signal==TaskWaitSignal::Started);
    sound["sequences"]["stand"]["entry"]["children"][0]["event"]="";
    Reject([&]{ParseTaskCatalog(sound.dump());});
    auto movement=input;
    movement["sequences"]["stand"]["entry"]={{"op","serial"},{"children",Json::array({
        {{"op","animation"},{"request","body"},{"participant","body"},{"state","Attack"}},
        {{"op","movement"},{"request","move"},{"animation","body"},{"clock","animation"},{"segments",Json::array({
            {{"destination","target"},{"name","Target"},{"stop_distance",1.5},{"start",0},{"end",0.4}},
            {{"destination","anchor"},{"name","Return"},{"offset",{0,0,1}},{"start",0.8},{"end",1.2}}
        })}}
    })}};
    const auto with_move=ParseTaskCatalog(movement.dump());
    const auto& move=std::get<TaskMovementCommand>(std::get<TaskSubmit>(with_move.sequences.at("stand")->nodes[1].operation).operation);
    Check(move.animation=="body"&&move.movement.state.empty()&&move.movement.segments[1].destination==MovementDestination::Anchor);
    auto missing=movement;missing["sequences"]["stand"]["entry"]["children"][1]["animation"]="not-declared";
    Reject([&]{ParseTaskCatalog(missing.dump());});
    missing=movement;missing["sequences"]["stand"]["entry"]["children"][0]["layer"]=3;
    Reject([&]{ParseTaskCatalog(missing.dump());});
    missing=movement;missing["sequences"]["stand"]["entry"]["children"][1]["segments"][0]["end"]=0;
    Reject([&]{ParseTaskCatalog(missing.dump());});
    missing=movement;missing["sequences"]["stand"]["entry"]["children"][1]["segments"][0]["destination"]="origin";
    Reject([&]{ParseTaskCatalog(missing.dump());});
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
void AnimationOffsets() {
    auto document=Document();
    auto& command=document["sequences"]["stand"]["entry"];
    command["start"]=0.375;command["transition"]=0.2;
    Fixture f(document);f.Select("idle");f.Tick();
    Check(f.plays.size()==1 && f.plays[0].first.normalized_start==0.375f && f.plays[0].first.transition==0.2f);
    Check(!f.plays[0].second->entered && !f.plays[0].second->progress);
    command["start"]="0.375";Reject([&]{ParseTaskCatalog(document.dump());});
    command["start"]=1e100;Reject([&]{ParseTaskCatalog(document.dump());});
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
Json ControlPath(const char* trigger="user",bool presentation=true) {
    return {{"trigger",trigger},{"presentation",presentation},
        {"sources",Json::array({{{"document","fixture"},{"field","control consumer"}}})}};
}
void ControlAdmissionAndPresentation() {
    auto document=Document();const Json skill={{"source","character/config"},{"index",6},{"slot",2}};
    auto ready=ControlPath();ready["accepted"]=Json::array({{{"op","select"},{"skill",skill}}});
    ready["requires"]=Json::array({{{"op","performance"},{"in",{"ready"}}}});
    document["tasks"]["idle"]["control"]=ready;
    auto attack=ControlPath();attack["requires"]=Json::array({{{"op","selected"},{"skill",skill}},
        {{"op","performance"},{"in",{"ready"}}}});
    attack["accepted"]=Json::array({{{"op","performance"},{"phase","accepted"}}});
    attack["started"]=Json::array({{{"op","performance"},{"phase","working"}}});
    attack["completed"]=Json::array({{{"op","performance"},{"phase","finished"}}});
    attack["failed"]=Json::array({{{"op","performance"},{"phase","failed"}}});
    document["tasks"]["attack"]["control"]=attack;
    document["tasks"]["attack"].erase("next");
    auto queue=ControlPath("user",false);queue["accepted"]=Json::array({{{"op","queue"},{"id","ultra"},{"skill",skill}}});
    document["tasks"]["insert"]={{"control",queue}};
    document["tasks"]["unmapped"]["control"]=ControlPath();
    Fixture f(document);f.ready=false;
    Check(f.controller->Query({{"test","attack"}}).query.facets->path==TaskAvailability::Unavailable);
    const auto missing=f.controller->Query({{"test","unmapped"}});
    Check(missing.query.facets->path==TaskAvailability::Unknown && missing.query.facets->binding==TaskAvailability::Unknown);
    Check(f.Select("idle").intent==1);
    Check(f.controller->Status().control.selection->index==6 && f.plays.empty());
    f.Tick();Check(f.plays.empty());
    Check(f.Select("attack").intent==2);
    Check(f.controller->Status().control.performance==ControlPerformance::Accepted);
    Check(f.controller->Query({{"test","idle"}}).query.facets->path==TaskAvailability::Unavailable);
    f.ready=true;f.Tick();f.Tick();
    Check(f.plays.size()==2 && f.controller->Status().current->name=="attack");
    Check(f.Select("insert").intent==3);f.Tick();
    Check(f.controller->Status().control.pending.size()==1 && f.controller->Status().current->name=="attack");
    Check(f.controller->Selection()->task.name=="attack");
    f.plays[0].second->entered=true;f.Tick();
    Check(f.controller->Status().control.performance==ControlPerformance::Working);
    f.plays[0].second->complete=true;f.plays[1].second->complete=true;f.Tick();
    Check(f.controller->Status().control.performance==ControlPerformance::Finished);
    Check(f.plays.size()==2); // no inferred StandBy or reset
    auto direct_auto=TaskSelection{{"test","attack"}};direct_auto.trigger=ControlTrigger::Automatic;
    Check(!f.controller->Select(direct_auto).intent);
    auto invalid=document;invalid["tasks"]["insert"]["entry"]="stand";Reject([&]{ParseTaskCatalog(invalid.dump());});
    invalid=document;invalid["tasks"]["idle"]["control"]["requires"][0]["in"]={"invented"};Reject([&]{ParseTaskCatalog(invalid.dump());});
    invalid=document;invalid["tasks"]["idle"]["control"]["accepted"][0]["skill"]["index"]=-1;Reject([&]{ParseTaskCatalog(invalid.dump());});
    auto automatic=ControlPath("automatic",false);
    automatic["requires"]=Json::array({{{"op","performance"},{"in",{"finished"}}}});
    automatic["accepted"]=Json::array({{{"op","performance"},{"phase","ready"}}});
    document["tasks"]["reset"]={{"control",automatic}};
    document["tasks"]["attack"]["next"]={{"task","reset"}};
    Fixture successor(document);successor.Select("idle");successor.Tick();successor.Select("attack");successor.Tick();successor.Tick();
    successor.plays[1].second->complete=true;successor.plays[2].second->complete=true;successor.Tick();
    Check(successor.controller->Status().control.performance==ControlPerformance::Ready);
    Check(successor.controller->Status().error.empty() && successor.plays.size()==3);
    document["tasks"]["attack"].erase("next");document["automatic"]={"reset"};
    Fixture automatic_reset(document);automatic_reset.Select("idle");automatic_reset.Tick();automatic_reset.Select("attack");automatic_reset.Tick();automatic_reset.Tick();
    automatic_reset.plays[1].second->complete=true;automatic_reset.plays[2].second->complete=true;automatic_reset.Tick();
    Check(automatic_reset.controller->Status().control.performance==ControlPerformance::Finished);
    automatic_reset.Tick();Check(automatic_reset.controller->Status().control.performance==ControlPerformance::Ready);
    const auto accepted=automatic_reset.controller->Status().requested;automatic_reset.Tick();
    Check(automatic_reset.controller->Status().requested==accepted); // condition changed, no repeated reset
    invalid=document;invalid["automatic"]={"attack"};Reject([&]{ParseTaskCatalog(invalid.dump());});
}
void ControlSlotsReachDispatch() {
    auto document=Document();
    document["control_slots"]={{"skills",Json::array({
        {{"skill",{{"source","fixture"},{"index",4}}},{"key","basic"}},
        {{"skill",{{"source","fixture"},{"index",9}}},{"key","enhanced"}}})},
        {"defaults",Json::array({{{"slot",1},{"key","basic"}}})}};
    auto control=ControlPath();control["requires"]=Json::array({
        {{"op","slot_skill"},{"slot",1},{"skill",{{"source","fixture"},{"index",4},{"slot",1}}}},
        {{"op","slot_available"},{"slot",1}}});
    document["tasks"]["attack"]["control"]=control;document["tasks"]["attack"].erase("next");
    auto map=ControlPath("event",false);map["accepted"]=Json::array({{{"op","map_slot"},{"slot",1},{"key","enhanced"}}});
    document["tasks"]["phase"]={{"control",map}};
    Fixture f(document);Check(f.controller->Status().control.slots.slots.at(1).current=="basic");
    Check(f.controller->Query({{"test","attack"}}).query.availability==TaskAvailability::Available);
    f.Select("idle");f.Tick();
    TaskSelection phase{{"test","phase"}};phase.trigger=ControlTrigger::Event;
    Check(f.controller->Select(phase).intent!=0);f.Tick();
    const auto blocked=f.controller->Query({{"test","attack"}});
    Check(blocked.query.facets->path==TaskAvailability::Unavailable && blocked.query.facets->binding==TaskAvailability::Available);
    Check(f.Select("attack").intent==0 && f.plays.size()==1); // dispatch uses the same current-slot check
    Check(f.controller->Selection()->task.name=="idle");
    f.controller->Reset(TaskExitReason::WorldLeft);f.Tick();
    Check(f.controller->Query({{"test","attack"}}).query.availability==TaskAvailability::Available);
}
void ControlAtSequenceBoundaries() {
    auto document=Document();
    auto path=ControlPath();
    path["started"]=Json::array({{{"op","performance"},{"phase","working"}}});
    path["completed"]=Json::array({{{"op","remove_source"},{"id","sequence-phase"}}});
    path["replaced"]=path["completed"];
    document["tasks"]["attack"]["control"]=path;document["tasks"]["attack"].erase("next");
    document["sequences"]["attack"]={{"entry",{{"op","serial"},{"children",Json::array({
        {{"op","animation"},{"request","body"},{"participant","body"},{"state","Attack"}},
        {{"op","wait_request"},{"request","body"},{"signal","progress"},{"progress",0.5}},
        {{"op","control"},{"request","phase-change"},{"changes",Json::array({
            {{"op","add_source"},{"id","sequence-phase"},{"flag",17},{"priority",2}},
            {{"op","performance"},{"phase","finished"}}})}},
        {{"op","wait_frame"}},
        {{"op","wait_request"},{"request","body"},{"signal","performance"}}
    })}}}};
    auto conditional=ControlPath("event",false);
    conditional["requires"]=Json::array({{{"op","source"},{"id","sequence-phase"}}});
    document["tasks"]["during"]={{"control",conditional}};
    Fixture f(document);TaskSelection during{{"test","during"}};during.trigger=ControlTrigger::Event;
    f.Select("attack");f.Tick();
    Check(f.controller->Query(during).query.availability==TaskAvailability::Unavailable);
    f.plays[0].second->progress=0.49;f.Tick();Check(f.controller->Status().control.sources.empty());
    f.plays[0].second->entered=true;f.plays[0].second->progress=0.5;f.Tick();
    Check(f.controller->Status().control.performance==ControlPerformance::Finished); // started did not rewind the explicit step
    Check(f.controller->Query(during).query.availability==TaskAvailability::Available);
    Check(f.plays.size()==1 && f.controller->Selection()->task.name=="attack");
    const auto requests=f.controller->Requests();
    const auto update=std::find_if(requests.begin(),requests.end(),[](const auto& row){return row.name=="phase-change";});
    Check(update!=requests.end() && update->receipt.participant.name=="control" && update->state.drained);
    f.plays[0].second->complete=true;f.Tick();f.Tick();
    Check(f.controller->Status().control.sources.empty());
    Check(f.controller->Status().error.empty());
    auto invalid=document;invalid["tasks"]["attack"].erase("control");
    Reject([&]{ParseTaskCatalog(invalid.dump());});
}
}
void TestTaskCatalog(){
    Codec();SelectionAndInterrupt();AnimationOffsets();NaturalAndPending();ContextsAndFinish();CloseAndReentrant();InputRequirementsAndPreparation();ControlAdmissionAndPresentation();ControlSlotsReachDispatch();ControlAtSequenceBoundaries();
    auto input=Document();
    {
        auto poses=Document();poses["sequences"]["stand"]["entry"]={{"op","ready_pose"},{"request","ready"},{"config","Battle/A"},{"skill",8},{"variant",4}};
        auto parsed=ParseTaskCatalog(poses.dump());
        const auto& ready=std::get<ReadyPoseSelection>(std::get<TaskPoseCommand>(std::get<TaskSubmit>(parsed.sequences.at("stand")->nodes[0].operation).operation).selection);
        Check(ready.source=="Battle/A" && ready.skill==8 && ready.variant==4);
        auto progress=poses;progress["sequences"]["stand"]["entry"]={{"op","serial"},{"children",Json::array({
            poses["sequences"]["stand"]["entry"],{{"op","wait_request"},{"request","ready"},{"signal","progress"},{"progress",0.5}}})}};
        Reject([&]{ParseTaskCatalog(progress.dump());});
        poses["sequences"]["stand"]["entry"]["skill"]=-1;Reject([&]{ParseTaskCatalog(poses.dump());});
        poses["sequences"]["stand"]["entry"]={{"op","reaction"},{"request","recover"},{"config","Battle/A"},{"flag",99},{"recover",true}};
        parsed=ParseTaskCatalog(poses.dump());
        const auto& recovery=std::get<ReactionSelection>(std::get<TaskPoseCommand>(std::get<TaskSubmit>(parsed.sequences.at("stand")->nodes[0].operation).operation).selection);
        Check(recovery.flag==99 && recovery.recover && recovery.source=="Battle/A");
        poses["sequences"]["stand"]["entry"]["participant"]="official-player";Reject([&]{ParseTaskCatalog(poses.dump());});
    }
    input["sequences"]["stand"]["entry"]={{"op","serial"},{"children",Json::array({
        {{"op","visual"},{"request","turn"},{"instruction",{{"op","face_target"},{"source","LookAt"},
            {"target","AbilityTargetEntity"},{"ratio",0.5},{"angle_offset",10},{"duration",1}}}},
        {{"op","wait_request"},{"request","turn"},{"signal","accepted"}},
        {{"op","animation"},{"request","body"},{"participant","body"},{"state","Attack"}}})}};
    Fixture f(input);f.facing_ready=false;f.Select("idle");f.Tick();
    Check(f.faces.empty() && f.plays.empty());
    f.facing_ready=true;f.Tick();
    Check(f.faces.size()==1 && f.plays.size()==1 && f.faces[0].target=="AbilityTargetEntity" &&
        f.faces[0].ratio==0.5f && f.faces[0].angle_offset==10 && f.faces[0].duration==1);
    f.Tick();Check(f.faces.size()==1); // no per-frame rotation write, no artificial one-second wait
}
