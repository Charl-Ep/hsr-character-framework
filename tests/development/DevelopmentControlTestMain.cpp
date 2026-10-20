#include <exception>
#include <iostream>
#include "../../tools/VisualProgramInputs.hpp"
#include <hksr/framework/tasks/TaskCatalog.hpp>
void TestDevelopmentControl();
void TestVisualProgramInputs();
int main(int argc,char** argv) {
    try {
        if(argc>1) {
            if(argc!=3 || std::string(argv[1])!="--visual-manifest")throw std::invalid_argument("expected --visual-manifest PATH");
            if(!std::filesystem::is_regular_file(argv[2]))throw std::invalid_argument("manifest file absent");
            const auto inputs=hksr::research::ReadVisualProgramInputs(argv[2]);
            for(const auto& [source,input]:inputs.sources)std::cout<<source<<": "<<input->program->id<<" ("<<input->program->actions.size()<<" actions)\n";
            for(const auto& [id,input]:inputs.catalogs)std::cout<<id<<": "<<input->program->source<<" ("<<input->tasks->tasks.size()<<" tasks, explicit selection)\n";
            return 0;
        }
        TestDevelopmentControl();TestVisualProgramInputs();return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
