// examples/clipboard_demo.cpp — read/write the system clipboard.
//
// Ctrl+C: publish a fixed string.
// Ctrl+V: read whatever text is currently on the clipboard and dump it to
//          stdout. Try copying something from another app first.
#include <cstdio>
#include <string>
#include "stilus/gui.hpp"

int main() {
    stilus::Window win("clipboard-demo", 480, 240);
    if (!win.is_open()) return 1;

    auto font = stilus::Font::from_file(
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 16.0f);
    if (font.valid()) {
        // Attach a CJK fallback so pasted 日本語 / 中文 shows up too.
        auto cjk = stilus::Font::from_file(
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 16.0f, 0);
        if (cjk.valid()) font.add_fallback(std::move(cjk));
    }

    std::string status = "Ctrl+C: copy demo string   Ctrl+V: paste to stdout";

    win.on_event([&](const stilus::Event& e) {
        using T = stilus::Event::Type;
        if (e.type == T::Close ||
            (e.type == T::KeyDown && e.key == stilus::Key::Escape)) win.close();
        if (e.type == T::KeyDown && e.mods.ctrl) {
            if (e.key == stilus::Key::C) {
                const char* payload = "hello from stilus 👋";
                stilus::App::instance().clipboard_set_text(payload);
                status = std::string("copied: ") + payload;
                std::printf("copied: %s\n", payload); std::fflush(stdout);
                win.request_redraw();
            } else if (e.key == stilus::Key::V) {
                std::string txt = stilus::App::instance().clipboard_get_text();
                status = "pasted (see stdout): " + txt.substr(0, 40);
                std::printf("pasted: %s\n", txt.c_str()); std::fflush(stdout);
                win.request_redraw();
            }
        }
    });

    win.on_frame([&](stilus::Canvas& c) {
        if (font.valid()) {
            c.draw_text({20, 60}, status, font,
                        stilus::Color::rgb(0xeeeeee));
        }
    });

    return stilus::App::instance().run();
}
