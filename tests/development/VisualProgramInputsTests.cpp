#include "../../tools/VisualProgramInputs.hpp"
#include <hksr/framework/tasks/TaskCatalog.hpp>
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace {
void Check(bool ok) {
    if (!ok)
        throw std::runtime_error("script input test failed");
}
template <class F> void Reject(F call) {
    bool failed = false;
    try {
        call();
    } catch (const std::exception&) {
        failed = true;
    }
    Check(failed);
}
void Write(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    output << text;
    output.close();
    Check(static_cast<bool>(output));
}
std::string Script(const std::string& id, const std::string& metadata = {}) {
    return "{\"format\":\"hksr.script\",\"id\":\"" + id +
           "\",\"applies_to\":\"character\",\"resources\":{\"effects\":{}}," + metadata +
           R"("actions":{"idle":{"run":[{"op":"animation","state":"StandBy"}]}}})";
}
}
void TestVisualProgramInputs() {
    namespace fs = std::filesystem;
    using hksr::research::ReadVisualProgramInputs;
    using hksr::research::ReadTaskCatalogInputs;
    const auto directory = fs::temp_directory_path() /
                           ("hksr-scripts-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Check(fs::create_directory(directory));
    struct Cleanup {
        fs::path directory;
        ~Cleanup() {
            std::error_code error;
            fs::remove_all(directory, error);
        }
    } cleanup{directory};
    const auto scripts = directory / "Scripts";
    Check(ReadVisualProgramInputs(scripts).empty());
    Reject([&] { ReadTaskCatalogInputs(scripts); });
    fs::create_directory(scripts);
    Check(ReadTaskCatalogInputs(scripts).empty());
    const auto file = scripts / "nested" / "character.script.json";
    Write(file, Script("test", R"("defaults":{"choices":{"prize":"minor"},
        "anchors":{"center":{"position":[6,1,58],"rotation":[0,1,0,0]}}},)"));
    Write(scripts / "ignored.json", "{not a script");
    Write(scripts / "ignored.map.json", "{diagnostic mapping");
    const auto inputs = ReadVisualProgramInputs(scripts);
    Check(inputs.sources.size() == 1 && inputs.catalogs.size() == 1);
    const auto& input = *inputs.sources.at("character");
    Check(inputs.sources.at("character") == inputs.catalogs.at("test"));
    Check(input.tasks->tasks.count("idle") == 1 && input.program->id == "test" &&
          input.program == input.tasks->visual && input.target_characters.empty());
    Check(input.choices.at("prize") == "minor" && input.anchors.at("center").position.x == 6 &&
          input.anchors.at("center").rotation.y == 1);

    auto overrides = std::make_shared<hksr::research::VisualProgramOverrides>();
    const auto providers = hksr::research::CreateVisualProgramProviders(overrides);
    const auto read = providers.read_parameters;
    overrides->Set(7, {{{"Target", 9}}, {{"prize", "grand"}}});
    const auto snapshot = read(7);
    Check(snapshot.targets.at("Target") == 9 && snapshot.choices.at("prize") == "grand" && read(8).choices.empty());
    overrides->Set(7, {{{"Target", 10}}, {{"prize", "minor"}}});
    Check(snapshot.targets.at("Target") == 9 && read(7).targets.at("Target") == 10);
    overrides->Set(7, {});
    Check(read(7).targets.empty() && read(7).choices.empty());
    hksr::framework::VisualProgramParameters spatial;
    spatial.anchors = hksr::framework::ParseVisualSpatialAnchors(R"({"center":{"position":[7,2,59]}})");
    overrides->Set(7, spatial);
    const auto held = read(7);
    Check(held.anchors.at("center").position.x == 7 && held.anchors.at("center").rotation.w == 1);
    overrides->Set(7, {});
    Check(read(7).anchors.empty() && held.anchors.size() == 1 && input.anchors.at("center").position.x == 6);
    hksr::framework::VisualProgramParameters positions;
    positions.position_targets.emplace("Point", hksr::framework::VisualSpatialAnchor{{1, 2, 3}, {0, 0, 0, 1}});
    overrides->Set(7, positions);
    const auto held_positions = read(7);
    Check(held_positions.position_targets.at("Point").position.y == 2 && held_positions.anchors.empty());
    hksr::framework::VisualProgramParameters aliases;
    aliases.target_aliases.emplace("Target", "Other");
    overrides->Set(7, aliases);
    Check(read(7).target_aliases.at("Target") == "Other" && read(7).position_targets.empty() &&
          held_positions.position_targets.size() == 1);
    hksr::framework::VisualProgramParameters collections;
    collections.target_collections.emplace("Group", std::vector<std::string>{});
    overrides->Set(7, collections);
    const auto held_collections = read(7);
    Check(held_collections.target_collections.count("Group") == 1 &&
          held_collections.target_collections.at("Group").empty() && held_collections.target_aliases.empty());
    collections.target_collections.at("Group") = {"Target", "Caster"};
    overrides->Set(7, collections);
    Check(read(7).target_collections.at("Group").size() == 2 && held_collections.target_collections.at("Group").empty());
    overrides->Set(7, {});
    const auto cleared = read(7);
    Check(cleared.targets.empty() && cleared.choices.empty() && cleared.anchors.empty() &&
          cleared.position_targets.empty() && cleared.target_aliases.empty() && cleared.target_collections.empty());
    overrides->Push(7, {11, 3, "enter_view"});
    overrides->Push(7, {11, 3, "exit_view"});
    Check(providers.take_signals(8).empty());
    const auto signals = providers.take_signals(7);
    Check(signals.size() == 2 && signals[0].generation == 11 && signals[0].intent == 3 &&
          signals[0].name == "enter_view" && signals[1].name == "exit_view" && providers.take_signals(7).empty());

    const auto original = ReadTaskCatalogInputs(scripts);
    for (const auto* metadata : {
             R"("default":"yes",)",
             R"("defaults":{"targets":{"Target":7}},)",
             R"("defaults":{"choices":{"prize":1}},)",
             R"("defaults":{"anchors":{"center":{"position":[1,2,3],"rotation":[0,0,0,0]}}},)"}) {
        Write(file, Script("test", metadata));
        Reject([&] { ReadVisualProgramInputs(scripts); });
    }
    Write(file, Script("test", R"("defaults":{"targets":{"Alias":{"alias":"Caster"},
        "Forward":{"local_position":[0,0,3]}}},)"));
    const auto declarations = ReadVisualProgramInputs(scripts);
    Check(declarations.sources.at("character")->target_characters.empty() &&
          std::get<std::string>(declarations.sources.at("character")->program->target_defaults.at("Alias")) == "Caster" &&
          std::get<hksr::api::Position3>(declarations.sources.at("character")->program->target_defaults.at("Forward")).z == 3);
    Write(file, Script("test", "\"default\":false,"));
    const auto explicit_only = ReadVisualProgramInputs(scripts);
    Check(explicit_only.sources.empty() && explicit_only.catalogs.count("test") == 1);
    Write(file, Script("test"));
    const auto second = scripts / "other.script.json";
    Write(second, Script("other"));
    Reject([&] { ReadVisualProgramInputs(scripts); });
    Write(file, Script("test", "\"default\":false,"));
    Write(second, Script("other", "\"default\":false,"));
    const auto all_explicit = ReadVisualProgramInputs(scripts);
    Check(all_explicit.catalogs.size() == 2 && all_explicit.sources.empty());
    Write(file, Script("test", "\"default\":true,"));
    const auto selected = ReadVisualProgramInputs(scripts);
    Check(selected.catalogs.size() == 2 && selected.sources.at("character") == selected.catalogs.at("test"));
    Write(second, Script("other", "\"default\":true,"));
    Reject([&] { ReadVisualProgramInputs(scripts); });
    Write(second, Script("test"));
    Reject([&] { ReadVisualProgramInputs(scripts); });
    fs::remove(second);

    Write(file, R"({"format":"hksr.task-catalog","id":"old"})");
    Reject([&] { ReadVisualProgramInputs(scripts); });
    Write(file, Script("updated"));
    const auto updated = ReadTaskCatalogInputs(scripts);
    Check(updated.count("updated") == 1 && original.count("test") == 1 &&
          original.at("test")->choices.at("prize") == "minor");
    Write(file, "{broken");
    Reject([&] { ReadTaskCatalogInputs(scripts); });
    Check(updated.count("updated") == 1);
    {
        const std::string module = R"({"resources":{"effects":{"common.glow":{"path":"glow.prefab","target":"Caster"}}},
          "states":{"common.mode":{"state_only":true,"values":{"enabled":false}}},
          "templates":{"common.action":{"inputs":{"element":{"type":"integer","default":1}},
            "run":[{"op":"effect.create","effects":["common.glow"]}]}}})";
        const auto reader = [&](const std::string& name) { Check(name == "Common/Effects.json"); return module; };
        auto parse = [&](int element) {
            return hksr::framework::ParseScript(
                "{\"format\":\"hksr.script\",\"id\":\"imported\",\"applies_to\":\"fixture\","
                "\"imports\":[\"Common/Effects.json\"],\"actions\":{\"show\":{\"use\":\"common.action\","
                "\"inputs\":{\"element\":{\"default\":" + std::to_string(element) + "}}}}}", reader);
        };
        const auto imported_first = parse(2), imported_second = parse(4);
        Check(imported_first->program != imported_second->program && imported_first->tasks != imported_second->tasks);
        Check(imported_first->tasks->tasks.at("show").definition.inputs.front().default_value ==
              hksr::framework::TaskValue{std::int64_t(2)});
        Check(imported_second->tasks->tasks.at("show").definition.inputs.front().default_value ==
              hksr::framework::TaskValue{std::int64_t(4)});
        Check(imported_first->program->effects.at("common.glow").path == "glow.prefab");
        Check(imported_first->tasks->contexts.at("common.mode").state_only);
        Reject([&] { hksr::framework::ParseScript(
            R"({"format":"hksr.script","id":"x","applies_to":"fixture","imports":["loop"],"actions":{}})",
            [](const std::string&) { return R"({"imports":["loop"]})"; }); });
        Reject([&] { hksr::framework::ParseScript(
            R"({"format":"hksr.script","id":"x","applies_to":"fixture","actions":{"x":{"use":"missing"}}})"); });
    }
    fs::remove(file);
    Check(ReadTaskCatalogInputs(scripts).empty());
    fs::remove(file.parent_path());
    fs::remove(scripts / "ignored.json");
    fs::remove(scripts / "ignored.map.json");
    fs::remove(scripts);
    Reject([&] { ReadTaskCatalogInputs(scripts); });
}
