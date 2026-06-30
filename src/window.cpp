// src/window.cpp
#include "stilus/window.hpp"

#include <algorithm>
#include <vector>

#include "stilus/widget.hpp"
#include "platform/platform.hpp"

namespace stilus {

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------
namespace detail {
// Declared in platform.hpp; implemented per-backend below.
extern std::unique_ptr<WindowImpl> create_wayland_window(std::string_view, int, int);
extern std::unique_ptr<WindowImpl> create_x11_window(std::string_view, int, int);

static bool detect_wayland() {
    if (const char* env = std::getenv("WAYLAND_DISPLAY"); env && env[0]) return true;
    return false;
}

std::unique_ptr<WindowImpl> create_window(std::string_view title, int w, int h) {
    // Backend selection: prefer Wayland, fall back to X11
    if (detect_wayland()) {
        return create_wayland_window(title, w, h);
    }
    return create_x11_window(title, w, h);
}
} // namespace detail

// ---------------------------------------------------------------------------
// App
// ---------------------------------------------------------------------------
struct App::Impl {
    std::vector<detail::WindowImpl*> windows;
};

App::App() : p_(std::make_unique<Impl>()) {}
App::~App() = default;

App& App::instance() {
    static App a;
    return a;
}

void App::register_window(detail::WindowImpl* w)   { p_->windows.push_back(w); }
void App::unregister_window(detail::WindowImpl* w) {
    auto& v = p_->windows;
    v.erase(std::remove(v.begin(), v.end(), w), v.end());
}

int App::run() {
    while (!p_->windows.empty()) {
        for (size_t i = 0; i < p_->windows.size();) {
            auto* w = p_->windows[i];
            if (!w->pump()) {
                p_->windows.erase(p_->windows.begin() + i);
            } else {
                ++i;
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------
Window::Window(std::string_view title, int w, int h)
    : impl_(detail::create_window(title, w, h)) {
    App::instance().register_window(impl_.get());
}

Window::~Window() {
    if (impl_) App::instance().unregister_window(impl_.get());
}

void Window::on_frame(std::function<void(Canvas&)> cb)      { impl_->set_frame_cb(std::move(cb)); }
void Window::on_event(std::function<void(const Event&)> cb) { impl_->set_event_cb(std::move(cb)); }
void Window::request_redraw() { impl_->request_redraw(); }
void Window::close()          { impl_->close(); }
bool Window::is_open() const  { return impl_->is_open(); }
int  Window::width()  const   { return impl_->width();  }
int  Window::height() const   { return impl_->height(); }
int  Window::scale_factor() const { return impl_->scale_factor(); }

void Window::set_root(std::unique_ptr<Widget> root) {
    root_ = std::move(root);
    if (root_) {
        root_->set_root_notify(impl_.get());

        // Install widget-aware frame + event callbacks that dispatch into
        // the tree. Existing user on_frame/on_event still overwrite these if
        // called afterwards.
        auto* root_ptr = root_.get();
        auto* impl     = impl_.get();
        auto& theme    = theme_;

        impl->set_frame_cb([root_ptr, &theme, impl](Canvas& c) {
            Constraints cc{0, (float)impl->width(), 0, (float)impl->height()};
            root_ptr->measure(cc);
            root_ptr->layout({0, 0, (float)impl->width(), (float)impl->height()});
            root_ptr->paint(c, theme);
            root_ptr->paint_overlay(c, theme);
        });
        impl->set_event_cb([root_ptr, impl](const Event& e) {
            // Tab cycles focus across the widget tree. Intercept before
            // dispatch so no widget sees Tab as a key event.
            if (e.type == Event::Type::KeyDown && e.key == Key::Tab) {
                std::vector<Widget*> fs;
                root_ptr->collect_focusable(fs);
                if (!fs.empty()) {
                    int cur = -1;
                    for (size_t i = 0; i < fs.size(); ++i) {
                        if (fs[i]->focused()) { cur = int(i); break; }
                    }
                    int n = int(fs.size());
                    int next = e.mods.shift
                        ? (cur <= 0 ? n - 1 : cur - 1)
                        : (cur < 0 ? 0 : (cur + 1) % n);
                    if (cur >= 0) fs[cur]->set_focused(false);
                    fs[next]->set_focused(true);
                }
                return;
            }
            root_ptr->dispatch_event(e);
            if (e.type == Event::Type::Close) impl->close();
        });
    }
}

Widget* Window::root() { return root_.get(); }

} // namespace stilus
