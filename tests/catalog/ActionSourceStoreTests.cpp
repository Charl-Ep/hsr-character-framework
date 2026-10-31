#include <hksr/framework/catalog/ActionSourceStore.hpp>
#include <hksr/framework/catalog/ActionCatalog.hpp>
#include <hksr/framework/layout/SceneLayout.hpp>
#include <Windows.h>
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace {
using namespace hksr;
using namespace hksr::framework;
void Check(bool value){if(!value)throw std::runtime_error("action source persistence test failed");}
ActionSourceIdentity Identity(){return {"fixture-build",42,{{"Persistent/IFix/Windows/M_IFixV.bytes","bytes:1234"}}};}
api::ActionConfigInput Document(api::ActionConfigKind kind,const std::string& path) {
    api::ActionConfigInput out;out.kind=kind;out.path=path;out.loaded=true;
    out.presentation={path,{true,"StandBy",{}},{true,{"events"},{}},{true,{"disabled-layer"},{}}};
    if(kind==api::ActionConfigKind::AnimationEvent){out.presentation.event_files.values.clear();out.referenced_event_file={true,"events",{}};}
    out.states={{"Attack_1","Anim"}};out.abilities={{"FixtureSkill","Entry"}};
    api::SkillInput skill;skill.source_index=7;skill.name={true,"Skill01",{}};skill.ready_state={true,"ReadyA",{}};
    skill.entry_ability={true,"FixtureSkill",{}};skill.prepare_ability={true,"Prepare",{}};skill.ready_effect={true,"FX",{}};
    skill.type={true,1,{}};skill.use_transition={true,true,{}};skill.cut_1_to_2={true,false,{}};
    skill.cut_2_to_1={true,true,{}};skill.wait_prepare={true,true,{}};skill.auto_standby={true,false,{}};
    skill.ready_wait={true,0.45f,{}};skill.child_skills={true,{"Child"},{}};
    skill.variants.rows.push_back({2,{true,"ReadyB",{}},{true,true,{}},{true,3,{}}});
    skill.variants.enumerated=true;skill.variants.visited_count=skill.variants.declared_count=1;
    out.skills.rows.push_back(skill);out.skills.enumerated=true;out.skills.visited_count=out.skills.declared_count=1;
    out.transitions.rows.push_back({4,{true,1,{}},{true,2,{}},{true,"AtoB",{}},{true,0.3f,{}},{true,0.5f,{}}});
    out.transitions.enumerated=true;out.transitions.declared_count=out.transitions.visited_count=1;
    out.elation_ready.rows.push_back({1,{true,"SkillE",{}},{true,"EntryE",{}},{true,"LoopE",{}}});
    out.elation_ready.enumerated=true;out.elation_ready.declared_count=out.elation_ready.visited_count=1;
    return out;
}
api::AbilitySources Ability(const std::string& name) {
    api::AbilitySources out;out.nodes_read=12;
    std::vector<api::ConfigStep> path{{"TaskRoot","Actions",api::ConfigSelection::Index,3,{}},
        {"Branch","Routes",api::ConfigSelection::StringKey,-1,"Caster"},
        {"Config","Task",api::ConfigSelection::Reference,-1,{}}};
    out.animations.push_back({name,path,api::AnimationTaskKind::StateWithMove,{true,"Skill01",{}},
        {true,2,{}},{true,true,{}},{true,true,{}},{true,"Caster",{}}});
    out.waits.push_back({name,path,{true,"Skill01",{}},{true,"Caster",{}},{true,true,{}}});
    out.removals.push_back({name,path,{{true,"Effect",{}},{true,"PathKey",{}},{true,"Caster",{}},{true,true,{}}}});
    out.references.push_back({name,path,{true,name,{}},{true,true,{}}});
    out.skill_mappings.push_back({name,path,{true,"Skill11",{}},{true,"Caster",{}},{true,0,{}},{true,true,{}}});
    out.skill_continuations.push_back({name,path,{true,1,{}},{true,2,{}},{true,true,{}}});return out;
}
void Complete(ActionCatalog& catalog){int limit=100;while(!catalog.Finished() && limit--)catalog.Advance();Check(catalog.Finished());}
std::string ProjectionIdentity(const ActionCatalogStatus& status) {
    std::ostringstream out;
    auto path=[&](const auto& steps){for(const auto& p:steps)out<<std::quoted(p.declaring_type)<<std::quoted(p.field)<<int(p.selection)<<':'<<p.index<<std::quoted(p.key);};
    for(const auto& row:status.proposals) {
        out<<'P'<<int(row.domain)<<std::quoted(row.state);
        for(const auto& ref:row.references) {
            out<<'R'<<int(ref.source.kind)<<std::quoted(ref.source.path)<<std::quoted(ref.field)<<std::quoted(ref.root_ability);
            if(ref.task){out<<'T'<<std::quoted(ref.task->ability)<<int(ref.task->kind);path(ref.task->path);}
        }
        for(const auto& exit:row.retirements){out<<'X'<<std::quoted(exit.state)<<std::quoted(exit.removal.ability);path(exit.removal.path);}
    }
    for(const auto& group:status.ready_actions) {
        out<<'G'<<std::quoted(group->source)<<std::quoted(group->initial_state);
        for(const auto& pose:group->poses)out<<'S'<<pose.skill_index<<':'<<pose.variant_index<<std::quoted(pose.skill_name)
            <<std::quoted(pose.state)<<std::quoted(pose.label)<<std::quoted(pose.preparation_source);
    }
    return out.str();
}
}
void TestActionSourceStore() {
    const auto directory=std::filesystem::temp_directory_path()/
        (L"hksr-action-sources-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    const auto file=directory/L"sources.json";
    auto make=[&](ActionSourceIdentity identity) {
        auto store=std::make_shared<ActionSourceStore>(std::filesystem::path{},file);
        store->Load();store->Bind(std::move(identity));return store;
    };
    const std::vector<ActionSourceRoot> roots{
        {ActionDomain::Exploration,{api::ActionConfigKind::Adventure,"adventure"}},
        {ActionDomain::Alternate,{api::ActionConfigKind::Battle,"battle"}}};
    auto store=make(Identity());int documents=0,abilities=0;
    auto read=[&](auto kind,const auto& path){++documents;return Document(kind,path);};
    auto ability=[&](const auto& names){++abilities;Check(names.size()==1);return Ability(names.front());};
    ActionCatalog cold(read,ability);cold.SetSourceStore(store);cold.Request(roots);Complete(cold);
    Check(documents==3 && abilities==1);store->Flush();store->Pump();
    Check(std::filesystem::exists(file));
    std::ifstream input(file,std::ios::binary);const std::string bytes((std::istreambuf_iterator<char>(input)),{});input.close();
    auto decoded=DecodeActionSources(bytes);Check(EncodeActionSources(decoded)==bytes);
    Check(decoded.documents.size()==3 && decoded.abilities.size()==1);
    Check(decoded.abilities.at("FixtureSkill")->references.front().path.at(1).key=="Caster");
    auto warm_store=make(Identity());ActionCatalog warm(read,ability);warm.SetSourceStore(warm_store);
    warm.Request(roots);
    Check(warm.Document(roots[0].source)!=nullptr && warm.Document(roots[1].source)!=nullptr);
    // Cached declarations and relationships are available in the first view;
    // Advance is reserved for missing native inputs, not imported value work.
    Check(warm.Status().documents.size()==cold.Status().documents.size());
    for(const auto& document:warm.Status().documents)Check(document.attempted && document.loaded);
    Check(warm.Finished());Check(documents==3 && abilities==1);
    Check(warm.Status().proposals.size()==cold.Status().proposals.size());
    for(std::size_t i=0;i<cold.Status().proposals.size();++i) {
        const auto& a=cold.Status().proposals[i];const auto& b=warm.Status().proposals[i];
        Check(a.state==b.state && a.domain==b.domain && a.references.size()==b.references.size() && a.retirements.size()==b.retirements.size());
    }
    for(auto domain:{ActionDomain::Exploration,ActionDomain::Alternate}) {
        const auto a=RestoreLayoutAction(SavedConfiguredAction{domain,"Attack_1"},cold.Status());
        const auto b=RestoreLayoutAction(SavedConfiguredAction{domain,"Attack_1"},warm.Status());
        Check(a.has_value() && b.has_value() && a->request.index()==b->request.index());
    }
    // A new root reads only its missing document. Shared abilities/events stay warm.
    auto extended=roots;extended.push_back({ActionDomain::Exploration,{api::ActionConfigKind::Adventure,"new"}});
    warm.Request(extended);Complete(warm);Check(documents==4 && abilities==1);
    warm.RefreshAbility("FixtureSkill");Complete(warm);Check(abilities==2);
    warm.Refresh(roots[0].source);Complete(warm);Check(documents==5);
    warm_store->Flush();warm_store->Pump();
    auto updated=Identity();++updated.design;auto different=make(updated);
    Check(!different->Ability("FixtureSkill") && !different->Document(roots[0].source));
    updated=Identity();updated.markers.begin()->second="new-hotfix";different=make(updated);
    Check(!different->Ability("FixtureSkill"));
    updated=Identity();updated.build="new-build";different=make(updated);Check(!different->Ability("FixtureSkill"));
    // Read failures are retained as diagnostics, never reused as empty success.
    auto failed=Document(api::ActionConfigKind::Adventure,"failed");failed.skills.rows[0].ready_state={false,{},"read failed"};
    failed.issues.push_back({7,"ready state","read failed"});
    warm_store->Put(failed);auto failed_ability=Ability("Failed");failed_ability.animations[0].state={false,{},"failed literal"};
    failed_ability.issues.push_back({"Failed","failed literal",{}});
    warm_store->Put("Failed",failed_ability);warm_store->Flush();warm_store->Pump();
    auto reloaded=make(Identity());Check(!reloaded->Document({api::ActionConfigKind::Adventure,"failed"}) && !reloaded->Ability("Failed"));
    auto partial=Document(api::ActionConfigKind::Adventure,"optional");partial.presentation.initial_state={false,{},"optional declaration absent"};
    warm_store->Put(partial);auto observed=Ability("Observed");observed.waits[0].target_alias={false,{},"not a literal target"};
    observed.issues.push_back({"Observed","not a literal target",{},api::AbilitySourceIssueKind::NotLiteral});
    observed.issues.push_back({"Observed","unsupported non-animation container",{},api::AbilitySourceIssueKind::Unsupported});
    warm_store->Put("Observed",observed);warm_store->Flush();warm_store->Pump();
    reloaded=make(Identity());const auto preserved=reloaded->Ability("Observed");
    Check(preserved && preserved->issues.size()==2 && !preserved->waits[0].target_alias.available);
    Check(!reloaded->Document({api::ActionConfigKind::Adventure,"optional"})->presentation.initial_state.available);
    auto unknown=Ability("Unclassified");unknown.issues.push_back({"Unclassified","target is not a literal alias",{}});
    Check(!ReusableActionSource(unknown)); // text is not the validity contract
    std::ifstream recorded(file,std::ios::binary);const auto raw=DecodeActionSources(std::string(std::istreambuf_iterator<char>(recorded),std::istreambuf_iterator<char>()));recorded.close();
    Check(raw.abilities.count("Failed")==1 && raw.documents.count({api::ActionConfigKind::Adventure,"failed"})==1);
    // Explicitly invalidating a cached root persists even if the reread fails.
    reloaded->EraseAbility("FixtureSkill");reloaded->Flush();reloaded->Pump();
    Check(!make(Identity())->Ability("FixtureSkill"));
    // Packaged data cannot resurrect an explicitly refreshed record after a
    // failed reread. A later complete reread supersedes that invalidation.
    const auto packaged=directory/L"packaged.json";
    {std::ofstream out(packaged,std::ios::binary);out<<bytes;}
    auto overlay=std::make_shared<ActionSourceStore>(packaged,file);overlay->Load();overlay->Bind(Identity());
    Check(!overlay->Ability("FixtureSkill") && overlay->Document(roots[0].source));
    overlay->Put("FixtureSkill",Ability("FixtureSkill"));overlay->Flush();overlay->Pump();
    auto overlay2=std::make_shared<ActionSourceStore>(packaged,file);overlay2->Load();overlay2->Bind(Identity());
    Check(bool(overlay2->Ability("FixtureSkill")));
    // The first projection must resolve ready overrides from cached abilities,
    // even before Request/Advance has populated a native-work queue.
    auto preparation=Ability("FixtureSkill");
    preparation.skill_mappings.front().path={{"Config","OnStart",api::ConfigSelection::Index,0,{}}};
    preparation.skill_mappings.front().skill={true,"Skill01",{}};
    preparation.skill_mappings.front().control={true,0,{}};
    preparation.animations.front().path={{"Config","OnStart",api::ConfigSelection::Index,1,{}}};
    preparation.animations.front().state={true,"ReadyFromSource",{}};
    preparation.skill_continuations.front().path={{"Config","OnStart",api::ConfigSelection::Index,2,{}}};
    preparation.skill_continuations.front().control={true,0,{}};
    preparation.skill_continuations.front().child={true,-1,{}};
    overlay2->Put("FixtureSkill",preparation);
    ActionCatalog immediate(read,ability);immediate.SetSourceStore(overlay2);
    const auto first=immediate.Snapshot(roots);
    Check(first->finished && first->ready_actions.size()==1);
    Check(first->ready_actions[0]->poses[0].state=="ReadyFromSource" && first->ready_actions[0]->poses[1].state=="ReadyB");
    ActionCatalog late(read,ability);late.Request(roots);Check(!late.Finished());
    const auto reads_before=documents+abilities;
    late.SetSourceStore(overlay2);
    Check(late.Finished() && ProjectionIdentity(late.Status())==ProjectionIdentity(*first));
    Check(documents+abilities==reads_before);
    // Unknown format or malformed files use the normal reader rather than fail restoration.
    {std::ofstream bad(file);bad<<"{\"format\":\"unknown\"}";}
    auto corrupt=make(Identity());ActionCatalog fallback(read,ability);fallback.SetSourceStore(corrupt);fallback.Request(roots);Complete(fallback);
    Check(fallback.Status().proposals.size()==cold.Status().proposals.size());
    std::filesystem::remove_all(directory);
}
void AuditActionSources(const char* file) {
    const auto start=std::chrono::steady_clock::now();
    std::ifstream input(std::filesystem::u8path(file),std::ios::binary);
    if(!input)throw std::runtime_error("action source file unavailable");
    const std::string bytes((std::istreambuf_iterator<char>(input)),{});input.close();
    const auto data=DecodeActionSources(bytes);
    Check(EncodeActionSources(data)==bytes);
    const auto parsed=std::chrono::steady_clock::now();
    auto store=std::make_shared<ActionSourceStore>(std::filesystem::u8path(file),std::filesystem::path{});
    store->Load();store->Bind(data.identity);
    std::set<std::string> missing_documents,missing_abilities;
    ActionCatalog catalog([&](auto,const auto& path)->api::ActionConfigInput {
        missing_documents.insert(path);throw std::runtime_error("source file does not cover document");
    },[&](const auto& names)->api::AbilitySources {
        missing_abilities.insert(names.begin(),names.end());throw std::runtime_error("source file does not cover ability");
    });
    catalog.SetSourceStore(store);
    std::vector<ActionSourceRoot> roots;
    for(const auto& row:data.documents)if(row.first.kind!=api::ActionConfigKind::AnimationEvent)
        roots.push_back({row.first.kind==api::ActionConfigKind::Adventure?ActionDomain::Exploration:ActionDomain::Alternate,row.first});
    const auto project_start=std::chrono::steady_clock::now();catalog.Request(roots);
    while(!catalog.Finished())catalog.Advance();
    const auto& status=catalog.Status();const auto done=std::chrono::steady_clock::now();
    // Replay the exact same captured values through uncached, queued reads.
    // Compare all derived state/source/exit/ready identities, not just counts.
    ActionCatalog queued([&](auto kind,const auto& path){return *data.documents.at({kind,path});},
        [&](const auto& names) {
            api::AbilitySources result;
            auto append=[&](auto member){for(const auto& name:names)if(auto it=data.abilities.find(name);it!=data.abilities.end()) {
                const auto& values=it->second.get()->*member;(result.*member).insert((result.*member).end(),values.begin(),values.end());
            }};
            append(&api::AbilitySources::animations);append(&api::AbilitySources::waits);append(&api::AbilitySources::removals);
            append(&api::AbilitySources::references);append(&api::AbilitySources::skill_mappings);append(&api::AbilitySources::skill_continuations);
            append(&api::AbilitySources::issues);return result;
        });
    queued.Request(roots);while(!queued.Finished())queued.Advance();
    Check(ProjectionIdentity(queued.Status())==ProjectionIdentity(status));
    auto us=[](auto a,auto b){return std::chrono::duration_cast<std::chrono::microseconds>(b-a).count();};
    std::cout<<"documents="<<data.documents.size()<<" abilities="<<data.abilities.size()<<" roots="<<roots.size()
        <<" proposals="<<status.proposals.size()<<" roundtrip_us="<<us(start,parsed)
        <<" store_load_us="<<us(parsed,project_start)<<" projection_us="<<us(project_start,done)
        <<" missing_documents="<<missing_documents.size()<<" missing_abilities="<<missing_abilities.size()<<" queued_projection_equal=1\n";
    for(const auto& name:missing_documents)std::cout<<"missing_document="<<name<<"\n";
    for(const auto& name:missing_abilities)std::cout<<"missing_ability="<<name<<"\n";
}
