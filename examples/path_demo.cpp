// examples/path_demo.cpp — arbitrary Path rasterization showcase
#include <cmath>
#include <cstdio>
#include "stilus/gui.hpp"

int main() {
    stilus::Window win("path-demo", 720, 480);
    if (!win.is_open()) return 1;

    auto font = stilus::Font::from_file(
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 18.0f);

    win.on_event([&](const stilus::Event& e) {
        if (e.type == stilus::Event::Type::KeyDown &&
            e.key == stilus::Key::Escape) win.close();
        if (e.type == stilus::Event::Type::Close) win.close();
        win.request_redraw();
    });

    win.on_frame([&](stilus::Canvas& c) {
        using stilus::Color;
        c.clear(Color::rgb(0x1c1c20));

        // 5-point star via explicit path
        stilus::Path star;
        const float cx = 140, cy = 150, R = 90, r = 40;
        for (int i = 0; i < 10; ++i) {
            float ang = float(i) * 3.14159265f / 5.f - 3.14159265f / 2.f;
            float rr  = (i & 1) ? r : R;
            stilus::Vec2 p{cx + rr * std::cos(ang), cy + rr * std::sin(ang)};
            (i == 0) ? star.move_to(p) : star.line_to(p);
        }
        star.close_();
        c.fill_path(star, Color::rgb(0xE5C07B));
        c.draw_text({60, 260}, "lineTo star", font, Color::rgb(0xdddddd));

        // Bezier flower — cubic curves only
        stilus::Path flower;
        const float fx = 380, fy = 150;
        for (int i = 0; i < 6; ++i) {
            float a0 = float(i)     * 3.14159265f / 3.f;
            float a1 = float(i + 1) * 3.14159265f / 3.f;
            stilus::Vec2 p0{fx + 20 * std::cos(a0), fy + 20 * std::sin(a0)};
            stilus::Vec2 p1{fx + 20 * std::cos(a1), fy + 20 * std::sin(a1)};
            stilus::Vec2 c0{fx + 120 * std::cos(a0 + 0.2f),
                         fy + 120 * std::sin(a0 + 0.2f)};
            stilus::Vec2 c1{fx + 120 * std::cos(a1 - 0.2f),
                         fy + 120 * std::sin(a1 - 0.2f)};
            (i == 0) ? flower.move_to(p0) : flower.line_to(p0);
            flower.cubic_to(c0, c1, p1);
        }
        flower.close_();
        c.fill_path(flower, Color{0.38f, 0.70f, 0.93f, 0.8f});
        c.draw_text({320, 260}, "cubic flower", font, Color::rgb(0xdddddd));

        // Even-odd vs NonZero demonstration: 2 concentric stars + inner circle
        stilus::Path rule_demo;
        for (int pass = 0; pass < 2; ++pass) {
            float rx = (pass == 0) ? 90.f : 50.f;
            float ry = (pass == 0) ? 40.f : 22.f;
            float bx = 600, by = 150;
            for (int i = 0; i < 10; ++i) {
                float ang = float(i) * 3.14159265f / 5.f - 3.14159265f / 2.f;
                float rr  = (i & 1) ? ry : rx;
                stilus::Vec2 p{bx + rr * std::cos(ang),
                            by + rr * std::sin(ang)};
                (i == 0) ? rule_demo.move_to(p) : rule_demo.line_to(p);
            }
            rule_demo.close_();
        }
        c.fill_path(rule_demo, Color::rgb(0x98C379), stilus::FillRule::EvenOdd);
        c.draw_text({540, 260}, "even-odd hole", font, Color::rgb(0xdddddd));

        // Rounded-rect via add_rounded_rect helper (uses cubic-Bezier corners)
        stilus::Path rr;
        rr.add_rounded_rect({60, 310, 260, 120}, 22.f);
        c.fill_path(rr, Color::rgb(0xE06B74));

        // Circle via add_circle helper
        stilus::Path circ;
        circ.add_circle({500, 370}, 60.f);
        c.fill_path(circ, Color{0.78f, 0.47f, 0.87f, 0.9f});

        c.draw_text({60, 448}, "helpers: add_rounded_rect / add_circle",
                    font, Color::rgb(0x9aa0a6));
    });

    return stilus::App::instance().run();
}
