// examples/hello.cpp
#include "stilus/gui.hpp"

int main() {
    stilus::Window win("hello-gui", 640, 480);
    if (!win.is_open()) return 1;

    bool down = false;
    float mx = 0, my = 0;

    win.on_event([&](const stilus::Event& e) {
        using T = stilus::Event::Type;
        switch (e.type) {
            case T::MouseMove: mx = e.x; my = e.y; break;
            case T::MouseDown: down = true;  break;
            case T::MouseUp:   down = false; break;
            case T::Close:     win.close();  break;
            default: break;
        }
        win.request_redraw();
    });

    win.on_frame([&](stilus::Canvas& c) {
        c.clear(stilus::Color::rgb(0x202024));
        c.fill_rect({ 40,  40, 120, 80}, stilus::Color::rgb(0xE06B74));
        c.fill_rect({180,  40, 120, 80}, stilus::Color::rgb(0x98C379));
        c.fill_rect({320,  40, 120, 80}, stilus::Color::rgb(0x61AFEF));
        // Cursor marker
        float s = down ? 20.f : 10.f;
        c.fill_rect({mx - s/2, my - s/2, s, s},
                    down ? stilus::Color::rgb(0xE5C07B)
                         : stilus::Color::rgb(0xABB2BF));
    });

    return stilus::App::instance().run();
}
