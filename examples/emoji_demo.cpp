// examples/emoji_demo.cpp — mixed Latin / CJK / color emoji via font fallback.
#include <cstdio>
#include "stilus/gui.hpp"

int main() {
    stilus::Window win("emoji-demo", 640, 320);
    if (!win.is_open()) return 1;

    // Primary Latin face + CJK fallback + color emoji fallback (Noto).
    // Each add_fallback is a no-op if the file is missing on the system.
    auto font = stilus::Font::from_file(
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 28.0f);
    if (font.valid()) {
        auto cjk = stilus::Font::from_file(
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 28.0f, 0);
        if (cjk.valid()) font.add_fallback(std::move(cjk));
        auto em  = stilus::Font::from_file(
            "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf", 28.0f);
        if (em.valid())  font.add_fallback(std::move(em));
    }

    win.on_event([&](const stilus::Event& e) {
        if (e.type == stilus::Event::Type::Close ||
            (e.type == stilus::Event::Type::KeyDown && e.key == stilus::Key::Escape))
            win.close();
    });

    win.on_frame([&](stilus::Canvas& c) {
        if (!font.valid()) return;
        c.draw_text({20, 60},  "Hello 👋 world 🌍",           font,
                    stilus::Color::rgb(0xeeeeee));
        c.draw_text({20, 110}, "こんにちは 🍣 ☕ 🐈",         font,
                    stilus::Color::rgb(0xeeeeee));
        c.draw_text({20, 160}, "Numbers: 42 ⚙️ 🚀 stilus",    font,
                    stilus::Color::rgb(0xeeeeee));
        c.draw_text({20, 210}, "Weather: ☀️ 🌧️ ❄️",           font,
                    stilus::Color::rgb(0xeeeeee));
    });

    return stilus::App::instance().run();
}
