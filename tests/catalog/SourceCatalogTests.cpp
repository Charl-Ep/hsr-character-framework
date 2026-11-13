#include <hksr/framework/catalog/SourceCatalog.hpp>
#include <hksr/framework/character/CharacterSession.hpp>
#include <hksr/framework/catalog/SourceList.hpp>
#include <hksr/framework/catalog/PlayerSourceGroups.hpp>
#include <hksr/framework/catalog/ActorAudioCatalog.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
namespace {
using namespace hksr::framework;
namespace api = hksr::api;
void Check(bool value) {
    if (!value)
        throw std::runtime_error("source catalog test failed");
}
}
void TestSourceCatalog() {
    {
        // Exercise shared ownership without constructing or invoking an SDK skin.
        auto resource = std::make_shared<int>(1);
        std::weak_ptr<int> lifetime = resource;
        PreparedSource prepared;
        prepared.summary.finished = true;
        prepared.summary.character = CharacterPackage{};
        prepared.skin = std::shared_ptr<api::CharacterSkin>(resource, nullptr);
        resource.reset();
        const auto summary = prepared.summary;
        auto plan = CharacterPlan::FromPrepared(prepared);
        prepared = {};
        Check(!lifetime.expired() && summary.finished && summary.character);
        plan.skin.reset();
        Check(lifetime.expired()); // the copied summary does not own the resource
    }
    {
        const auto path = std::filesystem::temp_directory_path() / "hksr-test-actor-audio-events.json";
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove(path, error);
            }
        } cleanup{path};
        auto write = [&](const char* text) {
            std::ofstream file(path);
            file << text;
        };
        write(
            R"({"format":"hksr.actor-audio-events","events":[{"event":"Voice","normal":{"node_id":1},"doubled":{"node_id":4}}]})");
        const auto audio = ReadActorAudioCatalog(path);
        Check(audio.events.size() == 1 && audio.events[0].event == "Voice" && audio.events[0].normal_node == 1 &&
              audio.events[0].doubled_node == 4 && audio.native_cues.empty() && audio.native_cue_issues.empty());
        write(
            R"({"format":"hksr.actor-audio-events","events":[],"native_cues":[{"source_config":"actor","event_file":"audio","state":"ready","event":"voice","cue":"ultra_ready"}]})");
        const auto coverage = ReadActorAudioCatalog(path);
        Check(coverage.events.empty() && coverage.native_cues.size() == 1 &&
              coverage.native_cues[0].cue == api::VoiceCue::UltraReady);
        ActorVoiceCue cue;
        cue.source_config = "actor";
        cue.state = "ready";
        cue.cue = api::VoiceCue::UltraReady;
        Check(UsesNativeVoice(coverage.native_cues, cue, "voice", {"audio"}));
        Check(!UsesNativeVoice(coverage.native_cues, cue, "voice", {"different"}));
        cue.state = "attack";
        Check(!UsesNativeVoice(coverage.native_cues, cue, "voice", {"audio"}));
        cue.state = "ready";
        cue.source_config = "other";
        Check(!UsesNativeVoice(coverage.native_cues, cue, "voice", {"audio"}));
        cue.source_config = "actor";
        cue.cue = api::VoiceCue::LightHit;
        Check(!UsesNativeVoice(coverage.native_cues, cue, "voice", {"audio"}));
        write(R"({"format":"hksr.actor-audio-events","events":[
            {"event":"A","normal":{"node_id":1},"doubled":{"node_id":4}},
            {"event":"B","normal":{"node_id":0},"doubled":{"node_id":4}},
            {"event":"","normal":{"node_id":1},"doubled":{"node_id":4}},
            {"event":"negative","normal":{"node_id":1},"doubled":{"node_id":-1}},
            {"event":"C","normal":{"node_id":1},"doubled":{"node_id":4}},
            {"event":"C","normal":{"node_id":2},"doubled":{"node_id":4}},
            {"event":"C","normal":{"node_id":1},"doubled":{"node_id":4}},
            {"event":"D","normal":{"node_id":3},"doubled":{"node_id":4}},
            {"event":"D","normal":{"node_id":3},"doubled":{"node_id":4}}],
            "native_cues":[{"source_config":"bad","cue":"ultra_ready"},
                {"source_config":"good","event_file":"audio","state":"ready","event":"voice","cue":"ultra_ready"}],
            "coverage":false,"unresolved_events":7})");
        const auto partial = ReadActorAudioCatalog(path);
        Check(partial.events.size() == 2 && partial.events[0].event == "A" && partial.events[1].event == "D" &&
              partial.issues.size() == 4);
        Check(partial.native_cues.size() == 1 && partial.native_cue_issues.size() == 1);
        cue.source_config = "bad";
        cue.cue = api::VoiceCue::UltraReady;
        Check(!NativeVoiceCoverageError(partial.native_cue_issues, cue).empty());
        cue.cue = api::VoiceCue::LightHit;
        Check(NativeVoiceCoverageError(partial.native_cue_issues, cue).empty());
        cue.source_config = "good";
        cue.cue = api::VoiceCue::UltraReady;
        Check(NativeVoiceCoverageError(partial.native_cue_issues, cue).empty() &&
              UsesNativeVoice(partial.native_cues, cue, "voice", {"audio"}));
        write(
            R"({"format":"hksr.actor-audio-events","events":[{"event":"A","normal":{"node_id":1},"doubled":{"node_id":4}}],"native_cues":false})");
        const auto malformed = ReadActorAudioCatalog(path);
        Check(malformed.events.size() == 1 && malformed.issues.empty() && malformed.native_cue_issues.size() == 1);
        Check(!NativeVoiceCoverageError(malformed.native_cue_issues, cue).empty());
        for (const char* text :
             {R"({"format":"wrong","events":[]})", R"({"format":"hksr.actor-audio-events","events":{}})", "{"}) {
            write(text);
            bool rejected = false;
            try {
                ReadActorAudioCatalog(path);
            } catch (const std::exception&) {
                rejected = true;
            }
            Check(rejected);
        }
    }
    {
        api::CharacterCatalog rows;
        api::PlayerCatalogRow normal{10, 100, 0, 0, "Player/A", "A.prefab"};
        normal.role = 42;
        normal.name = "Official name";
        auto enhanced = normal;
        enhanced.enhanced = 1;
        auto path = normal;
        path.id = 11;
        path.avatar = 101;
        path.character_config = "Player/B";
        auto other = normal;
        other.id = 12;
        other.avatar = 200;
        other.role = 43; // same label is not a shared Role
        rows.players.rows = {normal, enhanced, path, other};
        api::CharacterSkinCatalog skins;
        skins.rows = {{700, 100, "Official skin"}, {701, 101, "Other path skin"}};
        const auto groups = GroupPlayerSources(rows, &skins);
        Check(groups.size() == 2 && groups[0].players.size() == 3 && groups[0].skins.size() == 2);
        Check(PlayerForms(groups[0], rows) == std::vector<std::uint32_t>({100, 101}));
        Check(PlayerEnhancements(groups[0], rows, 100) == std::vector<std::uint32_t>({0, 1}));
        const auto normal_looks = PlayerAppearances(groups[0], rows, &skins, 100, 0);
        const auto enhanced_looks = PlayerAppearances(groups[0], rows, &skins, 100, 1);
        const auto path_looks = PlayerAppearances(groups[0], rows, &skins, 101, 0);
        Check(normal_looks.size() == 2 && normal_looks[0] == PlayerAppearanceChoice{false, 0} &&
              normal_looks[1] == PlayerAppearanceChoice{true, 0});
        Check(enhanced_looks.size() == 1 && enhanced_looks[0] == PlayerAppearanceChoice{false, 1});
        Check(path_looks.size() == 2 && path_looks[1] == PlayerAppearanceChoice{true, 1});
        Check(PlayerAppearances(groups[0], rows, &skins, 101, 1).empty());
        // Missing official relation never falls back to equal text or resources.
        rows.players.rows[0].role = rows.players.rows[1].role = rows.players.rows[2].role = 0;
        Check(GroupPlayerSources(rows, &skins).size() == 3);
        Check(rows.players.rows[1].enhanced == 1 && rows.players.rows[0].id == 10); // selection identity retained
    }
    {
        api::CharacterSkinStatus skin;
        skin.prepared = true;
        skin.skin_id = 1100101;
        skin.avatar_id = 1001;
        skin.adventure_model = "Skin/Player.prefab";
        skin.battle_model = "Skin/Battle.prefab";
        skin.adventure_config = "Player/Original";
        skin.battle_config = "Battle/Original";
        skin.adventure_event_files = {"Skin/AdventureAudio"};
        skin.battle_event_files = {"Skin/BattleEffect"};
        const auto package = SourceCatalog::SkinPackage(skin);
        Check(package.construction == ConstructionKind::StoryActor && package.npc_id == 0 && package.avatar_id == 1001);
        Check(package.appearance->template_prefab == skin.adventure_model &&
              package.appearance->character_config == "Player/Original");
        BattlePresentation original{{"Default.prefab", {"Default/Event"}, {"Excluded"}, "Battle/Original"},
                                    {true, "StandBy", {}}};
        const auto selected = SourceCatalog::SkinPresentation(original, skin);
        Check(selected.model.model_path == skin.battle_model && selected.model.event_files == skin.battle_event_files);
        Check(selected.model.character_config == "Battle/Original" &&
              selected.model.excluded_layers == original.model.excluded_layers);
        Check(selected.initial_state.value == "StandBy" && original.model.model_path == "Default.prefab");
        PreparedSource ready;
        ready.summary.finished = true;
        ready.summary.character = package;
        ready.summary.presentation = selected;
        ready.summary.battle.status = SourceResolution::Ready;
        ready.summary.battle.candidates = {{1001, "Battle/Original", "Default.prefab"}};
        const auto plan = CharacterPlan::FromPrepared(ready);
        Check(plan.action_sources.size() == 2 && plan.action_sources[0].source.path == "Player/Original" &&
              plan.action_sources[1].source.path == "Battle/Original");
        bool rejected = false;
        skin.battle_config = "Other/Avatar";
        try {
            SourceCatalog::SkinPresentation(original, skin);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        Check(rejected);
    }
    {
        api::CharacterCatalog data;
        // Reverse order, repeated IDs with different variants: no truncation,
        // hidden deduplication or dependence on container enumeration order.
        for (unsigned i = 3000; i > 0; --i)
            data.npcs.rows.push_back({i, "Config/NPC/Character" + std::to_string(i) + ".json", "Model"});
        data.players.rows = {{9, 9, 0, 1, "Config/Player/A.json", "Skin.prefab"},
                             {9, 9, 0, 0, "Config/Player/A.json", "Base.prefab"}};
        auto all = SourceIndices(data.npcs.rows, "");
        Check(all.size() == 3000 && data.npcs.rows[all.front()].id == 1 && data.npcs.rows[all.back()].id == 3000);
        Check(SourceIndices(data.players.rows, "").size() == 2 &&
              SourceIndices(data.players.rows, "skin") == std::vector<std::size_t>{0});
        Check(SourceIndices(data.players.rows, "CONFIG a.JSON").size() == 2);
        // Optional replay of a real captured whole catalog; never loads game
        // modules, copies resources or needs per-character user operations.
        char* environment = nullptr;
        std::size_t environment_size = 0;
        Check(_dupenv_s(&environment, &environment_size, "HKSR_CATALOG_EVIDENCE") == 0);
        const std::unique_ptr<char, decltype(&std::free)> storage(environment, &std::free);
        const auto path = storage.get();
        if (path && *path) {
            std::ifstream input(path);
            Check(bool(input));
            std::string line;
            bool found = false;
            while (std::getline(input, line)) {
                const auto j = nlohmann::json::parse(line);
                if (j.value("event", "") != "world-runtime-catalog-end")
                    continue;
                found = true;
                data = {};
                for (const auto& r : j.at("npcs").at("rows"))
                    data.npcs.rows.push_back({r.at("id"), r.at("character_config"), r.at("model_config")});
                for (const auto& r : j.at("players").at("rows"))
                    data.players.rows.push_back({r.at("id"), r.at("avatar"), r.at("enhanced"), r.at("skin"),
                                                 r.at("character_config"), r.at("prefab")});
                const auto audit = [](const auto& rows) {
                    Check(SourceIndices(rows, "").size() == rows.size());
                    for (std::size_t i = 0; i < rows.size(); ++i) {
                        const auto matches = SourceIndices(rows, SourceFold(rows[i].character_config));
                        Check(std::find(matches.begin(), matches.end(), i) != matches.end());
                    }
                };
                audit(data.npcs.rows);
                audit(data.players.rows);
                std::cout << "Whole captured source-list replay passed: NPC=" << data.npcs.rows.size()
                          << " Player=" << data.players.rows.size()
                          << "; every row and lowercase full-path search retained.\n";
            }
            Check(found);
        }
    }
    api::CharacterCatalog rows;
    rows.npcs.enumerated = rows.players.enumerated = rows.avatars.enumerated = true;
    rows.npcs.rows = {{91, "NPC/A", "Model/A"}, {92, "NPC/B", "Model/B"}};
    api::PlayerCatalogRow p{20, 777, 1, 0, "Player/A", "Player/A.prefab"};
    rows.players.rows = {p};
    rows.avatars.rows = {{777, "Battle/A", "Battle/A.prefab"}, {777, "Battle/A", "Battle/A.prefab"}};
    SourceCatalog sources(rows);
    auto package = SourceCatalog::PlayerPackage(p);
    Check(package.source == SourceKind::Official && package.npc_id == 0 &&
          package.construction == ConstructionKind::StoryActor && package.avatar_id == 777 &&
          package.appearance->template_prefab == "Player/A.prefab" && package.appearance->weapon_config == "Player/A");
    auto pure = SourceCatalog::NpcPackage(rows.npcs.rows.front());
    Check(!pure.appearance && pure.npc_id == 91);
    package.source = SourceKind::External;
    package.appearance->asset_directory = "assets";
    Check(sources.PackageSources(package).size() == 1);
    package.appearance->template_prefab = "NPC/A.prefab";
    Check(sources.PackageSources(package).empty()); // not name/ID inference
    auto battle = sources.Battle(777);
    Check(battle.status == SourceResolution::Ready && battle.candidates.size() == 1);
    auto partial_rows = rows;
    partial_rows.avatars.issues.push_back({4, "row", "unrelated row unavailable"});
    Check(SourceCatalog(partial_rows).Battle(777).status == SourceResolution::Ready);
    api::PresentationConfigInput input{"Battle/A", {true, "StandBy", {}}, {true, {"Event/A"}, {}}, {true, {}, {}}};
    auto presentation = SourceCatalog::Presentation(battle.candidates[0], input);
    Check(presentation.model.model_path == "Battle/A.prefab" && presentation.model.event_files.size() == 1 &&
          presentation.initial_state.value == "StandBy");
    Check(presentation.model.character_config == "Battle/A");
    input.initial_state = {false, {}, "absent"};
    Check(!SourceCatalog::Presentation(battle.candidates[0], input)
               .initial_state.available); // optional, no whole-source gate
    rows.avatars.rows.push_back({777, "Battle/B", "Battle/B.prefab"});
    Check(SourceCatalog(rows).Battle(777).status == SourceResolution::Ambiguous);
    rows.avatars.rows.pop_back();
    int presentation_reads = 0;
    SourcePreparation prepare(
        rows,
        {[&](const std::string& path) {
             ++presentation_reads;
             return api::PresentationConfigInput{path, {true, "StandBy", {}}, {true, {"Event/A"}, {}}, {true, {}, {}}};
         }});
    prepare.Player({20, 1, 0});
    prepare.Advance();
    Check(prepare.Status().summary.finished);
    Check(prepare.Status().summary.character->npc_id == 0 &&
          prepare.Status().summary.character->construction == ConstructionKind::StoryActor &&
          prepare.Status().summary.presentation &&
          presentation_reads == 1);
    prepare.Advance();
    Check(presentation_reads == 1);
    prepare.Player({20, 1, 0});
    prepare.Advance();
    Check(prepare.Status().summary.finished && presentation_reads == 1); // cached immutable inputs, no reload
    const auto taken = prepare.Take();
    Check(taken.summary.finished && taken.summary.character &&
          taken.summary.character->construction == ConstructionKind::StoryActor);
    Check(!prepare.Status().summary.finished && !prepare.Status().summary.character && !prepare.Status().skin);
    prepare.Advance();
    Check(prepare.Status().summary.finished && !prepare.Status().summary.character);
    Check(presentation_reads == 1); // Take clears the selection, not the shared presentation cache
    auto authored = SourceCatalog::PlayerPackage(p);
    authored.source = SourceKind::External;
    authored.construction = ConstructionKind::MapNpc;
    authored.npc_id = 91;
    prepare.Package(authored);
    prepare.Advance();
    Check(prepare.Status().summary.character->npc_id == 91 && prepare.Status().summary.presentation &&
          presentation_reads == 1);
    prepare.Player({20, 0, 0});
    Check(prepare.Status().summary.finished && !prepare.Status().summary.character); // not the first variant
    SourcePreparation failed(
        rows, {[](const auto&) -> api::PresentationConfigInput { throw std::runtime_error("config failure"); }});
    failed.Package(authored);
    failed.Advance();
    Check(failed.Status().summary.finished && failed.Status().summary.character &&
          !failed.Status().summary.presentation &&
          failed.Status().summary.errors.size() == 1);
}
