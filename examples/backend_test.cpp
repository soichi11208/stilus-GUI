// examples/backend_test.cpp
#include "stilus/gui.hpp"
#include <cstdio>

int main() {
    try {
        stilus::Window win("backend-test", 320, 240);
        if (!win.is_open()) {
            std::printf("Failed to create window\n");
            return 1;
        }
        
        std::printf("Window created successfully: %dx%d\n", win.width(), win.height());
        
        win.on_event([&](const stilus::Event& e) {
            using T = stilus::Event::Type;
            switch (e.type) {
                case T::Close: win.close(); break;
                default: break;
            }
        });
        
        stilus::App::instance().run();
    } catch (const std::exception& ex) {
        std::printf("Exception: %s\n", ex.what());
        return 1;
    }
    return 0;
}
