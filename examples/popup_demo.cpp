// examples/popup_demo.cpp — click the button to show a menu-style popup.
//
// Wayland only for now (the X11 backend returns a null popup). The popup
// anchors itself under the button and dismisses when you click outside.
#include <cstdio>
#include <memory>
#include "stilus/gui.hpp"

int main() {
    stilus::Window win("popup-demo", 480, 320);
    if (!win.is_open()) return 1;

    auto font = stilus::Font::from_file(
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 16.0f);

    const stilus::Rect button{40, 40, 160, 32};
    std::unique_ptr<stilus::Popup> popup;

    win.on_event([&](const stilus::Event& e) {
        using T = stilus::Event::Type;
        if (e.type == T::Close ||
            (e.type == T::KeyDown && e.key == stilus::Key::Escape)) win.close();
        if (e.type == T::MouseDown && button.contains({e.x, e.y})) {
            // Anchor the popup under the button.
            popup = std::make_unique<stilus::Popup>(win, button, 200, 120);
            if (popup->is_open()) {
                popup->on_frame([&](stilus::Canvas& c) {
                    if (font.valid()) {
                        c.draw_text({12, 24}, "Menu item A", font,
                                    stilus::Color::rgb(0xeeeeee));
                        c.draw_text({12, 52}, "Menu item B", font,
                                    stilus::Color::rgb(0xeeeeee));
                        c.draw_text({12, 80}, "Menu item C", font,
                                    stilus::Color::rgb(0xeeeeee));
                    }
                });
            } else {
                std::fprintf(stderr, "backend does not support popups\n");
                popup.reset();
            }
        }
    });

    win.on_frame([&](stilus::Canvas& c) {
        // Button.
        c.fill_rounded_rect(button, 6.f, stilus::Color::rgb(0x4a6cff));
        if (font.valid()) {
            c.draw_text({button.x + 12, button.y + 22}, "Open menu", font,
                        stilus::Color::rgb(0xffffff));
        }
        // If a popup was dismissed by the compositor, drop it.
        if (popup && !popup->is_open()) popup.reset();
    });

    return stilus::App::instance().run();
}
