#include <hksr/framework/catalog/ActionCatalog.hpp>
#include <hksr/framework/presentation/ActionSelection.hpp>
#include <hksr/framework/layout/SceneLayout.hpp>
#include <stdexcept>

namespace {
using namespace hksr;
using namespace hksr::framework;
void Check(bool yes) { if(!yes) throw std::runtime_error("action source preparation test failed"); }
api::ActionConfigInput Document(api::ActionConfigKind kind,std::string path) {
    api::ActionConfigInput out; out.kind=kind; out.path=std::move(path); out.loaded=true; return out;
}
struct PendingRead final : api::AbilitySourceRead {
    int& calls;int remaining=3;bool cancelled=false;api::AbilitySources result;
    PendingRead(int& count,const std::vector<std::string>& names):calls(count) {
        for(const auto& name:names){api::AnimationTaskSource task;task.ability=name;task.state={true,"from-"+name,{}};result.animations.push_back(task);}
    }
    void Advance(std::uint32_t budget) override {Check(budget==2000);++calls;if(remaining)--remaining;}
    void Cancel() override {cancelled=true;}
    bool Done() const override {return remaining==0;}
    api::AbilitySources Take() override {Check(Done());return cancelled?api::AbilitySources{}:std::move(result);}
};
struct ProgressiveRead final : api::ProgressiveAbilitySourceRead {
    std::vector<std::string> names;std::size_t next=0;api::CompletedAbilitySources completed;bool cancelled=false;
    explicit ProgressiveRead(std::vector<std::string> input):names(std::move(input)){}
    void Advance(std::uint32_t) override {
        if(Done())return;
        const auto name=names[next++];completed.abilities.push_back(name);
        api::AnimationTaskSource task;task.ability=name;task.state={true,"action-"+name,{}};
        completed.sources.animations.push_back(std::move(task));
    }
    void Cancel() override {cancelled=true;completed={};}
    bool Done() const override {return cancelled || next==names.size();}
    api::CompletedAbilitySources DrainCompleted() override {auto result=std::move(completed);completed={};return result;}
    api::AbilitySources Take() override {Check(Done());return std::move(completed.sources);}
};
}
void TestActionCatalog() {
    {
        using K=api::ActionConfigKind;
        ActionCatalog progressive([](auto kind,const auto& path){
            auto doc=Document(kind,path);doc.states={{"declared-"+path,"fixture"}};
            if(kind==K::Battle)doc.abilities={{path,"ability"}};return doc;
        },{},{},{},[](const auto& names){return std::make_unique<ProgressiveRead>(names);});
        const ActionSourceRoot a{ActionDomain::Alternate,{K::Battle,"a"}},b{ActionDomain::Alternate,{K::Battle,"b"}},
            exploration{ActionDomain::Exploration,{K::Adventure,"exploration"}};
        progressive.Request({a,b,exploration});
        progressive.Advance();progressive.Advance();progressive.Advance();
        const auto before=progressive.Snapshot({a,exploration});
        Check(before->Ready(ActionDomain::Exploration) && !before->Ready(ActionDomain::Alternate));
        CharacterActionCatalog catalog{1,*before};
        Check(std::get<AnimatorState>(ResolveCatalogAction(catalog,{1,ConfiguredActionKey{
            ActionDomain::Exploration,"declared-exploration"}}).request).name=="declared-exploration");
        bool rejected=false;try{ResolveCatalogAction(catalog,{1,ConfiguredActionKey{ActionDomain::Alternate,"declared-a"}});}
        catch(const std::runtime_error&){rejected=true;}Check(rejected);
        rejected=false;try{RestoreLayoutAction(SavedConfiguredAction{ActionDomain::Alternate,"declared-a"},catalog.actions);}
        catch(const std::runtime_error&){rejected=true;}Check(rejected);
        progressive.Advance();const auto first=progressive.Snapshot({a});
        Check(first->finished && !progressive.Finished() && progressive.ReadingAbilities());
        Check(first->proposals.back().state=="action-a" && !progressive.Snapshot({b})->finished);
        progressive.Advance();Check(progressive.Finished() && progressive.Snapshot({a})==first);
        CharacterActionCatalog completed{1,*first};
        const auto menu=ResolveCatalogAction(completed,{1,ConfiguredActionKey{ActionDomain::Alternate,"action-a"}});
        const auto restored=RestoreLayoutAction(SavedConfiguredAction{ActionDomain::Alternate,"action-a"},*first);
        Check(std::get<FreeStyleState>(menu.request).name==std::get<FreeStyleState>(restored->request).name);
    }
    {
        // Every source can advance while unrelated consumers retain exactly
        // their previous immutable projection. No character/action name shortcut.
        using K=api::ActionConfigKind;
        int reads=0;bool replacement=false;
        ActionCatalog many([&](auto kind,const auto& path){
            ++reads;auto value=Document(kind,path);
            value.states.push_back({replacement?"changed-"+path:path,"fixture"});
            if(path=="0" || path=="1")value.presentation.event_files={true,{"shared-event"},{}};
            if(path=="shared-event")value.referenced_event_file={true,"shared-event",{}};
            return value;
        });
        std::vector<ActionSourceRoot> roots;
        for(int i=0;i<98;++i)roots.push_back({i%2?ActionDomain::Alternate:ActionDomain::Exploration,
            {i%2?K::Battle:K::Adventure,std::to_string(i)}});
        many.Request(roots);
        std::vector<std::shared_ptr<const ActionCatalogStatus>> before;
        for(const auto& root:roots)before.push_back(many.Snapshot({root}));
        std::size_t changed=0;
        for(const auto& root:roots) {
            many.Load(root.source);
            for(std::size_t i=0;i<roots.size();++i) {
                auto after=many.Snapshot({roots[i]});
                if(after!=before[i]) {++changed;Check(roots[i].source.path==root.source.path);before[i]=after;}
            }
        }
        Check(changed==98 && reads==98 && !many.Finished());
        many.Advance();Check(many.Finished() && reads==99);
        for(std::size_t i=0;i<roots.size();++i) {
            auto after=many.Snapshot({roots[i]});
            Check((after!=before[i])==(i<2));before[i]=after;
        }
        // Reloading a cycle-shared input invalidates all its consumers, but no
        // other source. Held snapshots keep their old data and row ordering.
        replacement=true;many.Refresh({K::AnimationEvent,"shared-event"});
        Check(!many.Snapshot({roots[0]})->finished && !many.Snapshot({roots[1]})->finished);
        Check(many.Snapshot({roots[2]})==before[2]);many.Advance();
        Check(many.Snapshot({roots[0]})->proposals.back().state=="changed-shared-event");
        Check(before[0]->proposals.back().state=="shared-event" && reads==100);
        many.Request({roots[2]});Check(many.Finished() && many.Snapshot({roots[2]})==before[2]);
    }
    {
        ActionCatalog empty([](auto kind,const auto& path){return Document(kind,path);});
        empty.Request({});Check(empty.Status().finished);
        const auto revision=empty.Revision();empty.Request({});Check(empty.Revision()==revision);
    }
    {
        int linked_reads=0;
        ActionCatalog linked([&](auto kind,const auto& path){
            auto out=Document(kind,path);
            if(kind==api::ActionConfigKind::Battle) {
                out.elation_config={true,"linked-ready",{}};
                api::SkillInput skill;skill.source_index=7;skill.name={true,"ArbitrarySkill",{}};
                skill.entry_ability={true,"ArbitraryAttack",{}};out.skills.rows.push_back(skill);
            }else if(kind==api::ActionConfigKind::Elation) {
                ++linked_reads;out.elation_ready.enumerated=true;
                out.elation_ready.rows.push_back({0,{true,"ArbitrarySkill",{}},{true,"DeclaredEntry",{}},{true,"DeclaredLoop",{}}});
                out.elation_ready.rows.push_back({1,{true,"NoParentSkill",{}},{true,"IndependentPose",{}},{true,"",{}}});
                out.states={{"DeclaredEntry","ConfigList[0].ReadyAnim"},{"DeclaredLoop","ConfigList[0].ReadyLoopAnim"}};
            }
            return out;
        });
        const std::vector<ActionSourceRoot> roots{{ActionDomain::Alternate,{api::ActionConfigKind::Battle,"first"}},
            {ActionDomain::Alternate,{api::ActionConfigKind::Battle,"second"}}};
        linked.Request(roots);for(unsigned i=0;i<8 && !linked.Status().finished;++i)linked.Advance();
        const auto& result=linked.Status();Check(result.finished && linked_reads==1 && result.ready_actions.size()==2);
        for(const auto& set:result.ready_actions) {
            Check(set->poses.size()==3 && set->poses[0].state=="DeclaredEntry" && set->poses[1].state=="DeclaredLoop");
            Check(set->poses[0].skill_index==7 && set->poses[0].entry_ability.value=="ArbitraryAttack");
            Check(set->poses[2].skill_index==-1 && set->poses[2].state=="IndependentPose" && !set->poses[2].entry_ability.available);
            auto copy=*set;AppendElationReadyPoses(copy,*linked.Document({api::ActionConfigKind::Elation,"linked-ready"}));
            Check(copy.poses.size()==3); // same explicit pose is not duplicated
        }
        linked.Request(roots);linked.Advance();Check(linked_reads==1);
    }
    {
        // Initial state is an explicit official field, even when the generic
        // state collector does not repeat it. It is not hardcoded StandBy.
        ActionCatalog initial([](auto kind,const auto& path){
            auto out=Document(kind,path);out.presentation.initial_state={true,"ConfiguredRest",{}};return out;
        });
        initial.Request({{ActionDomain::Alternate,{api::ActionConfigKind::Battle,"initial-only"}}});
        initial.Advance();const auto& view=initial.Status();
        Check(view.finished && view.proposals.size()==1 && view.proposals[0].state=="ConfiguredRest");
        Check(std::get<FreeStyleState>(view.proposals[0].Request()).name=="ConfiguredRest");
    }
    // Ordered source relationships, not naming conventions or actor-specific IDs.
    api::AbilitySources cleanup;
    auto path=[](int branch,int item){return std::vector<api::ConfigStep>{
        {"Fixture","Branches",api::ConfigSelection::Index,branch,{}},
        {"RPG.GameCore.PredicateTaskList","SuccessTaskList",api::ConfigSelection::Index,item,{}}};};
    api::AnimationTaskSource begin;begin.ability="root";begin.path=path(0,6);
    begin.state={true,"AnyName",{}};begin.target_alias={true,"Caster",{}};begin.enabled={true,true,{}};
    cleanup.animations.push_back(begin);
    cleanup.waits.push_back({"root",path(0,7),begin.state,begin.target_alias,{true,true,{}}});
    api::EffectRemovalSource removal;removal.ability="root";removal.path=path(0,15);
    auto& r=removal.input;r.effect_path={true,"Effect/FromConfig",{}};r.path_key={true,"",{}};
    r.target_alias=begin.target_alias;r.enabled={true,true,{}};
    cleanup.removals.push_back(removal);
    Check(LinkActionRetirements(cleanup).size()==1);
    cleanup.removals[0].path=path(1,15);Check(LinkActionRetirements(cleanup).empty()); // sibling branch
    cleanup.removals[0]=removal;cleanup.waits[0].state.value="Other";Check(LinkActionRetirements(cleanup).empty());
    cleanup.waits[0].state=begin.state;begin.path=path(0,12);begin.target_alias.value="Target";
    cleanup.animations.push_back(begin);Check(LinkActionRetirements(cleanup).empty()); // later non-self boundary
    cleanup.animations.pop_back();cleanup.removals[0].input.path_key.value="DynamicKey";Check(LinkActionRetirements(cleanup).empty());
    cleanup.removals[0]=removal;
    using K=api::ActionConfigKind;
    std::map<ActionSourceKey,int> counts;
    bool missing=false;
    auto read=[&](K kind,const std::string& path) {
        ++counts[{kind,path}]; auto out=Document(kind,path);
        if(path=="player" || path=="battle") {
            out.states={{"DefaultCustom","InitAnimStateName"}};
            out.presentation.event_files={true,{"shared","shared","missing"},{}};
            if(path=="battle") {
                out.states.push_back({"ConditionalReady","SkillList[7].CustomReadyConfigs[2].ReadyAnimState"});
                out.abilities.push_back({"AutomaticAbility","SkillAbilityList[5].AbilityList[3]"});
                out.abilities.push_back({"BrokenAbility","SkillAbilityList[5].AbilityList[4]"});
            }
        } else if(path=="shared") {
            out.states={{"IdleCustom","AnimatorStateEvents[0].AnimatorStateName"},{"IdleCustom","AnimatorStateEvents[2].AnimatorStateName"}};
            out.referenced_event_file={true,"cycle",{}};
        } else if(path=="cycle") {
            out.referenced_event_file={true,"shared",{}};
            out.issues.push_back({2,"States.Name","one field unavailable"});
            out.states={{"PartialCustom","AnimatorStateGroupEvents[3].States[0].Name"}};
        } else if(path=="missing" && !missing) throw std::runtime_error("not currently loaded");
        else if(path=="missing") out.states={{"RecoveredCustom","AnimatorStateEvents[0].AnimatorStateName"}};
        return out;
    };
    int ability_reads=0;
    ActionCatalog catalog(read,[&](const std::vector<std::string>& abilities) {
        ++ability_reads; api::AbilitySources result;
        Check(abilities==std::vector<std::string>({"AutomaticAbility","BrokenAbility"}));
        api::AnimationTaskSource task; task.ability=abilities[0]; task.state={true,"Skill_From_Task",{}};
        task.enabled={true,false,{}}; result.animations.push_back(task);
        result.issues.push_back({"BrokenAbility","lookup unavailable",{}}); return result;
    });
    const std::vector<ActionSourceRoot> requests={{ActionDomain::Exploration,{K::Adventure,"player"}},{ActionDomain::Alternate,{K::Battle,"battle"}}};
    catalog.Request(requests); Check(!catalog.Status().finished && counts.empty());
    catalog.Advance(); catalog.Advance(); catalog.Advance(); catalog.Advance(); catalog.Advance();
    Check(!catalog.Status().finished && counts.size()==5 && ability_reads==0);
    catalog.Advance(); Check(catalog.Status().finished && ability_reads==1 && catalog.Status().proposals.size()==8);
    const auto exploration_view=catalog.Snapshot({requests[0]});
    const auto battle_view=catalog.Snapshot({requests[1]});
    catalog.RefreshAbility("BrokenAbility");
    Check(catalog.Snapshot({requests[0]})==exploration_view && catalog.Snapshot({requests[1]})!=battle_view);
    // Refresh the other root too; the fixture returns one joint batch.
    catalog.RefreshAbility("AutomaticAbility");catalog.Advance();Check(ability_reads==2);
    Check(catalog.Status().abilities.size()==2 && catalog.Status().abilities[0].attempted && catalog.Status().abilities[1].issues.size()==1);
    const auto task_entry=std::find_if(catalog.Status().proposals.begin(),catalog.Status().proposals.end(),[](const auto& p) { return p.state=="Skill_From_Task"; });
    Check(task_entry!=catalog.Status().proposals.end() && task_entry->references[0].task.has_value() && !task_entry->references[0].task->enabled.value);
    Check(counts.at({K::AnimationEvent,"shared"})==1 && counts.at({K::AnimationEvent,"missing"})==1);
    const auto& proposals=catalog.Status().proposals;
    const auto idle=std::find_if(proposals.begin(),proposals.end(),[](const auto& p) { return p.state=="IdleCustom" && p.domain==ActionDomain::Exploration; });
    Check(idle!=proposals.end() && idle->references.size()==2 && std::get<AnimatorState>(idle->Request()).name=="IdleCustom");
    const auto ready=std::find_if(proposals.begin(),proposals.end(),[](const auto& p) { return p.state=="ConditionalReady"; });
    Check(ready!=proposals.end() && std::get<FreeStyleState>(ready->Request()).name=="ConditionalReady");
    Check(catalog.Document({K::Battle,"battle"})->abilities[0].value=="AutomaticAbility");
    catalog.Request(requests); catalog.Advance(); Check(counts.at({K::Adventure,"player"})==1 && counts.at({K::AnimationEvent,"missing"})==1);
    missing=true; catalog.Refresh({K::AnimationEvent,"missing"}); Check(!catalog.Status().finished);
    catalog.Advance(); Check(catalog.Status().finished && catalog.Status().proposals.size()==10 && counts.at({K::AnimationEvent,"missing"})==2 && ability_reads==2);
    ActionCatalog* reentrant=nullptr;
    ActionCatalog changed([&](K kind,const std::string& path) {
        if(path=="old") reentrant->Request({{ActionDomain::Alternate,{K::Battle,"new"}}});
        auto out=Document(kind,path); out.states={{path,"InitAnimStateName"}}; return out;
    }); reentrant=&changed;
    changed.Request({{ActionDomain::Exploration,{K::Adventure,"old"}}}); changed.Advance();
    Check(changed.Status().proposals.empty() && !changed.Status().finished);
    changed.Advance(); Check(changed.Status().finished && changed.Status().proposals[0].state=="new");
    ActionCatalog wrong([](K,const auto&) { return Document(K::Battle,"wrong"); });
    wrong.Request({{ActionDomain::Alternate,{K::Battle,"requested"}}}); wrong.Advance();
    Check(wrong.Status().finished && !wrong.Status().documents[0].loaded && wrong.Status().documents[0].issues.size()==1);
    int visual_reads=0;
    ActionCatalog visual_catalog([](K kind,const auto& path){return Document(kind,path);},{},[&](const std::string& path){
        ++visual_reads;api::VisualConfigSources out;out.character_config=path;out.context_available=true;
        out.defaults.source="global";out.character.source=path;
        out.defaults.loaded=true;out.defaults.behaviors.enumerated=true;out.expression_transition={true,0.2f,{}};
        out.blink_suppressed_expression={true,"FaceFromGlobal",{}};out.blink_source={true,3,{}};
        api::VisualBehaviorInput one;one.flag={true,6,{}};one.occur.resolved_state={true,"SharedBody",{}};one.emotion={true,"",{}};
        out.defaults.behaviors.rows.push_back(one);
        one.flag.value=133;one.emotion.value="FaceFromGlobal";out.defaults.behaviors.rows.push_back(one);
        one.emotion.value="FaceFromCharacter";out.character.behaviors.rows.push_back(one);return out;
    });
    visual_catalog.Request({{ActionDomain::Alternate,{K::Battle,"custom"}}});visual_catalog.Advance();
    Check(!visual_catalog.Status().finished && visual_reads==0);
    visual_catalog.Advance();Check(visual_catalog.Status().finished && visual_reads==1);
    const auto& visual_states=visual_catalog.Status().visual_states;
    Check(visual_states.size()==2 && visual_states[0].source=="global" && visual_states[0].value.emotion.value.empty());
    Check(visual_states[1].source=="custom" && visual_states[1].value.emotion.value=="FaceFromCharacter");
    Check(visual_states[1].expression.value=="FaceFromGlobal" && visual_states[1].expression_transition.value==0.2f);
    Check(visual_states[0].blink_source==-1 && visual_states[1].blink_source==3);
    Check(visual_states[0].character_config=="custom" && visual_states[1].character_config=="custom");
    visual_catalog.Request({{ActionDomain::Alternate,{K::Battle,"custom"}}});visual_catalog.Advance();Check(visual_reads==1);
    visual_catalog.Refresh({K::Battle,"custom"});visual_catalog.Advance();visual_catalog.Advance();Check(visual_reads==2);
    int default_reads=0;
    ActionCatalog prepared([](K kind,const auto& path){
        auto out=Document(kind,path);api::SkillInput row;row.source_index=7;row.ready_state={true,"ReadyFromConfig",{}};
        out.skills.rows.push_back(row);out.transitions.enumerated=true;return out;
    },{},{},[&]{++default_reads;return api::ReadyAnimationDefaults{};});
    prepared.Request({{ActionDomain::Alternate,{K::Battle,"first"}}});prepared.Advance();
    Check(!prepared.Status().finished && prepared.Status().ready_actions.size()==1 && default_reads==0);
    prepared.Advance();Check(prepared.Status().finished && default_reads==1);
    auto retained=prepared.Status().ready_actions.front();
    prepared.Request({{ActionDomain::Alternate,{K::Battle,"second"}}});prepared.Advance();
    Check(default_reads==1 && retained->source=="first" && prepared.Status().ready_actions.front()->source=="second");
    Check(prepared.Status().ready_actions.front()->poses[0].skill_index==7); // no filtered-index renumbering
    int begins=0,steps=0;
    ActionCatalog sliced([](K kind,const auto& path){
        auto out=Document(kind,path);out.abilities={{path,"AbilityList[0]"}};return out;
    },{},{},{},[&](const auto& names){++begins;return std::make_unique<PendingRead>(steps,names);});
    sliced.Request({{ActionDomain::Alternate,{K::Battle,"old-batch"}}});sliced.Advance();sliced.Advance();
    Check(begins==1 && steps==1 && !sliced.Status().finished && sliced.Status().proposals.empty());
    const auto slice_revision=sliced.Revision();
    sliced.Request({{ActionDomain::Alternate,{K::Battle,"old-batch"}}, {ActionDomain::Alternate,{K::Battle,"old-batch"}}});
    Check(sliced.Revision()==slice_revision); // duplicate source ownership adds no work
    // Replacing selection cannot publish stale proposals. The completed old
    // batch is still useful cache; the new selection gets its own batch next.
    sliced.Request({{ActionDomain::Alternate,{K::Battle,"new-batch"}}});
    sliced.Advance();sliced.Advance();Check(sliced.Status().proposals.empty());
    sliced.Advance();sliced.Advance();sliced.Advance();sliced.Advance();
    Check(begins==2 && steps==6 && sliced.Status().finished && sliced.Status().proposals.size()==1 &&
        sliced.Status().proposals[0].state=="from-new-batch");
    sliced.Request({{ActionDomain::Alternate,{K::Battle,"old-batch"}}});
    Check(sliced.Status().finished && begins==2 && sliced.Status().proposals[0].state=="from-old-batch");
    sliced.RefreshAbility("old-batch");sliced.Advance();Check(begins==3);
    sliced.RefreshAbility("old-batch"); // discard stale in-flight retry result
    sliced.Advance();sliced.Advance();Check(!sliced.Status().finished && sliced.Status().proposals.empty());
    sliced.Advance();sliced.Advance();sliced.Advance();Check(begins==4 && sliced.Status().finished);
    sliced.RefreshAbility("old-batch");sliced.Advance();
    Check(!sliced.CancelPending());Check(sliced.CancelPending());
    Check(!sliced.ReadingAbilities() && sliced.Status().proposals.empty());
    int linked_reads=0;
    ActionCatalog linked([](K kind,const auto& source){
        auto doc=Document(kind,source);doc.abilities={{"entry", "SkillList[8].EntryAbility"}};
        doc.presentation.initial_state={true,"RestFromConfig",{}};api::SkillInput skill;skill.source_index=8;
        skill.ready_state={true,"Pose",{}};skill.entry_ability={true,"entry",{}};doc.skills.rows.push_back(skill);return doc;
    },[&](const auto& names){
        ++linked_reads;api::AbilitySources out;
        for(const auto& name:names) {
            out.references.push_back({name,{}, {true,name=="entry"?"branch":"entry",{}},{true,true,{}}});
            if(name=="branch")for(const auto* state:{"RestFromConfig","AttackVariantA","AttackVariantB"}) {
                api::AnimationTaskSource task;task.ability=name;task.state={true,state,{}};task.enabled={true,true,{}};
                task.target_alias={true,"Caster",{}};out.animations.push_back(task);
            }
        }return out;
    });
    linked.Request({{ActionDomain::Alternate,{K::Battle,"source"}}});linked.Advance();linked.Advance();linked.Advance();
    Check(linked.Status().finished && linked_reads==2 && linked.Status().abilities.size()==2);
    const auto& ready_set=*linked.Status().ready_actions.front();
    const auto attacks=ReadyAttacks(linked.Status(),ready_set,ready_set.poses.front());
    Check(attacks.size()==2 && attacks[0]->state=="AttackVariantA" && attacks[1]->state=="AttackVariantB");
    Check(attacks[0]->references[0].root_ability=="entry" && attacks[0]->references[0].task->ability=="branch");
    // A parent can have no EntryAbility and branch to children without any
    // ReadyAnimState. Include all explicit branches, not a single guessed one.
    api::ActionConfigInput children;children.path="branch-source";
    api::SkillInput parent;parent.name={true,"Ultimate",{}};parent.ready_state={true,"Prepare",{}};
    parent.child_skills={true,{"Single","Area","Missing","Ambiguous"},{}};
    api::SkillInput single;single.name={true,"Single",{}};single.entry_ability={true,"one",{}};
    single.child_skills={true,{"Ultimate"},{}}; // cycle back to the pose
    api::SkillInput area;area.name={true,"Area",{}};area.child_skills={true,{"Nested"},{}};
    api::SkillInput nested;nested.name={true,"Nested",{}};nested.entry_ability={true,"many",{}};
    api::SkillInput ambiguous;ambiguous.name={true,"Ambiguous",{}};ambiguous.entry_ability={true,"wrong",{}};
    children.skills.rows={parent,single,area,nested,ambiguous,ambiguous};
    const auto branches=BuildReadyActions(children,{});Check(branches.poses.size()==1 && branches.skills.size()==6);
    ActionCatalogStatus candidates;
    auto candidate=[&](const char* source,const char* root,const char* name,const char* target="Caster",bool enabled=true) {
        ActionProposal p;p.domain=ActionDomain::Alternate;p.state=name;
        api::AnimationTaskSource task;task.enabled={true,enabled,{}};task.target_alias={true,target,{}};
        p.references.push_back({{K::Battle,source},{},task,root});candidates.proposals.push_back(std::move(p));
    };
    candidate("branch-source","one","SingleAttack");candidate("branch-source","many","AreaAttack");
    candidates.proposals[1].references[0].task->kind=api::AnimationTaskKind::StateWithTargetList;
    candidate("branch-source","wrong","AmbiguousAttack");candidate("other-source","one","Foreign");
    candidate("branch-source","many","TargetOnly","Target");candidate("branch-source","many","Disabled","Caster",false);
    const auto choices=ReadyAttacks(candidates,branches,branches.poses.front());
    Check(choices.size()==2 && choices[0]->state=="SingleAttack" && choices[1]->state=="AreaAttack");
    auto no_children=branches.poses.front();no_children.child_skills.available=false;
    Check(ReadyAttacks(candidates,branches,no_children).empty());
}
