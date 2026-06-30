// src/platform/platform.hpp - Platform Abstraction Layer (internal)
#pragma once
#include <functional>
#include <memory>
#include <string_view>

#include "stilus/event.hpp"
#include "stilus/geom.hpp"

namespace stilus {
class Canvas;
namespace detail {

// Internal window interface, implemented per-backend (Wayland/X11/Win32).
class WindowImpl {
public:
    virtual ~WindowImpl() = default;

    virtual int  width()  const = 0;
    virtual int  height() const = 0;
    virtual int  scale_factor() const { return 1; }
    virtual bool is_open() const = 0;
    virtual void close() = 0;
    virtual void request_redraw() = 0;

    // Accumulate a damage rect in surface-local coords. Backends use this to
    // drive partial repaints and to tell the compositor which buffer region
    // changed. Default marks the whole surface dirty, i.e. a safe fallback.
    virtual void add_damage(Rect r) { (void)r; request_redraw(); }

    virtual void set_frame_cb(std::function<void(Canvas&)> cb) = 0;
    virtual void set_event_cb(std::function<void(const Event&)> cb) = 0;

    // Pump a single iteration of the event loop for this window/backend.
    // Returns false when the window has been closed.
    virtual bool pump() = 0;
};

std::unique_ptr<WindowImpl> create_window(std::string_view title, int w, int h);

} // namespace detail
} // namespace stilus
