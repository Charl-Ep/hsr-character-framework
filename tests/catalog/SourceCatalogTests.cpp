#include <hksr/framework/catalog/SourceCatalog.hpp>
#include <hksr/framework/catalog/OfficialSourcePreferences.hpp>
#include <hksr/framework/character/CharacterSession.hpp>
#include <hksr/framework/catalog/SourceList.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
namespace {
using namespace hksr::framework;
namespace api=hksr::api;
void Check(bool value) { if(!value) throw std::runtime_error("source catalog test failed"); }
}
void TestSourceCatalog() {
    {
        // Policy data is independent of live IDs, FreeStyle values and meshes.
        // All entries exercise the actual preparation path, not a string-presence test.
        for(const auto& preference:OfficialSourcePreferences()) {
            api::CharacterCatalog data;data.npcs.enumerated=true;
            api::PlayerCatalogRow player{40001,50001,0,0,preference.player_config,"CurrentPlayer.prefab"};
            data.players.rows={player};
            data.npcs.rows={{60001,preference.npc_config,preference.npc_model},
                {60002,preference.npc_config,preference.npc_model+".other-variant"}};
            SourceCatalog source(data);
            auto chosen=source.Foundation(player);
            Check(chosen.status==SourceResolution::Ready && chosen.selected->id==60001);
            Check(!chosen.explicit_choice && chosen.preference==preference.label);
            Check(chosen.needed_configs.empty() && chosen.notes.size()==1);
            // Explicit caller choice still wins, even over a reviewed preference.
            chosen=source.Foundation(player,60002);
            Check(chosen.selected->id==60002 && chosen.explicit_choice && chosen.preference.empty());
            // Null/changed FreeStyle is unrelated to this resource-based policy.
            source.SetIdentity(player.character_config,{true,{},{}});
            source.SetIdentity(preference.npc_config,{true,"Unrelated",{}});
            Check(source.Foundation(player).selected->id==60001);
            int reads=0;
            SourcePreparation prepare(data,{
                [&](const auto&) {++reads;return api::TextInput{};},
                [](const auto&) {return api::PresentationConfigInput{};}});
            prepare.Player({player.id,0,0});prepare.Advance();
            Check(prepare.Status().finished && prepare.Status().character && reads==0);
            Check(prepare.Status().character->npc_id==60001);
            Check(prepare.Status().character->avatar_id==player.avatar);
            Check(prepare.Status().character->appearance->template_prefab==player.prefab);
            const auto plan=CharacterPlan::FromPrepared(prepare.Status());
            Check(plan.source_notes==prepare.Status().foundation.notes && plan.action_sources.size()==1);
            Check(plan.action_sources.front().source.path==player.character_config);

            // Missing/stale mapping is diagnostic, not a global source failure.
            auto stale=data;stale.npcs.rows.erase(stale.npcs.rows.begin());
            SourceCatalog fallback(stale);
            fallback.SetIdentity(player.character_config,{true,"Identity",{}});
            fallback.SetIdentity(preference.npc_config,{true,"Identity",{}});
            chosen=fallback.Foundation(player);
            Check(chosen.selected->id==60002 && chosen.preference.empty() && !chosen.notes.empty());
            auto duplicate=data;duplicate.npcs.rows.push_back(data.npcs.rows.front());
            Check(SourceCatalog(duplicate).Foundation(player).status==SourceResolution::Ambiguous);
            // Similar spelling does not select this rule.
            player.character_config+=".other";
            Check(SourceCatalog(data).Foundation(player).status==SourceResolution::Pending);
        }
    }
    {
        api::CharacterCatalog data;
        // Reverse order, repeated IDs with different variants: no truncation,
        // hidden deduplication or dependence on container enumeration order.
        for(unsigned i=3000;i>0;--i) data.npcs.rows.push_back({i,"Config/NPC/Character"+std::to_string(i)+".json","Model"});
        data.players.rows={{9,9,0,1,"Config/Player/A.json","Skin.prefab"},{9,9,0,0,"Config/Player/A.json","Base.prefab"}};
        auto all=SourceIndices(data.npcs.rows,"");Check(all.size()==3000 && data.npcs.rows[all.front()].id==1 && data.npcs.rows[all.back()].id==3000);
        Check(SourceIndices(data.players.rows,"").size()==2 && SourceIndices(data.players.rows,"skin")==std::vector<std::size_t>{0});
        Check(SourceIndices(data.players.rows,"CONFIG a.JSON").size()==2);
        // Optional replay of a real captured whole catalog; never loads game
        // modules, copies resources or needs per-character user operations.
        char* environment=nullptr;std::size_t environment_size=0;
        Check(_dupenv_s(&environment,&environment_size,"HKSR_CATALOG_EVIDENCE")==0);
        const std::unique_ptr<char,decltype(&std::free)> storage(environment,&std::free);
        const auto path=storage.get();
        if(path && *path) {
            std::ifstream input(path);Check(bool(input));std::string line;bool found=false;
            while(std::getline(input,line)) {
                const auto j=nlohmann::json::parse(line);
                if(j.value("event","")!="world-runtime-catalog-end") continue;
                found=true;data={};
                for(const auto& r:j.at("npcs").at("rows")) data.npcs.rows.push_back({r.at("id"),r.at("character_config"),r.at("model_config")});
                for(const auto& r:j.at("players").at("rows")) data.players.rows.push_back({r.at("id"),r.at("avatar"),r.at("enhanced"),r.at("skin"),r.at("character_config"),r.at("prefab")});
                const auto audit=[](const auto& rows) {
                    Check(SourceIndices(rows,"").size()==rows.size());
                    for(std::size_t i=0;i<rows.size();++i) {
                        const auto matches=SourceIndices(rows,SourceFold(rows[i].character_config));
                        Check(std::find(matches.begin(),matches.end(),i)!=matches.end());
                    }
                };
                audit(data.npcs.rows);audit(data.players.rows);
                std::cout<<"Whole captured source-list replay passed: NPC="<<data.npcs.rows.size()<<" Player="<<data.players.rows.size()<<"; every row and lowercase full-path search retained.\n";
            }
            Check(found);
        }
    }
    api::CharacterCatalog rows;
    rows.npcs.enumerated=rows.players.enumerated=rows.avatars.enumerated=true;
    rows.npcs.rows={{91,"NPC/A","Model/A"},{92,"NPC/B","Model/B"}};
    api::PlayerCatalogRow p{20,777,1,0,"Player/A","Player/A.prefab"}; rows.players.rows={p};
    {
        SourceCatalog explicit_source(rows);
        const auto chosen=explicit_source.Foundation(p,91);
        Check(chosen.explicit_choice && chosen.selected->id==91 && chosen.status==SourceResolution::Ready && chosen.needed_configs.empty());
        Check(explicit_source.Foundation(p).status==SourceResolution::Pending); // automatic path unchanged
        explicit_source.SetIdentity("Player/A",{false,{},"FreeStyle identity absent"});
        Check(explicit_source.Foundation(p,91).selected->id==91);
        Check(!explicit_source.Foundation(p,999).selected);
        auto duplicate=rows;duplicate.npcs.rows.push_back(rows.npcs.rows.front());
        Check(SourceCatalog(duplicate).Foundation(p,91).status==SourceResolution::Ambiguous);
        auto missing=rows;missing.npcs.rows.front().model_config.clear();
        // These catalog strings are not inputs of the explicit ID creation
        // path; the actual factory must report its own load/config failure.
        Check(SourceCatalog(missing).Foundation(p,91).status==SourceResolution::Ready);
    }
    rows.avatars.rows={{777,"Battle/A","Battle/A.prefab"},{777,"Battle/A","Battle/A.prefab"}};
    SourceCatalog sources(rows);
    auto f=sources.Foundation(p); Check(f.status==SourceResolution::Pending && f.needed_configs==std::vector<std::string>{"Player/A"});
    sources.SetIdentity("Player/A",{true,"CharacterA",{}});
    f=sources.Foundation(p); Check(f.status==SourceResolution::Pending && f.needed_configs.size()==2);
    sources.SetIdentity("NPC/A",{true,"CharacterA",{}});
    f=sources.Foundation(p); Check(f.status==SourceResolution::Pending && !f.selected); // not prematurely unique
    sources.SetIdentity("NPC/B",{true,"CharacterB",{}});
    f=sources.Foundation(p); Check(f.status==SourceResolution::Ready && f.selected->id==91);
    auto package=SourceCatalog::PlayerPackage(p,*f.selected);
    Check(package.source==SourceKind::Official && package.npc_id==91 && package.appearance->template_prefab=="Player/A.prefab" && package.appearance->weapon_config=="Player/A");
    auto pure=SourceCatalog::NpcPackage(*f.selected); Check(!pure.appearance && pure.npc_id==91);
    package.source=SourceKind::External; package.appearance->asset_directory="assets";
    Check(sources.PackageSources(package).size()==1);
    package.appearance->template_prefab="NPC/A.prefab"; Check(sources.PackageSources(package).empty()); // not name/ID inference
    sources.SetIdentity("NPC/B",{true,"CharacterA",{}});
    Check(sources.Foundation(p).status==SourceResolution::Ambiguous);
    Check(sources.Foundation(p,92).selected->id==92);
    sources.SetIdentity("NPC/A",{false,{},"read failed"});
    Check(sources.Foundation(p).status==SourceResolution::Ready && sources.Foundation(p).selected->id==92 && !sources.Foundation(p).errors.empty());
    Check(sources.Foundation(p,92).status==SourceResolution::Ready);
    Check(sources.Foundation(p,777).status==SourceResolution::Missing);
    auto battle=sources.Battle(777); Check(battle.status==SourceResolution::Ready && battle.candidates.size()==1);
    auto partial_rows=rows; partial_rows.avatars.issues.push_back({4,"row","unrelated row unavailable"});
    Check(SourceCatalog(partial_rows).Battle(777).status==SourceResolution::Ready);
    api::PresentationConfigInput input{"Battle/A",{true,"StandBy",{}},{true,{"Event/A"},{}},{true,{}, {}}};
    auto presentation=SourceCatalog::Presentation(battle.candidates[0],input);
    Check(presentation.model.model_path=="Battle/A.prefab" && presentation.model.event_files.size()==1 && presentation.initial_state.value=="StandBy");
    Check(presentation.model.character_config=="Battle/A");
    input.initial_state={false,{},"absent"};
    Check(!SourceCatalog::Presentation(battle.candidates[0],input).initial_state.available); // optional, no whole-source gate
    rows.avatars.rows.push_back({777,"Battle/B","Battle/B.prefab"});
    Check(SourceCatalog(rows).Battle(777).status==SourceResolution::Ambiguous);
    rows.npcs.rows.push_back({777,"NPC/Preferred","Model/Preferred"});
    SourceCatalog preferred(rows); preferred.SetIdentity("Player/A",{true,"A",{}});
    f=preferred.Foundation(p); Check(f.needed_configs==std::vector<std::string>{"NPC/Preferred"});
    preferred.SetIdentity("NPC/Preferred",{true,"A",{}});
    Check(preferred.Foundation(p).status==SourceResolution::Ready && preferred.Foundation(p).selected->id==777);
    preferred.SetIdentity("NPC/Preferred",{true,"NotA",{}});
    Check(preferred.Foundation(p).status==SourceResolution::Pending); // number alone never authorizes pairing
    rows.avatars.rows.pop_back();
    int identity_reads=0,presentation_reads=0;
    SourcePreparation prepare(rows,{
        [&](const std::string& path) { ++identity_reads; return api::TextInput{true,path=="Player/A" || path=="NPC/Preferred"?"A":"B",{}}; },
        [&](const std::string& path) { ++presentation_reads; return api::PresentationConfigInput{path,{true,"StandBy",{}},{true,{"Event/A"},{}},{true,{}, {}}}; }});
    prepare.Player({20,1,0}); prepare.Advance();
    Check(identity_reads==1 && !prepare.Status().finished);
    prepare.Advance(); Check(identity_reads==2 && !prepare.Status().finished);
    prepare.Advance(); Check(prepare.Status().finished && prepare.Status().character->npc_id==777 && prepare.Status().presentation && presentation_reads==1);
    prepare.Advance(); Check(presentation_reads==1 && identity_reads==2);
    prepare.Player({20,1,0}); prepare.Advance();
    Check(prepare.Status().finished && presentation_reads==1 && identity_reads==2); // cached immutable inputs, no reload
    auto authored=SourceCatalog::PlayerPackage(p,rows.npcs.rows[0]); authored.source=SourceKind::External;
    prepare.Package(authored); prepare.Advance();
    Check(prepare.Status().character->npc_id==91 && prepare.Status().presentation && identity_reads==2 && presentation_reads==1);
    prepare.Player({20,0,0}); Check(prepare.Status().finished && !prepare.Status().character); // not the first variant
    SourcePreparation failed(rows,{{},[](const auto&)->api::PresentationConfigInput {throw std::runtime_error("config failure");}});
    failed.Package(authored); failed.Advance();
    Check(failed.Status().finished && failed.Status().character && !failed.Status().presentation && failed.Status().errors.size()==1);
}
