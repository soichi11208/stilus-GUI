// test/integration/x11_smoke.cpp — real-window smoke test.
//
// Opens a window against whatever display is actually available (X11 in CI
// via xvfb-run; Wayland/X11 locally), pumps the event loop for a few frames,
// and asserts basic invariants. Intentionally fast — no multi-second sleeps —
// so it's cheap to run on every CI build.
#include "test_framework.hpp"
#include "stilus/gui.hpp"

TEST(x11_smoke_window_opens_and_paints) {
    stilus::Window win("smoke-test", 200, 150);
    if (!win.is_open()) throw std::runtime_error("window failed to open");

    int frames = 0;
    win.on_frame([&](stilus::Canvas& c) {
        ++frames;
        c.fill_rect({10, 10, 50, 50}, stilus::Color::rgb(0x4488cc));
        if (frames >= 3) {
            win.close();
        } else {
            win.request_redraw();
        }
    });
    win.on_event([&](const stilus::Event& e) {
        if (e.type == stilus::Event::Type::Close) win.close();
    });

    win.request_redraw();
    // App::run() blocks until every registered window closes. Our frame
    // callback above closes the window after a few paints, bounding the
    // test's runtime without relying on wall-clock sleeps.
    stilus::App::instance().run();

    if (frames < 1) throw std::runtime_error("frame callback never fired");
}
