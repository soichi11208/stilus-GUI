// src/platform/platform.hpp - Platform Abstraction Layer (internal)
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <cstdint>

#include "stilus/event.hpp"
#include "stilus/geom.hpp"

namespace stilus {
class Canvas;
namespace detail {

class PopupImpl;  // defined below

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

    // Register a one-shot animation-frame callback. The default backend
    // implementation is a common fallback that stores the callback and
    // fires it during pump(); backends can override for tighter framing.
    virtual void request_animation_frame(std::function<void(float)> cb) {
        raf_cb_ = std::move(cb);
    }

    // Clipboard, text only. Default: no-op / empty; concrete backends
    // (Wayland via wl_data_device, X11 via XSetSelectionOwner) override.
    virtual void        clipboard_set_text(std::string_view) {}
    virtual std::string clipboard_get_text() { return {}; }

    // Popup factory. Anchor rect is in this window's logical coords. Backend
    // returns nullptr if it can't create the popup (missing globals etc).
    virtual std::unique_ptr<PopupImpl> create_popup(Rect /*anchor*/,
                                                    int /*w*/, int /*h*/) {
        return nullptr;
    }

protected:
    // Populated by request_animation_frame(); backends should invoke and
    // clear this at the appropriate point in their pump() loop.
    std::function<void(float)> raf_cb_;
    // Monotonic timestamp (ms) of the last raf fire. 0 = "no previous frame".
    uint64_t                   raf_last_ms_ = 0;
public:

    // Pump a single iteration of the event loop for this window/backend.
    // Returns false when the window has been closed.
    virtual bool pump() = 0;
};

// Popup surface (menus/tooltips/comboboxes). Owns its own buffer + canvas
// but shares the parent window's backend connection and event loop.
class PopupImpl {
public:
    virtual ~PopupImpl() = default;

    virtual int  width()  const = 0;
    virtual int  height() const = 0;
    virtual bool is_open() const = 0;
    virtual void close() = 0;
    virtual void request_redraw() = 0;

    virtual void set_frame_cb(std::function<void(Canvas&)> cb) = 0;
    virtual void set_event_cb(std::function<void(const Event&)> cb) = 0;
};

// Parent-window-driven popup factory. Returns nullptr if the backend
// doesn't yet support popups.
std::unique_ptr<WindowImpl> create_window(std::string_view title, int w, int h);

} // namespace detail
} // namespace stilus
