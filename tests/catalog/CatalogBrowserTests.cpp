#include <hksr/framework/catalog/CatalogBrowser.hpp>
#include <stdexcept>
#include <iostream>
namespace {
namespace api=hksr::api;
using namespace hksr::framework;
void CheckAt(bool condition,int line) {if(!condition) throw std::runtime_error("catalog browser test failed at line "+std::to_string(line));}
#define Check(...) CheckAt((__VA_ARGS__),__LINE__)
void Finish(CatalogBrowser& browser) {
    for(unsigned i=0;i!=100;++i) {
        browser.Advance();const auto out=browser.Snapshot();
        bool finished=out->attempted || !out->requested;
        for(const auto& entry:out->sources)finished=finished && !entry.second->Pending() &&
            (entry.second->phase!=SourceRequestPhase::Ready || entry.second->actions.finished);
        for(const auto& entry:out->characters)finished=finished && entry.second->actions.finished;
        if(finished) return;
    }
    throw std::runtime_error("browser test did not finish");
}
struct SlicedRead final : api::AbilitySourceRead {
    int& steps;bool cancelled=false;int remaining=3;
    explicit SlicedRead(int& count):steps(count){}
    void Advance(std::uint32_t budget) override {Check(budget==2000);++steps;if(remaining)--remaining;}
    void Cancel() override {cancelled=true;}
    bool Done() const override {return !remaining;}
    api::AbilitySources Take() override {Check(Done());return {};}
};
}
void TestCatalogBrowser() {
    api::CharacterCatalog rows;rows.npcs.enumerated=rows.players.enumerated=rows.avatars.enumerated=true;
    rows.npcs.rows={{91,"Npc/A","Model/A"}};
    rows.players.rows={{11,91,0,0,"Player/A","Player.prefab"},{11,91,0,1,"Player/Skin","Skin.prefab"}};
    rows.avatars.rows={{91,"Battle/A","Battle.prefab"}};
    int table_reads=0,identity_reads=0,ability_reads=0,npc_reads=0,ready_reads=0,pose_visual_reads=0;
    std::map<std::string,int> document_reads;
    bool broken_battle=false,broken_tables=false;
    CatalogReaders readers{
        [&]{++table_reads;if(broken_tables) throw std::runtime_error("table fail");return rows;},
        [&](const auto&){++identity_reads;return api::TextInput{true,"A",{}};},
        [&](auto kind,const std::string& path) {
            ++document_reads[path];
            if(broken_battle && path=="Battle/A") throw std::runtime_error("battle fail");
            api::ActionConfigInput out;out.path=path;out.kind=kind;out.loaded=true;
            out.presentation={path,{true,"StandBy",{}},{true,{"Event/Shared"},{}},{true,{}, {}}};
            out.states={{path=="Battle/A"?"SkillReady":"StandBy","declared field"}};
            if(kind==api::ActionConfigKind::Battle) {
                api::SkillInput skill;skill.source_index=0;skill.ready_state={true,"SkillReady",{}};
                out.skills.rows.push_back(skill);
            }
            if(kind==api::ActionConfigKind::AnimationEvent) {
                out.referenced_event_file={true,path,{}}; // cycle must stop
                out.abilities={{"ability/A","task"}};
            }
            return out;
        },
        [&](const auto& keys){++ability_reads;api::AbilitySources out;for(const auto& key:keys) {api::AnimationTaskSource source;source.ability=key;source.state={true,"Attack",{}};out.animations.push_back(source);}return out;},
        [&](void* component){Check(component==reinterpret_cast<void*>(123));++npc_reads;api::NpcMotionSources out;api::NpcMotionRow row;row.id=42;out.motions.push_back(row);return out;}};
    readers.ready_defaults=[&] {
        ++ready_reads;api::ReadyAnimationDefaults defaults;
        defaults.default_expression={true,"FaceDefault",{}};
        defaults.hit_additive_layer={true,"HitAdditive",{}};
        defaults.hit_override_layer={true,"HitOverride",{}};
        defaults.empty_layer_state={true,"Empty",{}};
        defaults.transition_in={true,0.17f,{}};
        api::ReadyTransitionInput edge;edge.from={true,1,{}};edge.to={true,2,{}};
        edge.state={true,"DefaultReadyEdge",{}};defaults.transitions.push_back(edge);
        return defaults;
    };
    readers.visuals=[&](const auto& source) {
        ++pose_visual_reads;api::VisualConfigSources out;out.character_config=source;out.context_available=true;
        out.defaults.loaded=true;out.defaults.behaviors.enumerated=true;out.defaults.source="Shared";
        api::VisualBehaviorInput row;row.flag={true,99,{}};row.emotion={true,"Emo_Hit",{}};
        row.recover.resolved_state={true,"Recover",{}};out.defaults.behaviors.rows.push_back(row);return out;
    };
    int hit_config_reads=0,hit_weight_reads=0,hit_name_reads=0;
    readers.hit_character=[&](const auto& path){++hit_config_reads;api::CharacterHitInput out;out.source=path;out.loaded=true;return out;};
    readers.hit_weights=[&]{++hit_weight_reads;return api::HitAnimationDefaults{};};
    readers.hit_names=[&]{++hit_name_reads;return api::VisualReactionInput{};};
    // Receiving a hit must not discover the roster, actions, events or abilities.
    {
        CatalogBrowser receiving(readers);
        Check(!receiving.ResolveHitInputs("Battle/A",false));receiving.Advance(100000);
        const auto input=receiving.ResolveHitInputs("Battle/A",false);
        Check(input && input->character->source=="Battle/A" && !input->visuals);
        Check(table_reads==0 && document_reads.empty() && ability_reads==0 && pose_visual_reads==0);
        Check(hit_config_reads==1 && hit_name_reads==1 && hit_weight_reads==1 && ready_reads==1);
        Check(receiving.ResolveHitInputs("Battle/A",false).has_value());
        Check(!receiving.ResolveHitInputs("Battle/B",true));receiving.Advance(100000);
        Check(receiving.ResolveHitInputs("Battle/B",true)->visuals->character_config=="Battle/B");
        Check(hit_config_reads==2 && hit_weight_reads==1 && ready_reads==1 && pose_visual_reads==1);
        Check(document_reads.empty() && ability_reads==0 && table_reads==0);
        const auto old=receiving.ResolveHitInputs("Battle/A",false);
        receiving.RefreshDocument({api::ActionConfigKind::Battle,"Battle/A"});
        Check(!receiving.ResolveHitInputs("Battle/A",false));receiving.Advance(100000);
        Check(receiving.ResolveHitInputs("Battle/A",false)->character!=old->character && old->character->loaded);
    }
    hit_config_reads=hit_weight_reads=hit_name_reads=ready_reads=pose_visual_reads=0;
    CatalogBrowser browser(readers);Check(table_reads==0 && ready_reads==0 && !browser.Snapshot()->requested);
    auto selection=browser.Prepare(BrowsePlayer{{11,0,1},{}});Check(table_reads==0 && identity_reads==0);
    Finish(browser);auto view=browser.Snapshot();
    Check(std::get<BrowsePlayer>(view->Source(selection)->input).key.skin==1);
    Check(table_reads==1 && identity_reads==0 && view->Source(selection)->result.character->appearance->template_prefab=="Skin.prefab");
    Check(browser.Plan(selection).package.npc_id==0 && browser.Plan(selection).package.construction==ConstructionKind::StoryActor);
    Check(document_reads["Battle/A"]==1 && document_reads["Event/Shared"]==1 && ability_reads==1);
    Check(view->Source(selection)->actions.proposals.size()>=4 && browser.Plan(selection).alternate);
    // Exercise the production CatalogReaders -> Browser -> ActionCatalog ->
    // immutable request path, not defaults injected directly into playback.
    Check(ready_reads==1 && view->Source(selection)->actions.ready_actions.size()==1);
    const auto& defaults=view->Source(selection)->actions.ready_actions.front()->defaults;
    Check(defaults.default_expression.available && defaults.default_expression.value=="FaceDefault");
    Check(defaults.hit_additive_layer.value=="HitAdditive" && defaults.hit_override_layer.value=="HitOverride");
    Check(defaults.empty_layer_state.value=="Empty" && defaults.transition_in.value==0.17f);
    Check(defaults.transitions.size()==1 && defaults.transitions.front().state.value=="DefaultReadyEdge");
    const auto roots=browser.Plan(selection).action_sources;
    const auto ready_pose=browser.ResolvePose(roots,ReadyPoseSelection{"Battle/A",0,-1});
    Check(ready_pose && std::get<ReadyState>(*ready_pose).actions->defaults.transition_in.value==0.17f);
    Check(document_reads["Battle/A"]==1 && ready_reads==1); // resolver uses the shared cache
    bool foreign_pose=false,missing_pose=false;
    try{browser.ResolvePose(roots,ReadyPoseSelection{"Battle/Other",0,-1});}catch(const std::invalid_argument&){foreign_pose=true;}
    try{browser.ResolvePose(roots,ReadyPoseSelection{"Battle/A",44,-1});}catch(const std::runtime_error&){missing_pose=true;}
    Check(foreign_pose && missing_pose && document_reads.count("Battle/Other")==0);
    Check(!browser.ResolvePose(roots,ReactionSelection{"Battle/A",99,true}) && pose_visual_reads==0);
    Finish(browser);const auto recovery_pose=browser.ResolvePose(roots,ReactionSelection{"Battle/A",99,true});
    Check(recovery_pose && std::get<VisualState>(*recovery_pose).recover && std::get<VisualState>(*recovery_pose).expression.value=="Emo_Hit");
    Check(pose_visual_reads==1 && document_reads["Battle/A"]==1);
    const auto immutable=view;const auto current=browser.Prepare(BrowseNpc{91});
    Check(browser.Plan(selection).package.appearance.has_value()); // another client cannot invalidate it
    Finish(browser);Check(browser.Plan(current).package.npc_id==91 && !browser.Plan(current).package.appearance);
    Check(std::get<BrowseNpc>(browser.Snapshot()->Source(current)->input).id==91u);
    Check(immutable->sources.size()==1 && immutable->Source(selection)->result.character->appearance.has_value());
    browser.Prepare(BrowsePlayer{{11,0,1},{}});Finish(browser);
    Check(identity_reads==0 && document_reads["Battle/A"]==1 && table_reads==1);
    Check(ready_reads==1); // shared defaults are not reread for each selection
    browser.RefreshAbility("ability/A");Finish(browser);Check(ability_reads==2 && document_reads["Event/Shared"]==1);
    browser.RefreshDocument({api::ActionConfigKind::AnimationEvent,"Event/Shared"});Finish(browser);
    Check(document_reads["Event/Shared"]==2 && document_reads["Battle/A"]==1);
    Check(browser.NpcMotions(reinterpret_cast<void*>(123)).motions[0].id==42 && npc_reads==1);
    const auto missing=browser.Prepare(BrowsePlayer{{11,0,9},{}});Finish(browser);Check(!browser.Snapshot()->Source(missing)->result.character);
    Check(browser.Snapshot()->Source(missing)->phase==SourceRequestPhase::Unresolved);
    const auto bad=browser.Prepare(BrowseNpc{404});Finish(browser);Check(!browser.Snapshot()->Source(bad)->error.empty());
    const auto good=browser.Prepare(BrowseNpc{91});Finish(browser);Check(browser.Snapshot()->Source(good)->error.empty());
    // Independent battle failure leaves a usable exploration plan and no
    // automatic retry. Explicit retry reuses successful identity/table reads.
    broken_battle=true;CatalogBrowser partial(readers);selection=partial.Prepare(BrowsePlayer{{11,0,0},{}});Finish(partial);
    Check(partial.Plan(selection).package.appearance && !partial.Plan(selection).alternate);
    auto reads=document_reads["Battle/A"];partial.Advance();Check(document_reads["Battle/A"]==reads);
    const auto before_identity=identity_reads;broken_battle=false;const auto retry=partial.RetrySource(selection);Finish(partial);
    Check(partial.Plan(retry).alternate.has_value() && document_reads["Battle/A"]==reads+1 && identity_reads==before_identity);
    Check(!partial.Plan(selection).alternate); // old result is not silently rewritten
    // Table failure is not a dependency of an author's explicit NPC package.
    broken_tables=true;CatalogBrowser authored(readers);CharacterPackage package;package.id="authored";package.npc_id=123;
    selection=authored.Prepare(package);Finish(authored);Check(authored.Plan(selection).package.npc_id==123 && !authored.Snapshot()->table_error.empty());
    broken_tables=false;authored.Request(true);Finish(authored);Check(authored.Snapshot()->table_error.empty());
    browser.InspectActions(27,{{ActionDomain::Alternate,{api::ActionConfigKind::Battle,"Battle/A"}}});Finish(browser);
    browser.Prepare(BrowseNpc{91});Finish(browser);
    Check(browser.Snapshot()->Actions(27)->actions.proposals.front().domain==ActionDomain::Alternate);
    // Different entity configurations sharing an identity remain distinct;
    // the UI can select either through the same validated source command.
    rows.npcs.rows={{91,"Npc/A","Entity/Standard"},{92,"Npc/A","Entity/Other"}};
    rows.players.rows={{1202,1202,0,0,"Player/A","Player.prefab"}};
    CatalogBrowser variants(readers);const auto ambiguous=variants.Prepare(BrowsePlayer{{1202,0,0},{}});Finish(variants);
    Check(variants.Snapshot()->Source(ambiguous)->phase==SourceRequestPhase::Ready);
    for(auto id:{91u,92u}) {
        const auto key=std::get<BrowsePlayer>(variants.Snapshot()->Source(ambiguous)->input).key;
        const auto chosen=variants.Prepare(BrowsePlayer{key,id});Finish(variants);
        Check(variants.Plan(chosen).package.npc_id==0 && variants.Plan(chosen).package.appearance->template_prefab=="Player.prefab");
    }
    auto unavailable_readers=readers;
    unavailable_readers.ready_defaults=[]()->api::ReadyAnimationDefaults {throw std::runtime_error("defaults read failed");};
    CatalogBrowser unavailable(unavailable_readers);unavailable.Request();unavailable.Advance();
    unavailable.InspectActions(27,{{ActionDomain::Alternate,{api::ActionConfigKind::Battle,"Battle/A"}}});Finish(unavailable);
    Check(unavailable.Snapshot()->Actions(27)->actions.ready_actions.size()==1);
    Check(!unavailable.Snapshot()->Actions(27)->actions.ready_actions.front()->defaults.default_expression.available);
    Check(unavailable.Snapshot()->Actions(27)->actions.ready_actions.front()->defaults.transition_in.error=="defaults read failed");
    int sliced_steps=0,sliced_begins=0;
    auto incremental=readers;
    incremental.abilities=[](const auto&)->api::AbilitySources{throw std::runtime_error("synchronous reader must not be used");};
    incremental.begin_abilities=[&](const auto& names){Check(!names.empty());++sliced_begins;return std::make_unique<SlicedRead>(sliced_steps);};
    CatalogBrowser slices(incremental);const auto sliced=slices.Prepare(BrowsePlayer{{1202,0,0},91});Finish(slices);
    Check(sliced_steps==3 && sliced_begins==1 && slices.Snapshot()->Source(sliced)->actions.finished);
    slices.RefreshAbility("ability/A");slices.Advance(0);Check(sliced_steps==4);
    Check(!slices.CancelPending());Check(slices.CancelPending());Check(sliced_steps==6);
    // No source-window selection or table query is needed to discover two
    // saved actors. Cache reads are shared, but projected actions are not.
    std::map<std::string,int> shared_reads;int shared_abilities=0;
    CatalogReaders multiple;
    multiple.characters=[]()->api::CharacterCatalog{throw std::runtime_error("table should not be requested");};
    multiple.document=[&](auto kind,const std::string& path) {
        ++shared_reads[path];api::ActionConfigInput out;out.kind=kind;out.path=path;out.loaded=true;
        out.states.push_back({path,"state"});
        if(kind==api::ActionConfigKind::AnimationEvent)out.abilities.push_back({"SharedAbility","ability"});
        else out.presentation.event_files={true,{"SharedEvent"},{}};
        return out;
    };
    multiple.abilities=[&](const auto& names) {
        ++shared_abilities;Check(names==std::vector<std::string>{"SharedAbility"});return api::AbilitySources{};
    };
    std::vector<CatalogWork> work_reports;
    CatalogBrowser actors(multiple,[&](const auto& work){work_reports.push_back(work);});
    const std::vector<ActionSourceRoot> first{{ActionDomain::Alternate,{api::ActionConfigKind::Battle,"ActorA"}}};
    const std::vector<ActionSourceRoot> second{{ActionDomain::Exploration,{api::ActionConfigKind::Adventure,"ActorB"}}};
    actors.InspectActions(1,first);actors.InspectActions(2,second);Finish(actors);
    Check(work_reports.size()==1 && work_reports[0].source_steps==0 && work_reports[0].action_steps==4 &&
        work_reports[0].slices>0 && work_reports[0].max_us<=work_reports[0].total_us);
    auto both=actors.Snapshot();const auto rev_a=both->Actions(1)->revision,rev_b=both->Actions(2)->revision;
    Check(!both->requested && both->sources.empty() && shared_reads["SharedEvent"]==1 && shared_abilities==1);
    Check(both->Actions(1)->actions.proposals.size()==2 && both->Actions(2)->actions.proposals.size()==2);
    const CatalogActionSelection a{rev_a,ConfiguredActionKey{ActionDomain::Alternate,"ActorA"}};
    const CatalogActionSelection b{rev_b,ConfiguredActionKey{ActionDomain::Exploration,"ActorB"}};
    Check(std::get<FreeStyleState>(actors.ResolveAction(1,a).request).name=="ActorA");
    Check(std::get<AnimatorState>(actors.ResolveAction(2,b).request).name=="ActorB");
    bool foreign=false;try{actors.ResolveAction(1,{rev_a,ConfiguredActionKey{ActionDomain::Exploration,"ActorB"}});}catch(const std::runtime_error&){foreign=true;}Check(foreign);
    actors.InspectActions(1,first);Check(actors.Snapshot()->Actions(1)->revision==rev_a && shared_reads["ActorA"]==1);
    actors.RefreshDocument({api::ActionConfigKind::Battle,"ActorA"});
    Check(actors.Snapshot()->characters.at(2)==both->characters.at(2));
    Check(actors.Snapshot()->Actions(2)->revision==rev_b && actors.Snapshot()->Actions(2)->actions.finished);
    Check(std::get<AnimatorState>(actors.ResolveAction(2,b).request).name=="ActorB");
    bool old=false;try{actors.ResolveAction(1,a);}catch(const std::runtime_error&){old=true;}Check(old);
    Finish(actors);Check(shared_reads["ActorA"]==2 && shared_reads["ActorB"]==1 && shared_reads["SharedEvent"]==1);
    Check(actors.Snapshot()->characters.at(2)==both->characters.at(2));
    Check(both->Actions(1)->revision==rev_a && both->Actions(1)->actions.finished); // old snapshot stays immutable
    const auto after_refresh=actors.Snapshot()->Actions(1)->revision;
    actors.RefreshAbility("SharedAbility");Finish(actors);
    Check(actors.Snapshot()->Actions(1)->revision!=after_refresh && actors.Snapshot()->Actions(2)->revision!=rev_b && shared_abilities==2);
    actors.InspectActions(1,second);Finish(actors);Check(shared_reads["ActorB"]==1);
    Check(actors.Snapshot()->Actions(1)->actions.proposals.front().state=="ActorB");
    Check(actors.Snapshot()->Actions(2)->actions.proposals.front().state=="ActorB");
    const auto rejected=actors.Prepare(BrowseNpc{404});Finish(actors);
    Check(!actors.Snapshot()->Source(rejected)->error.empty() && actors.Snapshot()->Actions(1)->actions.finished && actors.Snapshot()->Actions(2)->actions.finished);
    Check(shared_reads["ActorB"]==1); // failing creation preview does not discard actor catalogs
    actors.InspectActions(99,{{ActionDomain::Alternate,{api::ActionConfigKind::Battle,"Abandoned"}}});
    actors.ForgetCharacter(99);Finish(actors);
    Check(!shared_reads.count("Abandoned") && !actors.Snapshot()->Actions(99));
    // Independent creation requests use one table/config cache, not one
    // mutable selection or a complete reader/cache per caller.
    int concurrent_tables=0;std::map<std::string,int> identities;
    CatalogReaders concurrent;
    concurrent.characters=[&] {
        ++concurrent_tables;api::CharacterCatalog data;
        data.npcs.enumerated=data.players.enumerated=data.avatars.enumerated=true;
        data.npcs.rows={{31,"N31","M31"},{32,"N32","M32"}};
        data.players.rows={{11,99,0,0,"P11","P11.prefab"}};return data;
    };
    concurrent.identity=[&](const auto& path) {++identities[path];return api::TextInput{true,path=="N32"?"B":"A",{}};};
    concurrent.document=[](auto kind,const auto& path) {api::ActionConfigInput out;out.kind=kind;out.path=path;out.loaded=true;return out;};
    CatalogBrowser requests(concurrent);
    const auto slow=requests.Prepare(BrowsePlayer{{11,0,0},{}});
    const auto fast=requests.Prepare(BrowseNpc{32});
    const auto fault=requests.Prepare(BrowseNpc{404});
    const auto duplicate=requests.Prepare(BrowsePlayer{{11,0,0},{}});
    const auto abandoned=requests.Prepare(BrowsePlayer{{404,0,0},{}});
    const auto queued=requests.Snapshot();requests.ReleaseSource(abandoned);requests.ReleaseSource(abandoned);
    Check(queued->Source(abandoned)->phase==SourceRequestPhase::Queued && !requests.Snapshot()->Source(abandoned));
    Check(concurrent_tables==0 && identities.empty());
    bool early=false;try{requests.Plan(slow);}catch(const std::runtime_error&){early=true;}Check(early);
    for(int i=0;i<6;++i)requests.Advance();
    Check(requests.Snapshot()->Source(fast)->phase==SourceRequestPhase::Ready && requests.Snapshot()->Source(slow)->phase==SourceRequestPhase::Ready);
    Check(requests.Plan(fast).package.npc_id==32); // no head-of-line block
    Finish(requests);
    Check(requests.Plan(slow).package.npc_id==0 && requests.Plan(duplicate).package.npc_id==0);
    Check(requests.Snapshot()->Source(fault)->phase==SourceRequestPhase::Failed);
    Check(concurrent_tables==1 && identities.empty());
    const auto completed=requests.Snapshot();const auto copied_plan=requests.Plan(slow);
    requests.Request(true);Check(requests.Plan(fast).package.npc_id==32); // table refresh doesn't revoke copied results
    requests.ReleaseSource(slow);bool released=false;
    try{requests.Plan(slow);}catch(const std::invalid_argument&){released=true;}Check(released);
    Check(copied_plan.package.npc_id==0 && completed->Source(slow)->phase==SourceRequestPhase::Ready);
    Finish(requests);Check(concurrent_tables==2 && requests.Plan(duplicate).package.npc_id==0);
    const auto before_batch=requests.Snapshot();
    std::uint64_t batched=0;
    requests.BatchSources([&]{
        batched=requests.Prepare(BrowseNpc{31});
        requests.BatchSources([&]{requests.Prepare(BrowseNpc{32});});
        Check(requests.Snapshot()==before_batch);
    });
    Check(requests.Snapshot()!=before_batch && requests.Snapshot()->Source(batched));
    const auto before_throw=requests.Snapshot();std::uint64_t interrupted=0;
    try {requests.BatchSources([&]{interrupted=requests.Prepare(BrowseNpc{31});throw std::runtime_error("fixture");});}
    catch(const std::runtime_error&) {}
    Check(requests.Snapshot()==before_throw);requests.Advance();
    Check(requests.Snapshot()->Source(interrupted)); // no stuck batch after exception
    Finish(requests);const auto stable=requests.Snapshot();requests.Advance();Check(requests.Snapshot()==stable);
    // Source preparation and layout restoration of ordinary actions must not
    // trigger reaction reads. Browsing opts in once per resolved character.
    int visual_reads=0;
    concurrent.visuals=[&](const std::string& path) {
        ++visual_reads;api::VisualConfigSources out;out.character_config=path;out.context_available=true;
        out.defaults.source="global";out.defaults.loaded=true;out.defaults.behaviors.enumerated=true;
        out.expression_transition={true,0.2f,{}};
        api::VisualBehaviorInput row;row.flag={true,28,{}};row.emotion={true,"Emo_Stun",{}};
        row.occur.resolved_state={true,"Stagger",{}};row.loop.resolved_state={true,"Stagger_Loop",{}};
        row.recover.resolved_state={true,"Stagger_Recover",{}};
        row.occur.transition=row.loop.transition=row.recover.transition={true,0.15f,{}};
        out.defaults.behaviors.rows.push_back(row);return out;
    };
    CatalogBrowser reactions(concurrent);
    const std::vector<ActionSourceRoot> reaction_roots{{ActionDomain::Alternate,{api::ActionConfigKind::Battle,"Reactions/A"}}};
    reactions.InspectActions(1,reaction_roots);Finish(reactions);Check(visual_reads==0);
    Check(reactions.Snapshot()->Actions(1)->actions.finished && reactions.Snapshot()->Actions(1)->actions.visual_states.empty());
    Check(reactions.InspectVisuals(reaction_roots));
    Check(!reactions.Snapshot()->Actions(1)->actions.finished && reactions.Snapshot()->Actions(1)->actions.Ready(ActionDomain::Alternate));
    Finish(reactions);Check(visual_reads==1);
    const auto reaction_view=reactions.Snapshot()->Actions(1);
    const auto picked=reactions.ResolveAction(1,{reaction_view->revision,VisualActionKey{"Reactions/A",28,false}});
    Check(std::get<VisualState>(picked.request).expression.value=="Emo_Stun");
    reactions.InspectActions(2,reaction_roots);Check(!reactions.InspectVisuals(reaction_roots));Finish(reactions);Check(visual_reads==1);
    reactions.RefreshDocument({api::ActionConfigKind::Battle,"Reactions/A"});Finish(reactions);Check(visual_reads==2);
    std::cout<<"Catalog browser passed: shared reads, per-instance projections, source revision isolation, production ready defaults, explicit retries and immutable snapshots; no game.\n";
}
