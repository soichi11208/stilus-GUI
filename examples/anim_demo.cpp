// examples/anim_demo.cpp — request_animation_frame usage.
//
// Draws a bouncing circle whose position advances by (velocity * dt) each
// frame. Illustrates the delta-time-driven animation model.
#include <cmath>
#include <cstdio>
#include "stilus/gui.hpp"

int main() {
    stilus::Window win("anim-demo", 640, 400);
    if (!win.is_open()) return 1;

    float cx = 100, cy = 100;
    float vx = 220, vy = 170; // px/sec
    const float r = 24;

    auto tick = [&](float dt) {
        cx += vx * dt;
        cy += vy * dt;
        // Reflect at the walls.
        if (cx - r < 0)                 { cx = r; vx = -vx; }
        if (cx + r > win.width())       { cx = win.width() - r; vx = -vx; }
        if (cy - r < 0)                 { cy = r; vy = -vy; }
        if (cy + r > win.height())      { cy = win.height() - r; vy = -vy; }
    };

    // Rearm the raf from inside the callback for continuous animation.
    std::function<void(float)> raf;
    raf = [&](float dt) {
        tick(dt);
        win.request_animation_frame(raf);
    };
    win.request_animation_frame(raf);

    win.on_event([&](const stilus::Event& e) {
        if (e.type == stilus::Event::Type::Close ||
            (e.type == stilus::Event::Type::KeyDown && e.key == stilus::Key::Escape))
            win.close();
    });

    win.on_frame([&](stilus::Canvas& c) {
        c.fill_circle({cx, cy}, r, stilus::Color::rgb(0x4488cc));
    });

    return stilus::App::instance().run();
}
