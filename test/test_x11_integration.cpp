// test/test_x11_integration.cpp
#include "test_framework.hpp"
#include <string>
#include <iostream>

int main(int argc, char* argv[]) {
    std::string filter;
    if (argc > 1) {
        filter = argv[1];
    }
    
    std::cout << "X11 Integration Tests" << std::endl;
    std::cout << "=====================" << std::endl;
    
    return test::TestRunner::instance().run(filter);
}
