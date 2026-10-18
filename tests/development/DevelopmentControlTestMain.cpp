#include <exception>
#include <iostream>
#include "../../tools/VisualProgramInputs.hpp"
void TestDevelopmentControl();
void TestVisualProgramInputs();
int main(int argc,char** argv) {
    try {
        if(argc>1) {
            if(argc!=3 || std::string(argv[1])!="--visual-manifest")throw std::invalid_argument("expected --visual-manifest PATH");
            if(!std::filesystem::is_regular_file(argv[2]))throw std::invalid_argument("manifest file absent");
            const auto inputs=hksr::research::ReadVisualProgramInputs(argv[2]);
            for(const auto& [source,input]:inputs)std::cout<<source<<": "<<input->program->id<<" ("<<input->program->actions.size()<<" actions)\n";
            return 0;
        }
        TestDevelopmentControl();TestVisualProgramInputs();return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
