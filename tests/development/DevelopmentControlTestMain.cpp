#include <exception>
#include <iostream>
void TestDevelopmentControl();
void TestVisualProgramInputs();
int main() {
    try {TestDevelopmentControl();TestVisualProgramInputs();return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
