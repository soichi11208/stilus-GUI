// stilus/window.hpp - Window class (platform-agnostic public API)
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "event.hpp"
#include "geom.hpp"
#include "theme.hpp"

namespace stilus {

class Canvas; // forward
class Widget;

namespace detail { class WindowImpl; }

class Window {
public:
    Window(std::string_view title, int width, int height);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void on_frame(std::function<void(Canvas&)> cb);
    void on_event(std::function<void(const Event&)> cb);

    // Widget tree. Setting a root clears the on_frame/on_event callbacks
    // (they can still coexist — widget pass runs first).
    void  set_root(std::unique_ptr<Widget> root);
    Widget* root();

    Theme& theme() { return theme_; }
    const Theme& theme() const { return theme_; }

    void request_redraw();
    void close();
    bool is_open() const;

    // Logical size (independent of HiDPI scale). Multiply by scale_factor()
    // to get physical pixels.
    int  width()  const;
    int  height() const;
    int  scale_factor() const;

private:
    std::unique_ptr<detail::WindowImpl> impl_;
    std::unique_ptr<Widget>             root_;
    Theme                               theme_;
    friend class App;
};

class App {
public:
    static App& instance();
    int run(); // blocks; returns exit code

    // registration is done automatically from Window ctor
    void register_window(detail::WindowImpl* w);
    void unregister_window(detail::WindowImpl* w);

private:
    App();
    ~App();
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace stilus
