// examples/input_probe.cpp — dumps input events for manual smoke-testing
#include <cstdio>
#include "stilus/gui.hpp"

int main() {
    stilus::Window win("input-probe", 640, 480);
    if (!win.is_open()) return 1;

    int frame = 0;
    float mx = 0, my = 0;
    bool down = false;

    win.on_event([&](const stilus::Event& e) {
        using T = stilus::Event::Type;
        switch (e.type) {
            case T::MouseMove:
                mx = e.x; my = e.y;
                std::printf("move   %6.1f %6.1f\n", e.x, e.y);
                break;
            case T::MouseDown:
                down = true;
                std::printf("down   btn=%d at %6.1f %6.1f\n", int(e.button), e.x, e.y);
                break;
            case T::MouseUp:
                down = false;
                std::printf("up     btn=%d at %6.1f %6.1f\n", int(e.button), e.x, e.y);
                break;
            case T::MouseWheel:
                std::printf("wheel  dx=%+.2f dy=%+.2f\n", e.wheel_dx, e.wheel_dy);
                break;
            case T::KeyDown:
                std::printf("keydn  key=0x%x mods=%c%c%c%c\n",
                            unsigned(e.key),
                            e.mods.ctrl  ? 'C':'-',
                            e.mods.shift ? 'S':'-',
                            e.mods.alt   ? 'A':'-',
                            e.mods.super ? 'M':'-');
                if (e.key == stilus::Key::Escape) win.close();
                break;
            case T::KeyUp:
                std::printf("keyup  key=0x%x\n", unsigned(e.key));
                break;
            case T::Focus:   std::printf("focus\n");   break;
            case T::Unfocus: std::printf("unfocus\n"); break;
            case T::Close:   win.close(); break;
            default: break;
        }
        std::fflush(stdout);
        win.request_redraw();
    });

    win.on_frame([&](stilus::Canvas& c) {
        c.clear(stilus::Color::rgb(0x1e1e22));
        float s = down ? 24.f : 12.f;
        c.fill_rect({mx - s/2, my - s/2, s, s},
                    down ? stilus::Color::rgb(0xE5C07B)
                         : stilus::Color::rgb(0x61AFEF));
        ++frame;
    });

    return stilus::App::instance().run();
}
