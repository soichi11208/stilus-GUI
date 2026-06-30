// examples/cjk_demo.cpp — multi-face font fallback + soft wrap with kinsoku.
#include <cstdio>
#include "stilus/gui.hpp"

int main() {
    stilus::Window win("cjk-demo", 720, 540);
    if (!win.is_open()) return 1;

    // Primary: a Latin face. Fallback: Noto Sans CJK JP — picks face 0 of the
    // .ttc which contains the JP face on most distributions.
    auto font = stilus::Font::from_file(
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 20.0f);
    if (font.valid()) {
        auto cjk = stilus::Font::from_file(
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 20.0f, 0);
        if (cjk.valid()) font.add_fallback(std::move(cjk));
        else std::fprintf(stderr, "(no CJK fallback)\n");
    }

    const std::string body =
        "Phase 3: text shaping with CJK fallback.\n"
        "英語と日本語が混ざった文章でも、Noto CJKをフォールバック"
        "として登録すれば、コードポイント単位で適切なフェースから"
        "グリフを取り出して描画します。\n"
        "禁則処理 (kinsoku) もごく基本的なものを実装しています — "
        "行頭に「、」「。」「）」「！」「？」が来ないように改行位置を"
        "ずらします。\n"
        "Long English words like internationalization wrap on whitespace.";

    win.on_event([&](const stilus::Event& e) {
        if (e.type == stilus::Event::Type::Close ||
            (e.type == stilus::Event::Type::KeyDown && e.key == stilus::Key::Escape))
            win.close();
        if (e.type == stilus::Event::Type::Resize) win.request_redraw();
    });

    win.on_frame([&](stilus::Canvas& c) {
        if (!font.valid()) {
            return;
        }
        const float margin = 24.f;
        const float wrap_w = win.width() - margin * 2.f;
        auto lines = font.wrap(body, wrap_w);
        const float line_h = font.line_height();
        float y = margin + font.ascent();
        for (const auto& ln : lines) {
            std::string_view sv(body.data() + ln.start, ln.end - ln.start);
            c.draw_text({margin, y}, sv, font, stilus::Color::rgb(0xeeeeee));
            y += line_h;
            if (y > win.height() - margin) break;
        }
    });

    return stilus::App::instance().run();
}
