// examples/transform_demo.cpp — exercises Phase 1 features:
// affine transforms (rotate/scale), path clipping, HiDPI awareness.
#include <cmath>
#include <cstdio>
#include "stilus/gui.hpp"

int main() {
    stilus::Window win("transform-demo", 720, 480);
    if (!win.is_open()) return 1;

    auto font = stilus::Font::from_file(
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 20.0f);

    float angle = 0.0f;

    win.on_event([&](const stilus::Event& e) {
        using T = stilus::Event::Type;
        if (e.type == T::KeyDown && e.key == stilus::Key::Escape) win.close();
        if (e.type == T::Close) win.close();
        if (e.type == T::MouseMove) angle = e.x * 0.01f;
        win.request_redraw();
    });

    win.on_frame([&](stilus::Canvas& c) {
        // Background already cleared by the backend.
        std::printf("scale_factor=%d  size=%dx%d\n",
                    win.scale_factor(), win.width(), win.height());

        // 1) Rotated rectangle around screen center.
        {
            float cx = win.width() * 0.5f;
            float cy = win.height() * 0.5f;
            c.push_transform(stilus::Affine::translate(cx, cy));
            c.push_transform(stilus::Affine::rotate(angle));
            c.fill_rounded_rect({-80, -50, 160, 100}, 12.0f,
                                stilus::Color::rgb(0x4a90e2));
            c.pop_transform();
            c.pop_transform();
        }

        // 2) Path clip: clip subsequent draws to a circular region.
        {
            stilus::Path mask;
            mask.add_circle({160, 360}, 80);
            c.push_clip_path(mask);
            // Fill the whole region — only the circular area shows.
            for (int i = 0; i < 6; ++i) {
                c.fill_rect({80.f + i * 20.f, 280.f, 18.f, 160.f},
                            stilus::Color::rgba(0xff5050c0));
            }
            c.pop_clip();
        }

        // 3) Scaled text.
        if (font.valid()) {
            c.push_transform(stilus::Affine::translate(40, 60));
            c.push_transform(stilus::Affine::scale(1.5f));
            c.draw_text({0, 20}, "Phase 1: affine + path clip + HiDPI", font,
                        stilus::Color::rgb(0xeeeeee));
            c.pop_transform();
            c.pop_transform();
        }
    });

    return stilus::App::instance().run();
}
