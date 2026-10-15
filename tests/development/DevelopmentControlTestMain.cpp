#include <exception>
#include <iostream>
void TestDevelopmentControl();
int main() {
    try {TestDevelopmentControl();return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
