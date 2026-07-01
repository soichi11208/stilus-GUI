// test/test_x11_integration.cpp
#include "test_framework.hpp"
#include <csignal>
#include <string>
#include <iostream>
#include <unistd.h>

namespace {

void timeout_handler(int) {
    constexpr char msg[] = "\nX11 integration test timed out\n";
    write(STDERR_FILENO, msg, sizeof(msg) - 1);
    _exit(124);
}

} // namespace

int main(int argc, char* argv[]) {
    std::signal(SIGALRM, timeout_handler);
    alarm(10);

    std::string filter;
    if (argc > 1) {
        filter = argv[1];
    }
    
    std::cout << "X11 Integration Tests" << std::endl;
    std::cout << "=====================" << std::endl;
    
    return test::TestRunner::instance().run(filter);
}
