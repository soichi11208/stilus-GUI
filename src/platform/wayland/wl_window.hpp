// src/platform/wayland/wl_window.hpp
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../platform.hpp"
#include "../../damage.hpp"
#include "wl_proto.hpp"
#include "xkb_parse.hpp"

namespace stilus {
class Canvas;

namespace wlw {

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
    // --- setup -------------------------------------------------------------
    bool init();
    void bind_globals_(); // handles wl_registry.global events

    // --- buffer management (wl_shm) ---------------------------------------
    struct ShmBuffer {
        wl::ObjectId id        = 0;
        uint32_t*    pixels    = nullptr;
        int          width     = 0;
        int          height    = 0;
        int          stride    = 0;
        size_t       size      = 0;
        bool         busy      = false; // held by compositor
        void*        map       = nullptr;
        int          map_fd    = -1;
        size_t       map_size  = 0;
    };
    ShmBuffer* acquire_buffer_(int w, int h);
    void       destroy_buffer_(ShmBuffer& b);

    // --- paint -------------------------------------------------------------
    void paint_frame_();

    // --- event senders -----------------------------------------------------
    void send_close_();

    // --- input setup -------------------------------------------------------
    void setup_seat_(uint32_t caps);
    void on_pointer_event_(uint16_t op, const uint8_t* pl, size_t n);
    void on_keyboard_event_(uint16_t op, const uint8_t* pl, size_t n);
    // text-input-v3
    void setup_text_input_();
    void text_input_enable_();
    void text_input_disable_();
    void on_text_input_event_(uint16_t op, const uint8_t* pl, size_t n);
public:
    // Backend hook used by Window to drive IME enable/disable as focus moves
    // between TextInput widgets in the tree.
    void ime_set_enabled(bool);
private:

private:
    std::string title_;
    int  width_  = 0;       // logical pixels
    int  height_ = 0;       // logical pixels
    bool sending_close_ = false;
    int  scale_  = 1;       // current buffer scale factor (>=1)
    int  applied_scale_ = 0; // last scale we sent to wl_surface::set_buffer_scale
    bool open_   = false;
    bool configured_       = false;
    bool needs_redraw_     = true;
    bool pending_resize_   = false;
    int  pending_w_        = 0;
    int  pending_h_        = 0;

    wl::Display d_;

    // Globals
    wl::ObjectId compositor_   = 0;  uint32_t compositor_ver_ = 0;
    wl::ObjectId shm_          = 0;
    wl::ObjectId xdg_wm_base_  = 0;
    wl::ObjectId seat_         = 0;  uint32_t seat_ver_ = 0;
    wl::ObjectId pointer_      = 0;
    wl::ObjectId keyboard_     = 0;
    // xdg-decoration-unstable-v1: server-side decorations (titlebar + buttons).
    // The manager is bound if the compositor offers it; the toplevel-decoration
    // object is created once we have an xdg_toplevel.
    wl::ObjectId decoration_manager_ = 0;
    wl::ObjectId toplevel_decoration_ = 0;
    // Client-side decorations: drawn when the compositor refuses or doesn't
    // support xdg-decoration. The surface buffer is taller than the user-
    // visible area by `csd_titlebar_h_` pixels; the titlebar lives at the top
    // in surface coords. Pointer events on the titlebar are consumed (move /
    // close / minimize / maximize).
    bool csd_enabled_       = false;
    int  csd_titlebar_h_    = 28;
    bool csd_maximized_     = false;
    int  csd_hover_button_  = -1;   // 0=min 1=max 2=close, -1=none
    int  csd_pressed_button_= -1;
    uint32_t last_pointer_serial_ = 0;
    uint32_t last_pointer_enter_serial_ = 0;
    // Helpers
    int  csd_titlebar_h_eff_() const { return csd_enabled_ ? csd_titlebar_h_ : 0; }
    int  csd_button_at_(float x, float y) const; // -1 if outside any button
    void draw_csd_titlebar_(Canvas& c);
    // Returns true when the event was consumed by the titlebar UI; callers
    // should skip widget dispatch in that case.
    bool csd_handle_pointer_button_(uint32_t serial, bool pressed, float x, float y);
    bool csd_update_hover_(float x, float y);
    // text-input-unstable-v3 (IME). Manager is bound if the compositor
    // advertises it; text_input_ is created lazily once we also have a seat.
    wl::ObjectId text_input_manager_ = 0;
    wl::ObjectId text_input_         = 0;
    bool         text_input_enabled_ = false;
    bool         ime_enabled_        = false;
    // Accumulated preedit/commit fragments since the last `done` event; the
    // text-input-v3 protocol batches them.
    std::string  pending_preedit_;
    int          pending_preedit_cb_ = -1;
    int          pending_preedit_ce_ = -1;
    std::string  pending_commit_;
    bool         pending_has_preedit_ = false;

    // Per-window objects
    wl::ObjectId surface_      = 0;
    wl::ObjectId xdg_surface_  = 0;
    wl::ObjectId xdg_toplevel_ = 0;

    // Outputs: name (global registry name) -> { wl_output id, advertised scale }
    struct OutputInfo { wl::ObjectId id = 0; int scale = 1; };
    std::unordered_map<uint32_t, OutputInfo> outputs_;       // by global name
    std::unordered_map<wl::ObjectId, uint32_t> output_by_id_; // wl_output id -> name
    // wl_output IDs the surface currently overlaps (from surface.enter/leave).
    std::unordered_set<wl::ObjectId> entered_outputs_;

    // Recompute scale_ from entered_outputs_; if it changed, apply and
    // schedule a full redraw with a new buffer size.
    void update_scale_();

    // Input state
    float    mouse_x_         = 0;
    float    mouse_y_         = 0;
    bool     mouse_in_        = false;
    KeyMods  mods_{};

    // XKB keymap parsed from the compositor's wl_keyboard.keymap event.
    // If parse fails we fall back to the hardcoded US table in wl_keymap.hpp.
    XkbKeymap xkb_;
    // Extra mods for xkb that aren't in KeyMods: Caps Lock + Level3 (AltGr).
    bool      caps_lock_       = false;
    bool      level3_          = false;
    // XKB modifier bit positions discovered from the keymap (not strictly
    // needed today — we stick with the core-mod convention below — but kept
    // for when we parse xkb_compatibility.)

    // Key repeat. Compositor advertises rate (chars/sec) and delay (ms) via
    // wl_keyboard.repeat_info. We keep the last pressed key and re-emit
    // KeyDown+TextInput from pump() when the held deadline elapses.
    uint32_t repeat_rate_     = 25;   // chars/sec
    uint32_t repeat_delay_ms_ = 400;  // initial delay
    uint32_t held_evkey_      = 0;    // 0 = no key held
    Key      held_key_        = Key::Unknown;
    uint32_t held_codepoint_  = 0;    // 0 if non-printable
    char     held_text_[5]    = {0};
    uint64_t held_next_ms_    = 0;    // absolute deadline (monotonic)

    // Double buffer pool. unique_ptr storage keeps pointers stable across
    // pool growth (needed by buffer release handlers and last_attached_).
    std::vector<std::unique_ptr<ShmBuffer>> buffers_;
    // Most-recently attached buffer. Used as the pixel source for partial
    // repaints (copy into the next acquired buffer before drawing damage).
    ShmBuffer*             last_attached_ = nullptr;

    // Accumulated damage for the next paint.
    detail::DamageRegion   damage_;

    // Callbacks
    std::function<void(Canvas&)>     frame_cb_;
    std::function<void(const Event&)> event_cb_;
};

} // namespace wlw
} // namespace stilus
