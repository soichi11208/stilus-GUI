// examples/paint_demo.cpp — shows the AA'd primitives and text.
#include <cstdio>
#include "stilus/gui.hpp"

int main() {
    stilus::Window win("paint-demo", 720, 480);
    if (!win.is_open()) return 1;

    auto font = stilus::Font::from_file(
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 20.0f);
    auto font_big = stilus::Font::from_file(
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 32.0f);
    if (!font.valid() || !font_big.valid()) {
        std::fprintf(stderr, "failed to load font\n");
    }

    float mx = 0, my = 0;
    bool  down = false;

    win.on_event([&](const stilus::Event& e) {
        using T = stilus::Event::Type;
        if (e.type == T::MouseMove) { mx = e.x; my = e.y; }
        if (e.type == T::MouseDown) down = true;
        if (e.type == T::MouseUp)   down = false;
        if (e.type == T::KeyDown && e.key == stilus::Key::Escape) win.close();
        if (e.type == T::Close) win.close();
        win.request_redraw();
    });

    win.on_frame([&](stilus::Canvas& c) {
        using stilus::Color;
        c.clear(Color::rgb(0x1e1e22));

        // Header text
        c.draw_text({24, 48}, "Soft renderer demo", font_big, Color::rgb(0xe4e4ea));
        c.draw_text({24, 78}, "analytic AA · source-over · stb_truetype", font,
                    Color::rgb(0x9aa0a6));

        // Rounded-rect palette
        float x = 24, y = 110;
        auto swatch = [&](Color col) {
            c.fill_rounded_rect({x, y, 110, 70}, 14.f, col);
            x += 122;
        };
        swatch(Color::rgb(0xE06B74));
        swatch(Color::rgb(0xD19A66));
        swatch(Color::rgb(0xE5C07B));
        swatch(Color::rgb(0x98C379));
        swatch(Color::rgb(0x61AFEF));
        swatch(Color::rgb(0xC678DD));

        // Strokes demo
        c.stroke_rounded_rect({24, 208, 220, 90}, 16.f, 2.f, Color::rgb(0x61AFEF));
        c.draw_text({40, 258}, "stroke_rounded_rect", font, Color::rgb(0xcaced6));

        // Circle row (varying alpha)
        for (int i = 0; i < 6; ++i) {
            float a = 0.15f + i * 0.17f;
            stilus::Color col{0.38f, 0.82f, 0.47f, a};
            c.fill_circle({280.f + i * 44.f, 252.f}, 18.f, col);
        }

        // Rounded-rect size/radius variations (sub-pixel precision check)
        for (int i = 0; i < 6; ++i) {
            float r = 2.f + i * 4.f;
            c.fill_rounded_rect({24.f + i * 70.f, 320.f, 60.f, 60.f}, r,
                                Color::rgb(0x98C379));
        }
        c.draw_text({24, 404}, "radius 2 → 22 (sub-pixel AA)", font,
                    Color::rgb(0x9aa0a6));

        // Cursor hotspot
        float s = down ? 24.f : 14.f;
        c.fill_circle({mx, my}, s, Color{0.96f, 0.79f, 0.48f, down ? 0.95f : 0.7f});
        c.stroke_rounded_rect({mx - s, my - s, s*2, s*2}, s, 1.f,
                              Color::rgb(0xffffff));
    });

    return stilus::App::instance().run();
}
