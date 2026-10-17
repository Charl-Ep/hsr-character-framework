#include "../../tools/VisualProgramInputs.hpp"
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
    write(R"({"programs":[{"file":"scene.json","targets":{"Target":2},"choices":{"prize":"minor"}}]})");
    const auto inputs=ReadVisualProgramInputs(file);Check(inputs.size()==1);
    const auto& input=*inputs.at("character");
    Check(input.program->id=="test" && input.targets.empty() && !input.resolve_character &&
        input.target_characters.at("Target")==2 && input.choices.at("prize")=="minor");
    for(const auto* text:{
        R"({"programs":[{"file":"scene.json","targets":{"Target":-1}}]})",
        R"({"programs":[{"file":"scene.json","targets":{"Caster":2}}]})",
        R"({"programs":[{"file":"scene.json","targets":{"Target":1.5}}]})",
        R"({"programs":[{"file":"scene.json"},{"file":"scene.json"}]})",
        R"({"programs":[{"file":"missing.json"}]})",
        R"({"programs":[{"file":"scene.json","unexpected":true}]})"
    }) {
        write(text);bool failed=false;try{ReadVisualProgramInputs(file);}catch(const std::exception&){failed=true;}Check(failed);
    }
}
