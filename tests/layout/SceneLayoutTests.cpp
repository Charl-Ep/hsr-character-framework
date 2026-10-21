#include <hksr/framework/layout/SceneLayout.hpp>
#include <hksr/presentation/PresentationBindings.hpp>
#include <hksr/framework/character/SceneCharacters.hpp>
#include <hksr/framework/host/RuntimeServices.hpp>
#include <hksr/framework/presentation/VisualProgramRuntime.hpp>
#include <hksr/framework/tasks/TaskCatalog.hpp>
#include <Windows.h>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool b){if(!b)throw std::runtime_error("scene layout test failed");}
template<class F> void Reject(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}Check(rejected);}
struct NoObjects final:hksr::api::ManagedObjects {
    hksr::api::ObjectResult NewStringUtf8(const char*) override {throw std::runtime_error("unexpected game call");}
    hksr::api::ObjectResult AllocateObject(const char*,const char*) override {throw std::runtime_error("unexpected game call");}
    hksr::api::ObjectResult ManagedType(const char*,const char*) override {throw std::runtime_error("unexpected game call");}
};
}
void TestSceneLayout() {
    using namespace hksr::framework;
    SceneLayout data;data.scene="hsr-map-v1:1:2";
    SavedCharacter first;first.key=17;first.source=BrowseNpc{1110};
    first.placement={{1,2,3},{0,0,0,1}};first.action=NpcMotion{123};
    SavedCharacter second;second.key=29;second.source=BrowsePlayer{{1217,0,0},1217};
    second.placement={{4,5,6},{0,0,0,1}};second.enabled=false;
    second.look={LookMode::Instance,17,{}};
    second.look.max_distance=12;
    second.action=SavedReadyAction{"official/config","Ready","Skill02",-1};
    second.presentation_phase="authored-phase";
    data.characters={first,second};
    const auto bytes=EncodeSceneLayout(data);const auto parsed=DecodeSceneLayout(bytes);
    Check(EncodeSceneLayout(parsed)==bytes && parsed.characters[1].look.target==17 &&
        !parsed.characters[1].enabled && std::get<BrowsePlayer>(parsed.characters[1].source).npc==1217);
    Check(parsed.characters[1].presentation_phase=="authored-phase");
    Check(parsed.characters[1].look.max_distance==12);
    auto different=parsed;different.characters[1].presentation_phase.clear();Check(!SameLayout(parsed,different));
    auto visual=parsed;
    VisualContextSnapshot context;context.name="phase";context.inputs.targets["Target"]=17;
    context.inputs.choices["variant"]="left";context.variables["progress"]=0.75f;
    context.inputs.anchors["origin"]={{1,2,3},{0,0,0,1}};
    visual.characters[1].visual_state=VisualProgramSnapshot{"program",{}, {context}};
    Check(!SameLayout(parsed,visual) && SameLayout(visual,DecodeSceneLayout(EncodeSceneLayout(visual))));
    auto changed_visual=visual;changed_visual.characters[1].visual_state->contexts[0].variables["progress"]=1;
    Check(!SameLayout(visual,changed_visual));
    changed_visual=visual;changed_visual.characters[1].visual_state->contexts[0].inputs.targets["Target"]=999;
    Reject([&]{EncodeSceneLayout(changed_visual);});
    changed_visual=visual;changed_visual.characters[1].visual_state->contexts.push_back(context);Reject([&]{EncodeSceneLayout(changed_visual);});
    changed_visual=visual;changed_visual.characters[1].visual_state->contexts[0].variables["progress"]=std::numeric_limits<float>::infinity();
    Reject([&]{EncodeSceneLayout(changed_visual);});
    auto idle=visual;idle.characters[1].action=BattleIdle{};
    Check(SameLayout(idle,DecodeSceneLayout(EncodeSceneLayout(idle))));
    Check(std::holds_alternative<BattleIdle>(RestoreLayoutAction(idle.characters[1].action,{})->request));
    auto remapped=*visual.characters[1].visual_state;RemapVisualTargets(remapped,{{17,100},{29,200}});
    Check(remapped.contexts[0].inputs.targets.at("Target")==100 && visual.characters[1].visual_state->contexts[0].inputs.targets.at("Target")==17);
    auto grounded=parsed;grounded.characters[0].placement.snap_to_ground=false;grounded.characters[1].placement.snap_to_ground=true;
    Check(!SameLayout(parsed,grounded) && SameLayout(grounded,DecodeSceneLayout(EncodeSceneLayout(grounded))));
    auto bad=data;bad.characters[1].key=17;Reject([&]{EncodeSceneLayout(bad);});
    bad=data;bad.characters[1].look.target=99;Reject([&]{EncodeSceneLayout(bad);});
    bad=data;bad.characters[0].placement.position.x=std::numeric_limits<float>::infinity();Reject([&]{EncodeSceneLayout(bad);});
    bad=data;bad.characters[0].placement.rotation.w=0;Reject([&]{EncodeSceneLayout(bad);});
    Reject([&]{DecodeSceneLayout("{broken");});
    // JSON arithmetic conversions must not silently truncate/wrap identity.
    for(const auto* variant:{"1.5","4294967295","18446744073709551615","true"}) {
        auto malformed=bytes;const auto at=malformed.find("\"variant\": -1");
        Check(at!=std::string::npos);malformed.replace(at,13,std::string("\"variant\": ")+variant);
        Reject([&]{DecodeSceneLayout(malformed);});
    }

    ActionCatalogStatus catalog;
    catalog.proposals.push_back({ActionDomain::Alternate,"Idle",{},{}});
    auto ready=std::make_shared<ReadyActions>();ready->source="official/config";
    ReadyPose other;other.state="Other";other.skill_name="Skill01";ready->poses.push_back(other);
    ReadyPose pose;pose.state="Ready";pose.skill_name="Skill02";ready->poses.push_back(pose);
    catalog.ready_actions.push_back(ready);
    VisualState reaction;reaction.character_config="official/config";reaction.source="global";reaction.value.flag={true,28,{}};
    reaction.value.occur.resolved_state={true,"Stagger",{}};reaction.value.loop.resolved_state={true,"Stagger_Loop",{}};
    reaction.value.recover.resolved_state={true,"Stagger_Recover",{}};
    reaction.value.occur.transition=reaction.value.loop.transition=reaction.value.recover.transition={true,0.15f,{}};
    reaction.expression={true,"Emo_Stun",{}};reaction.expression_transition={true,0.2f,{}};
    auto no_face=reaction;no_face.value.flag.value=6;no_face.expression.value.clear();
    catalog.visual_states={reaction,no_face};
    const VisualActionKey enter_key{"official/config",28,false},recover_key{"official/config",28,true};
    auto reacting=data;reacting.characters[1].action=enter_key;
    Check(SameLayout(reacting,DecodeSceneLayout(EncodeSceneLayout(reacting))));
    auto recovering=reacting;recovering.characters[1].action=recover_key;Check(!SameLayout(reacting,recovering));
    Check(std::get<VisualState>(RestoreLayoutAction(recover_key,catalog)->request).recover);
    // Shared body names never merge different official flags/expressions.
    Check(ResolveVisualAction(catalog,enter_key).expression.value=="Emo_Stun");
    Check(ResolveVisualAction(catalog,{"official/config",6,false}).expression.value.empty());
    Reject([&]{ResolveVisualAction(catalog,{"another/config",28,false});});
    auto duplicate=catalog;duplicate.visual_states.push_back(reaction);Reject([&]{ResolveVisualAction(duplicate,enter_key);});
    auto incomplete=reaction;incomplete.value.loop.resolved_state.available=false;
    Check(!QueryVisualAction(incomplete,false).available && QueryVisualAction(incomplete,true).available);
    incomplete=reaction;incomplete.value.occur.resolved_state.value.clear();incomplete.value.loop.resolved_state.value.clear();
    Check(!QueryVisualAction(incomplete,false).available); // material-only is not a fake playable animation
    // A complete reaction source doesn't depend on the rest of the skill graph.
    auto reaction_pending=catalog;reaction_pending.pending_domains.insert(ActionDomain::Alternate);
    CharacterActionCatalog reaction_view{37,reaction_pending};
    Check(std::get<VisualState>(ResolveCatalogAction(reaction_view,{37,enter_key}).request).expression.value=="Emo_Stun");
    Reject([&]{ResolveCatalogAction(reaction_view,{36,enter_key});});
    Check(std::get<VisualState>(RestoreLayoutAction(enter_key,reaction_pending)->request).value.flag.value==28);
    hksr::api::VisualConfigSources read_source;read_source.character_config="official/config";
    reaction_pending.visual_sources.push_back(read_source);
    Check(VisualActionSourceReady(reaction_pending,"official/config") && !VisualActionSourceReady(reaction_pending,"other"));
    auto restored=RestoreLayoutAction(data.characters[1].action,catalog);
    Check(std::get<ReadyState>(restored->request).pose==1); // not a persisted old array index
    ready->poses.push_back(pose);Reject([&]{RestoreLayoutAction(data.characters[1].action,catalog);});ready->poses.pop_back();
    restored=RestoreLayoutAction(SavedConfiguredAction{ActionDomain::Alternate,"Idle"},catalog);
    Check(std::get<FreeStyleState>(restored->request).name=="Idle");
    Reject([&]{RestoreLayoutAction(SavedConfiguredAction{ActionDomain::Alternate,"Missing"},catalog);});
    auto observed=data;observed.characters[0].action=SavedConfiguredAction{ActionDomain::Alternate,"Base.OtherPhaseIdle",true};
    const auto observed_read=DecodeSceneLayout(EncodeSceneLayout(observed));
    Check(SameLayout(observed,observed_read));
    restored=RestoreLayoutAction(observed_read.characters[0].action,catalog);
    Check(std::get<FreeStyleState>(restored->request).controller_observed && std::get<FreeStyleState>(restored->request).name=="Base.OtherPhaseIdle");
    auto authored=data;authored.characters[0].action=ProgramAction{"fixture","second",{{"branch","b"}}};
    const auto authored_read=DecodeSceneLayout(EncodeSceneLayout(authored));Check(SameLayout(authored,authored_read));
    ActionCatalogStatus pending_catalog;pending_catalog.pending_domains.insert(ActionDomain::Alternate);
    restored=RestoreLayoutAction(authored_read.characters[0].action,pending_catalog);
    Check(restored->domain==ActionDomain::Alternate && std::get<ProgramAction>(restored->request).action=="second");
    Check(std::get<ProgramAction>(restored->request).choices.at("branch")=="b");
    auto another=authored;std::get<ProgramAction>(*another.characters[0].action).action="first";Check(!SameLayout(authored,another));
    another=authored;std::get<ProgramAction>(*another.characters[0].action).choices["branch"]="a";Check(!SameLayout(authored,another));

    NoObjects objects;hksr::api::PresentationBindings bindings;int info=0;
    const auto retain=+[](void*,int,void*)->int{throw std::runtime_error("unexpected game retain");};
    const auto target=+[](int,void*)->void*{throw std::runtime_error("unexpected game target");};
    const auto release=+[](int,void*){throw std::runtime_error("unexpected game free");};
    bindings.references={{reinterpret_cast<void*>(retain),&info},{reinterpret_cast<void*>(target),&info},
        {reinterpret_cast<void*>(release),&info},{},{}};
    RuntimeServices runtime(objects,bindings);
    SceneCharacters scene(runtime.Sessions(false),SceneCharacters::Create{});
    CharacterPlan plan;plan.package.npc_id=1110;plan.source_intent=BrowseNpc{1110};
    auto id=scene.Add(data.scene,plan,PlacedAction{ActionDomain::Exploration,NpcMotion{123}},first.placement);
    Check(CaptureSceneLayout(scene,data.scene).characters.size()==1);
    scene.Select(id,{ActionDomain::Exploration,AnimatorState{"IdleShow",0,0}});
    Check(std::get<SavedConfiguredAction>(*CaptureSceneLayout(scene,data.scene).characters[0].action).state=="IdleShow");
    scene.Select(id,{ActionDomain::Alternate,reaction});
    const auto reaction_layout=CaptureSceneLayout(scene,data.scene);
    Check(std::get<VisualActionKey>(*reaction_layout.characters[0].action).flag==28);
    Check(SameLayout(reaction_layout,DecodeSceneLayout(EncodeSceneLayout(reaction_layout))));
    auto program=std::make_shared<VisualProgram>();program->id="fixture";
    program->actions["first"].state=program->actions["second"].state="SameBody";
    auto input=std::make_shared<VisualProgramInput>();input->program=program;plan.visual_program=input;
    scene.ReplacePlan(id,plan);
    scene.Select(id,*restored);
    Check(scene.Status(id).program_actions.size()==2 && scene.Status(id).visual_program=="fixture");
    Check(std::get<ProgramAction>(*CaptureSceneLayout(scene,data.scene).characters[0].action).action=="second");
    Reject([&]{scene.Select(id,{ActionDomain::Alternate,ProgramAction{"foreign","first"}});});
    Reject([&]{scene.Select(id,{ActionDomain::Alternate,ProgramAction{"fixture","absent"}});});
    Reject([&]{scene.Select(id,{ActionDomain::Exploration,ProgramAction{"fixture","first"}});});
    Check(std::get<ProgramAction>(scene.DesiredAction(id)->request).action=="second");
    // Explicit task catalogs cannot override the automatic scene of this source.
    auto tasks=std::make_shared<TaskCatalog>(ParseTaskCatalog(R"({
      "format":"hksr.task-catalog","id":"tasks","domain":"battle",
      "visual":{"id":"task-scene","source":"official/config","scope":"test","effects":{}},
      "sequences":{"idle":{"entry":{"op":"animation","request":"body","participant":"body","state":"StandBy"}}},
      "tasks":{"idle":{"entry":"idle"}}})"));
    auto task_input=std::make_shared<VisualProgramInput>();task_input->tasks=tasks;task_input->program=tasks->visual;
    scene.ConfigurePrograms({{{"official/config",input}},{{"tasks",task_input}}});
    plan.visual_program.reset();
    plan.action_sources.push_back({ActionDomain::Alternate,{hksr::api::ActionConfigKind::Battle,"official/config"}});
    auto automatic=plan;scene.ReplacePlan(id,automatic);
    Check(scene.Plan(id).visual_program->program==input->program && !scene.Plan(id).visual_program->tasks);
    scene.ConfigureTaskPreview(plan,"tasks");
    Check(plan.visual_program->tasks==tasks && plan.persistence==CharacterPersistence::Session);
    auto foreign=automatic;foreign.action_sources.clear();
    Reject([&]{scene.ConfigureTaskPreview(foreign,"tasks");});
    Reject([&]{scene.ConfigureTaskPreview(foreign,"absent");});
    Check(!foreign.visual_program && foreign.persistence==CharacterPersistence::Layout);
    // Omission precedes source/action/context serialization, including unsupported
    // development-only values. The rest of the map still saves without error.
    plan.source_intent.reset();plan.package.source=SourceKind::External;
    const auto before=EncodeSceneLayout(CaptureSceneLayout(scene,data.scene));
    const auto transient=scene.Add(data.scene,plan,PlacedAction{ActionDomain::Alternate,AnimatorState{"Face",1,0}});
    Check(scene.Status(transient).transient && !scene.Status(id).transient);
    Check(EncodeSceneLayout(CaptureSceneLayout(scene,data.scene))==before && !scene.CaptureVisualProgram(transient));
    Reject([&]{scene.Look(id,{LookMode::Instance,transient,{}});});
    Reject([&]{scene.Plan(id).visual_program->resolve_character(transient);});
    Check(EncodeSceneLayout(CaptureSceneLayout(scene,data.scene))==before);
    scene.ReplacePlan(transient,automatic);
    Check(scene.Plan(transient).persistence==CharacterPersistence::Session);
    Check(EncodeSceneLayout(CaptureSceneLayout(scene,data.scene))==before);
    scene.Delete(transient);
    scene.SetEnabled(id,false);Check(!CaptureSceneLayout(scene,data.scene).characters[0].enabled);
    scene.Delete(id);Check(CaptureSceneLayout(scene,data.scene).characters.empty());
    scene.Shutdown();

    const auto folder=std::filesystem::temp_directory_path()/
        ("hksr-layout-test-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove_all(path,error);}} cleanup{folder};
    LayoutStorage store(folder);
    Check(!store.Current(data.scene));store.SaveCurrent(data);
    Check(EncodeSceneLayout(*store.Current(data.scene))==bytes);
    store.SaveNamed("布局一",data);Check(store.Names(data.scene)==std::vector<std::string>{"布局一"});
    Reject([&]{store.SaveNamed("布局一",data);});
    Check(EncodeSceneLayout(store.Named(data.scene,"布局一"))==bytes); // rejected overwrite preserved prior data
    Reject([&]{store.SaveNamed("../escape",data);});
    auto changed=data;changed.characters[0].placement.position.x=99;
    store.SaveCurrent(changed);Check(store.Current(data.scene)->characters[0].placement.position.x==99);
    Check(store.Named(data.scene,"布局一").characters[0].placement.position.x==1);
    store.SaveNamed("布局一",changed,true);Check(store.Named(data.scene,"布局一").characters[0].placement.position.x==99);
    store.DeleteNamed(data.scene,"布局一");Check(store.Names(data.scene).empty());
    Check(!store.Current("hsr-map-v1/1/2")); // distinct map keys cannot alias through filename escaping
}
