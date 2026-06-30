// test/integration/x11_gpu.cpp
#include "test_framework.hpp"
#include "stilus/gui.hpp"
#include <thread>
#include <chrono>

TEST(x11_gpu) {
    // Test window creation and basic event loop
    stilus::Window win("gpu-test", 320, 240);
    std::cout << "  Window created, showing for 3 seconds..." << std::endl;
    
    // Show window for 3 seconds
    for (int i = 0; i < 30; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    win.close();
    std::cout << "  Window closed" << std::endl;
}
