#include "../../tools/VisualProgramInputs.hpp"
#include <hksr/framework/tasks/TaskCatalog.hpp>
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace {
void Check(bool ok){if(!ok)throw std::runtime_error("visual program input test failed");}
}
void TestVisualProgramInputs() {
    namespace fs=std::filesystem;using hksr::research::ReadVisualProgramInputs;
    const auto directory=fs::temp_directory_path()/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    Check(fs::create_directory(directory));
    struct Cleanup {
        fs::path directory;
        ~Cleanup(){std::error_code e;fs::remove(directory/"input.json",e);fs::remove(directory/"scene.json",e);fs::remove(directory,e);}
    } cleanup{directory};
    const auto file=directory/"input.json";
    Check(ReadVisualProgramInputs(file).empty());
    std::ofstream(directory/"scene.json") << R"({"format":"hksr.visual-program","id":"test","source":"character","scope":"scene",
      "effects":{},"actions":{"start":{"state":"Start","restart":true}}})";
    auto write=[&](const std::string& text){std::ofstream output(file);output<<text;output.close();Check(static_cast<bool>(output));};
    write(R"({"programs":[{"file":"scene.json","targets":{"Target":2},"choices":{"prize":"minor"},
      "anchors":{"center":{"position":[6,1,58],"rotation":[0,1,0,0]}}}]})");
    const auto inputs=ReadVisualProgramInputs(file);Check(inputs.sources.size()==1 && inputs.catalogs.empty());
    const auto& input=*inputs.sources.at("character");
    Check(input.program->id=="test" && input.targets.empty() && !input.resolve_character &&
        input.target_characters.at("Target")==2 && input.choices.at("prize")=="minor");
    Check(input.anchors.at("center").position.x==6 && input.anchors.at("center").rotation.y==1);
    auto overrides=std::make_shared<hksr::research::VisualProgramOverrides>();
    const auto bound=hksr::research::BindVisualProgramOverrides(inputs,overrides);
    const auto read=bound.sources.at("character")->read_parameters;
    overrides->Set(7,{{{"Target",9}},{{"prize","grand"}}});
    const auto snapshot=read(7);
    Check(snapshot.targets.at("Target")==9 && snapshot.choices.at("prize")=="grand" && read(8).choices.empty());
    overrides->Set(7,{{{"Target",10}},{{"prize","minor"}}});
    Check(snapshot.targets.at("Target")==9 && read(7).targets.at("Target")==10);
    overrides->Set(7,{});Check(read(7).targets.empty() && read(7).choices.empty());
    hksr::framework::VisualProgramParameters spatial;
    spatial.anchors=hksr::framework::ParseVisualSpatialAnchors(R"({"center":{"position":[7,2,59]}})");
    overrides->Set(7,spatial);const auto held=read(7);
    Check(held.anchors.at("center").position.x==7 && held.anchors.at("center").rotation.w==1);
    overrides->Set(7,{});Check(read(7).anchors.empty() && held.anchors.size()==1 && input.anchors.at("center").position.x==6);
    overrides->Push(7,{11,3,"enter_view"});overrides->Push(7,{11,3,"exit_view"});
    Check(bound.sources.at("character")->take_signals(8).empty());
    const auto signals=bound.sources.at("character")->take_signals(7);
    Check(signals.size()==2 && signals[0].generation==11 && signals[0].intent==3 &&
        signals[0].name=="enter_view" && signals[1].name=="exit_view" && bound.sources.at("character")->take_signals(7).empty());
    Check(input.target_characters.at("Target")==2 && input.choices.at("prize")=="minor" && !input.read_parameters);
    for(const auto* text:{
        R"({"programs":[{"file":"scene.json","targets":{"Target":-1}}]})",
        R"({"programs":[{"file":"scene.json","targets":{"Caster":2}}]})",
        R"({"programs":[{"file":"scene.json","targets":{"Target":1.5}}]})",
        R"({"programs":[{"file":"scene.json","anchors":{"center":{"position":[1,2]}}}]})",
        R"({"programs":[{"file":"scene.json","anchors":{"center":{"position":[1,2,3],"rotation":[0,0,0,0]}}}]})",
        R"({"programs":[{"file":"scene.json"},{"file":"scene.json"}]})",
        R"({"programs":[{"file":"missing.json"}]})",
        R"({"programs":[{"file":"scene.json","unexpected":true}]})"
    }) {
        write(text);bool failed=false;try{ReadVisualProgramInputs(file);}catch(const std::exception&){failed=true;}Check(failed);
    }
    std::ofstream(directory/"scene.json") << R"({
        "format":"hksr.task-catalog","id":"tasks","domain":"battle",
        "visual":{"id":"scene","source":"character","scope":"test","effects":{}},
        "sequences":{"idle":{"entry":{"op":"animation","request":"body","participant":"body","state":"StandBy"}}},
        "tasks":{"idle":{"entry":"idle"}}
    })";
    write(R"({"programs":[{"file":"scene.json"}]})");
    const auto task_inputs=ReadVisualProgramInputs(file);
    Check(task_inputs.sources.empty() && task_inputs.catalogs.at("tasks")->tasks->tasks.count("idle")==1);
    Check(task_inputs.catalogs.at("tasks")->program==task_inputs.catalogs.at("tasks")->tasks->visual &&
        task_inputs.catalogs.at("tasks")->program->actions.empty());
    const auto task_bound=hksr::research::BindVisualProgramOverrides(task_inputs,overrides);
    Check(task_bound.catalogs.at("tasks")->tasks==task_inputs.catalogs.at("tasks")->tasks);
    overrides->Set(7,spatial);
    Check(task_bound.catalogs.at("tasks")->read_parameters(7).anchors.at("center").position.x==7);
    const auto reloaded=hksr::research::ReadBoundTaskCatalogs(file,overrides);
    Check(reloaded.at("tasks")->tasks->tasks.count("idle")==1 && reloaded.at("tasks")->read_parameters(7).anchors.size()==1);
    // File replacement reparses definitions without mutating earlier readers.
    std::ofstream(directory/"scene.json") << R"({
        "format":"hksr.task-catalog","id":"tasks","domain":"battle",
        "visual":{"id":"scene","source":"character","scope":"test","effects":{}},
        "sequences":{},"tasks":{"new_candidate":{}}
    })";
    const auto updated=hksr::research::ReadBoundTaskCatalogs(file,overrides);
    Check(updated.at("tasks")->tasks->tasks.count("new_candidate")==1 && reloaded.at("tasks")->tasks->tasks.count("idle")==1);
    write("{broken");bool rejected=false;
    try{hksr::research::ReadBoundTaskCatalogs(file,overrides);}catch(const std::exception&){rejected=true;}Check(rejected);
    Check(updated.at("tasks")->tasks->tasks.count("new_candidate")==1);
    write(R"({"programs":[]})");Check(hksr::research::ReadBoundTaskCatalogs(file,overrides).empty());
    fs::remove(file);rejected=false;
    try{hksr::research::ReadBoundTaskCatalogs(file,overrides);}catch(const std::exception&){rejected=true;}Check(rejected);
    write(R"({"programs":[{"file":"scene.json"},{"file":"scene.json"}]})");
    bool duplicate=false;try{ReadVisualProgramInputs(file);}catch(const std::invalid_argument&){duplicate=true;}
    Check(duplicate);
}
