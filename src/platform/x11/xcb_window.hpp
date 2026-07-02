// src/platform/x11/xcb_window.hpp
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "../platform.hpp"
#include "../../damage.hpp"

#include <xcb/xcb.h>

namespace stilus {
class Canvas;

namespace xcbw {

// X11 backend using XCB (libxcb). Mirrors the Wayland backend structure.
class Window final : public detail::WindowImpl {
public:
    Window(std::string_view title, int w, int h);
    ~Window() override;

    int  width()  const override { return width_; }
    int  height() const override { return height_; }
    int  scale_factor() const override { return scale_; }
    bool is_open() const override { return open_; }
    void close() override;
    void request_redraw() override { needs_redraw_ = true; }
    void add_damage(Rect r) override { damage_.add(r); needs_redraw_ = true; }

    void set_frame_cb(std::function<void(Canvas&)> cb) override { frame_cb_ = std::move(cb); }
    void set_event_cb(std::function<void(const Event&)> cb) override { event_cb_ = std::move(cb); }

    bool pump() override;

private:
    bool init();

    // Backing pixel buffer used by SoftCanvas. Reallocated on resize.
    struct ShmBuffer {
        uint8_t* data   = nullptr;
        int      width  = 0;
        int      height = 0;
        int      stride = 0;
        size_t   size   = 0;
    };
    ShmBuffer* acquire_buffer_(int w, int h);
    void       destroy_buffer_(ShmBuffer& b);

    void paint_frame_();
    void send_close_();
    void dispatch_event_(xcb_generic_event_t* ev);

private:
    std::string title_;
    int  width_  = 0;
    int  height_ = 0;
    int  scale_  = 1;
    bool open_   = false;
    bool needs_redraw_ = true;
    bool sending_close_ = false;

    xcb_connection_t* conn_   = nullptr;
    xcb_screen_t*     screen_ = nullptr;
    xcb_window_t      root_   = 0;
    xcb_window_t      win_    = 0;
    xcb_gcontext_t    gc_     = 0;
    xcb_visualid_t    visual_ = 0;
    uint8_t           depth_  = 24;

    // Atoms
    xcb_atom_t wm_protocols_atom_ = 0;
    xcb_atom_t wm_delete_atom_    = 0;
    xcb_atom_t net_wm_name_atom_  = 0;
    xcb_atom_t utf8_string_atom_  = 0;
    // Clipboard atoms.
    xcb_atom_t clipboard_atom_    = 0;
    xcb_atom_t targets_atom_      = 0;
    xcb_atom_t incr_atom_         = 0;   // INCR — chunked selection transfers
    xcb_atom_t stilus_paste_atom_ = 0;   // scratch property for paste replies

    // Local clipboard state — used both to answer SelectionRequest events
    // when we own the selection and as the fast path for clipboard_get_text().
    std::string clipboard_local_text_;
    bool        owns_clipboard_ = false;

public:
    void        clipboard_set_text(std::string_view utf8) override;
    std::string clipboard_get_text() override;
private:

    // Input state
    float    mouse_x_         = 0;
    float    mouse_y_         = 0;
    bool     mouse_in_        = false;
    KeyMods  mods_{};

    uint32_t repeat_rate_     = 25;
    uint32_t repeat_delay_ms_ = 400;
    uint32_t held_evkey_      = 0;
    Key      held_key_        = Key::Unknown;
    uint32_t held_codepoint_  = 0;
    char     held_text_[5]    = {0};
    uint64_t held_next_ms_    = 0;

    // Double buffer pool
    std::vector<std::unique_ptr<ShmBuffer>> buffers_;

    // Accumulated damage
    detail::DamageRegion damage_;

    // Callbacks
    std::function<void(Canvas&)>     frame_cb_;
    std::function<void(const Event&)> event_cb_;
};

} // namespace xcbw
} // namespace stilus
