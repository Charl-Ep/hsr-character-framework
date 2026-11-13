#include <exception>
#include <iostream>
#include "../../tools/VisualProgramInputs.hpp"
#include <hksr/framework/tasks/TaskCatalog.hpp>
void TestDevelopmentControl();
void TestVisualProgramInputs();
int main(int argc, char** argv) {
    try {
        if (argc > 1) {
            if (argc != 3 || std::string(argv[1]) != "--script-directory")
                throw std::invalid_argument("expected --script-directory PATH");
            if (!std::filesystem::is_directory(argv[2]))
                throw std::invalid_argument("script directory absent");
            const auto inputs = hksr::research::ReadVisualProgramInputs(argv[2]);
            for (const auto& [source, input] : inputs.sources)
                std::cout << source << ": " << input->program->id << " ("
                          << (input->tasks ? input->tasks->tasks.size() : 0) << " flow actions, "
                          << input->program->actions.size() << " cued animations)\n";
            for (const auto& [id, input] : inputs.catalogs)
                std::cout << id << ": " << input->program->source << " (" << input->tasks->tasks.size()
                          << " flow actions)\n";
            return 0;
        }
        TestDevelopmentControl();
        TestVisualProgramInputs();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
