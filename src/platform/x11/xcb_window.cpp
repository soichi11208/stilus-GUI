// src/platform/x11/xcb_window.cpp
//
// XCB-backed window. Mirrors the Wayland backend's responsibilities:
//   - create an X11 window via xcb_create_window
//   - register WM_DELETE_WINDOW so the close button delivers Close
//   - render into a BGRA pixmap, then xcb_copy_area to the window
//   - convert X11 events into stilus::Event
//
// The implementation deliberately avoids XShm/XKB extensions for now; the
// fallbacks (xcb_put_image + linux-evdev keycode table) are correct and
// portable across all X servers we care about. XShm is a perf optimization
// that can be added once the basic backend is stable.
#include "xcb_window.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <poll.h>
#include <unistd.h>
#include <xcb/xcb.h>

#include "stilus/canvas.hpp"
#include "stilus/event.hpp"
#include "render/soft_canvas.hpp"
// The Wayland backend's evdev tables are correct for Linux input. X11
// keycodes are evdev codes + 8 (XKB convention), so we reuse those tables.
#include "../wayland/wl_keymap.hpp"

namespace stilus::detail {
uint32_t utf8_decode_next(const char*& p, const char* end);
}

namespace stilus::xcbw {

namespace {

uint64_t now_ms() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000 + uint64_t(ts.tv_nsec) / 1000000;
}

// Intern an atom synchronously. Returns 0 on failure.
xcb_atom_t intern(xcb_connection_t* c, const char* name) {
    xcb_intern_atom_cookie_t ck =
        xcb_intern_atom(c, 0, uint16_t(std::strlen(name)), name);
    xcb_intern_atom_reply_t* r = xcb_intern_atom_reply(c, ck, nullptr);
    if (!r) return 0;
    xcb_atom_t a = r->atom;
    free(r);
    return a;
}

// X11 KeyPress/Release `state` bit -> KeyMods.
KeyMods mods_from_state(uint16_t state) {
    KeyMods m;
    m.shift = state & XCB_MOD_MASK_SHIFT;
    m.ctrl  = state & XCB_MOD_MASK_CONTROL;
    m.alt   = state & XCB_MOD_MASK_1;     // typically Alt
    m.super = state & XCB_MOD_MASK_4;     // typically Super
    return m;
}

} // namespace

// ---------------------------------------------------------------------------
Window::Window(std::string_view title, int w, int h)
    : title_(title), width_(w), height_(h) {
    damage_.set_surface(w, h);
    if (!init()) {
        std::fprintf(stderr, "xcbw::Window init failed\n");
        return;
    }
    open_ = true;
}

Window::~Window() {
    for (auto& b : buffers_) destroy_buffer_(*b);
    buffers_.clear();
    if (conn_) {
        if (gc_)  xcb_free_gc(conn_, gc_);
        if (win_) xcb_destroy_window(conn_, win_);
        xcb_flush(conn_);
        xcb_disconnect(conn_);
    }
}

void Window::close() {
    // Don't re-fire Close from close() — that would recurse forever if the
    // user's handler calls close() in response to a Close event.
    open_ = false;
}

void Window::send_close_() {
    if (sending_close_) return;
    sending_close_ = true;
    if (event_cb_) {
        Event e; e.type = Event::Type::Close;
        event_cb_(e);
    }
    sending_close_ = false;
}

// ---------------------------------------------------------------------------
bool Window::init() {
    conn_ = xcb_connect(nullptr, nullptr);
    if (!conn_ || xcb_connection_has_error(conn_)) {
        std::fprintf(stderr, "xcb_connect failed\n");
        return false;
    }

    screen_ = xcb_setup_roots_iterator(xcb_get_setup(conn_)).data;
    root_   = screen_->root;
    depth_  = screen_->root_depth;
    visual_ = screen_->root_visual;

    // Atoms we need.
    wm_protocols_atom_ = intern(conn_, "WM_PROTOCOLS");
    wm_delete_atom_    = intern(conn_, "WM_DELETE_WINDOW");
    net_wm_name_atom_  = intern(conn_, "_NET_WM_NAME");
    utf8_string_atom_  = intern(conn_, "UTF8_STRING");
    clipboard_atom_    = intern(conn_, "CLIPBOARD");
    targets_atom_      = intern(conn_, "TARGETS");
    incr_atom_         = intern(conn_, "INCR");
    stilus_paste_atom_ = intern(conn_, "STILUS_PASTE");

    win_ = xcb_generate_id(conn_);
    uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    uint32_t values[2] = {
        screen_->black_pixel,
        XCB_EVENT_MASK_EXPOSURE          | XCB_EVENT_MASK_KEY_PRESS  |
        XCB_EVENT_MASK_KEY_RELEASE       | XCB_EVENT_MASK_BUTTON_PRESS |
        XCB_EVENT_MASK_BUTTON_RELEASE    | XCB_EVENT_MASK_ENTER_WINDOW |
        XCB_EVENT_MASK_LEAVE_WINDOW      | XCB_EVENT_MASK_POINTER_MOTION |
        XCB_EVENT_MASK_STRUCTURE_NOTIFY  | XCB_EVENT_MASK_FOCUS_CHANGE |
        // PropertyNotify is required for INCR clipboard transfers.
        XCB_EVENT_MASK_PROPERTY_CHANGE
    };
    xcb_create_window(conn_,
        XCB_COPY_FROM_PARENT,
        win_, root_,
        0, 0, uint16_t(width_), uint16_t(height_),
        0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
        visual_,
        mask, values);

    // Register close-button handling: WM_PROTOCOLS property holds atom list
    // including WM_DELETE_WINDOW. Without this, the WM hard-kills our window.
    if (wm_protocols_atom_ && wm_delete_atom_) {
        xcb_change_property(conn_, XCB_PROP_MODE_REPLACE, win_,
            wm_protocols_atom_, XCB_ATOM_ATOM, 32, 1, &wm_delete_atom_);
    }

    // Titles: set both legacy WM_NAME (Latin-1) and _NET_WM_NAME (UTF-8).
    xcb_change_property(conn_, XCB_PROP_MODE_REPLACE, win_,
        XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
        uint32_t(title_.size()), title_.data());
    if (net_wm_name_atom_ && utf8_string_atom_) {
        xcb_change_property(conn_, XCB_PROP_MODE_REPLACE, win_,
            net_wm_name_atom_, utf8_string_atom_, 8,
            uint32_t(title_.size()), title_.data());
    }

    gc_ = xcb_generate_id(conn_);
    xcb_create_gc(conn_, gc_, win_, 0, nullptr);

    xcb_map_window(conn_, win_);
    xcb_flush(conn_);

    needs_redraw_ = true;
    return true;
}

// ---------------------------------------------------------------------------
Window::ShmBuffer* Window::acquire_buffer_(int w, int h) {
    for (auto& b : buffers_) {
        if (b->width == w && b->height == h) return b.get();
    }
    // Free any other-sized buffers; only keep one at a time.
    for (auto& b : buffers_) destroy_buffer_(*b);
    buffers_.clear();

    auto buf = std::make_unique<ShmBuffer>();
    buf->width  = w;
    buf->height = h;
    buf->stride = w * 4;
    buf->size   = size_t(buf->stride) * size_t(h);
    buf->data   = new uint8_t[buf->size]();

    ShmBuffer* ref = buf.get();
    buffers_.push_back(std::move(buf));
    return ref;
}

void Window::destroy_buffer_(ShmBuffer& b) {
    delete[] b.data;
    b.data = nullptr;
}

void Window::paint_frame_() {
    int phys_w = width_  * scale_;
    int phys_h = height_ * scale_;
    ShmBuffer* b = acquire_buffer_(phys_w, phys_h);
    if (!b) return;

    render::SoftCanvas canvas;
    uint32_t* pixels = reinterpret_cast<uint32_t*>(b->data);
    canvas.bind(pixels, b->width, b->height, b->stride / 4);

    needs_redraw_ = false;

    if (scale_ != 1) canvas.push_transform(Affine::scale(float(scale_)));
    canvas.clear(Color::rgb(0x202024));
    if (frame_cb_) frame_cb_(canvas);
    if (scale_ != 1) canvas.pop_transform();

    // xcb_put_image's request length is limited by the server's max request
    // size. Chunk by horizontal strips that fit; the simplest cap is to send
    // a stride that fits in ~256 KiB per request — large enough that almost
    // any window fits in 1–2 chunks, small enough to stay within limits even
    // for very wide pixmaps.
    const size_t max_bytes = 256 * 1024;
    int rows_per_chunk = std::max(1, int(max_bytes / size_t(b->stride)));
    for (int y = 0; y < b->height; y += rows_per_chunk) {
        int rows = std::min(rows_per_chunk, b->height - y);
        xcb_put_image(conn_,
            XCB_IMAGE_FORMAT_Z_PIXMAP,
            win_, gc_,
            uint16_t(b->width), uint16_t(rows),
            0, int16_t(y),
            0, depth_,
            uint32_t(b->stride) * uint32_t(rows),
            b->data + size_t(y) * size_t(b->stride));
    }
    xcb_flush(conn_);

    damage_.clear();
}

// ---------------------------------------------------------------------------
bool Window::pump() {
    if (!open_) return false;

    // Animation-frame callback (see wl_window.cpp for the full rationale).
    if (raf_cb_) {
        uint64_t t = now_ms();
        float dt = raf_last_ms_ == 0 ? 0.0f : float(t - raf_last_ms_) / 1000.0f;
        raf_last_ms_ = t;
        auto cb = std::move(raf_cb_);
        raf_cb_ = nullptr;
        cb(dt);
        needs_redraw_ = true;
    } else {
        raf_last_ms_ = 0;
    }

    if (needs_redraw_) paint_frame_();

    // Process any events already queued.
    for (;;) {
        xcb_generic_event_t* ev = xcb_poll_for_event(conn_);
        if (!ev) break;
        dispatch_event_(ev);
        free(ev);
        if (!open_) return false;
    }

    // Fire any due key repeats (matches Wayland backend behavior).
    if (held_evkey_ && repeat_rate_ > 0 && event_cb_) {
        uint64_t now = now_ms();
        uint32_t period = 1000u / repeat_rate_;
        if (period == 0) period = 1;
        while (held_evkey_ && now >= held_next_ms_) {
            Event e;
            e.type = Event::Type::KeyDown;
            e.key  = held_key_;
            e.mods = mods_;
            event_cb_(e);
            if (held_codepoint_) {
                Event t;
                t.type = Event::Type::TextInput;
                t.mods = mods_;
                t.codepoint = held_codepoint_;
                t.text = held_text_;
                event_cb_(t);
            }
            held_next_ms_ += period;
        }
    }

    // Wait for the next event with a small timeout so request_redraw() picks
    // up promptly. poll() on the xcb fd is the standard pattern.
    int fd = xcb_get_file_descriptor(conn_);
    struct pollfd pfd{ fd, POLLIN, 0 };
    int timeout_ms = needs_redraw_ ? 0 : 16;
    ::poll(&pfd, 1, timeout_ms);
    if (xcb_connection_has_error(conn_)) {
        open_ = false;
        return false;
    }
    return open_;
}

// ---------------------------------------------------------------------------
void Window::dispatch_event_(xcb_generic_event_t* ev) {
    const uint8_t type = ev->response_type & 0x7f;
    switch (type) {
    case XCB_EXPOSE: {
        needs_redraw_ = true;
        break;
    }
    case XCB_CONFIGURE_NOTIFY: {
        auto* c = (xcb_configure_notify_event_t*)ev;
        if (c->width != width_ || c->height != height_) {
            width_  = c->width;
            height_ = c->height;
            damage_.set_surface(width_, height_);
            needs_redraw_ = true;
            if (event_cb_) {
                Event e; e.type = Event::Type::Resize;
                e.width = width_; e.height = height_;
                event_cb_(e);
            }
        }
        break;
    }
    case XCB_CLIENT_MESSAGE: {
        auto* cm = (xcb_client_message_event_t*)ev;
        if (cm->type == wm_protocols_atom_ &&
            cm->data.data32[0] == wm_delete_atom_) {
            open_ = false;
            send_close_();
        }
        break;
    }
    case XCB_KEY_PRESS:
    case XCB_KEY_RELEASE: {
        auto* k = (xcb_key_press_event_t*)ev;
        const bool pressed = (type == XCB_KEY_PRESS);
        // X11 keycode = evdev keycode + 8 (XKB convention used by every
        // sensible modern X server). Subtract to get the linux input code
        // our shared table expects.
        uint32_t evkey = uint32_t(k->detail) - 8;
        mods_ = mods_from_state(k->state);
        Event e;
        e.type = pressed ? Event::Type::KeyDown : Event::Type::KeyUp;
        e.key  = wlw::evdev_to_key(evkey);
        e.mods = mods_;
        if (event_cb_) event_cb_(e);

        uint32_t cp = 0;
        char utf8_buf[5] = {0};
        if (pressed && !mods_.ctrl && !mods_.alt) {
            cp = wlw::evdev_to_codepoint(evkey, mods_.shift);
            if (cp) {
                wlw::utf8_encode(cp, utf8_buf);
                if (event_cb_) {
                    Event t;
                    t.type = Event::Type::TextInput;
                    t.mods = mods_;
                    t.codepoint = cp;
                    t.text = utf8_buf;
                    event_cb_(t);
                }
            }
        }
        if (pressed) {
            held_evkey_     = evkey;
            held_key_       = e.key;
            held_codepoint_ = cp;
            std::memcpy(held_text_, utf8_buf, sizeof(utf8_buf));
            held_next_ms_   = now_ms() + repeat_delay_ms_;
        } else if (evkey == held_evkey_) {
            held_evkey_ = 0;
        }
        break;
    }
    case XCB_BUTTON_PRESS:
    case XCB_BUTTON_RELEASE: {
        auto* b = (xcb_button_press_event_t*)ev;
        const bool pressed = (type == XCB_BUTTON_PRESS);
        mouse_x_ = b->event_x; mouse_y_ = b->event_y;
        // X11 buttons: 1=L, 2=Middle, 3=R, 4-7 are wheel up/down/left/right.
        if (b->detail >= 4 && b->detail <= 7) {
            if (pressed && event_cb_) {
                Event e; e.type = Event::Type::MouseWheel;
                e.x = mouse_x_; e.y = mouse_y_;
                switch (b->detail) {
                    case 4: e.wheel_dy = +1.f; break;  // up
                    case 5: e.wheel_dy = -1.f; break;  // down
                    case 6: e.wheel_dx = -1.f; break;
                    case 7: e.wheel_dx = +1.f; break;
                }
                event_cb_(e);
            }
        } else {
            MouseButton mb = MouseButton::Left;
            switch (b->detail) {
                case 1: mb = MouseButton::Left;   break;
                case 2: mb = MouseButton::Middle; break;
                case 3: mb = MouseButton::Right;  break;
                case 8: mb = MouseButton::X1;     break;
                case 9: mb = MouseButton::X2;     break;
                default: return;
            }
            if (event_cb_) {
                Event e;
                e.type = pressed ? Event::Type::MouseDown : Event::Type::MouseUp;
                e.x = mouse_x_; e.y = mouse_y_;
                e.button = mb;
                event_cb_(e);
            }
        }
        break;
    }
    case XCB_MOTION_NOTIFY: {
        auto* m = (xcb_motion_notify_event_t*)ev;
        mouse_x_ = m->event_x; mouse_y_ = m->event_y;
        if (event_cb_) {
            Event e; e.type = Event::Type::MouseMove;
            e.x = mouse_x_; e.y = mouse_y_;
            event_cb_(e);
        }
        break;
    }
    case XCB_ENTER_NOTIFY: {
        auto* en = (xcb_enter_notify_event_t*)ev;
        mouse_in_ = true;
        mouse_x_ = en->event_x; mouse_y_ = en->event_y;
        if (event_cb_) {
            Event e; e.type = Event::Type::MouseMove;
            e.x = mouse_x_; e.y = mouse_y_;
            event_cb_(e);
        }
        break;
    }
    case XCB_LEAVE_NOTIFY: {
        mouse_in_ = false;
        held_evkey_ = 0;
        break;
    }
    case XCB_FOCUS_IN:
        if (event_cb_) { Event e; e.type = Event::Type::Focus;   event_cb_(e); }
        break;
    case XCB_FOCUS_OUT:
        held_evkey_ = 0;
        mods_ = KeyMods{};
        if (event_cb_) { Event e; e.type = Event::Type::Unfocus; event_cb_(e); }
        break;
    case XCB_SELECTION_REQUEST: {
        // Another client wants to paste from CLIPBOARD; if we own it, hand
        // over the stored text.
        auto* sr = (xcb_selection_request_event_t*)ev;
        xcb_selection_notify_event_t notify{};
        notify.response_type = XCB_SELECTION_NOTIFY;
        notify.time      = sr->time;
        notify.requestor = sr->requestor;
        notify.selection = sr->selection;
        notify.target    = sr->target;
        notify.property  = XCB_ATOM_NONE;
        if (sr->selection == clipboard_atom_ && owns_clipboard_) {
            if (sr->target == targets_atom_) {
                // Advertise the two MIME-ish targets we support.
                xcb_atom_t targets[] = { targets_atom_, utf8_string_atom_ };
                xcb_change_property(conn_, XCB_PROP_MODE_REPLACE,
                    sr->requestor, sr->property, XCB_ATOM_ATOM, 32,
                    sizeof(targets)/sizeof(targets[0]), targets);
                notify.property = sr->property;
            } else if (sr->target == utf8_string_atom_ ||
                       sr->target == XCB_ATOM_STRING) {
                xcb_change_property(conn_, XCB_PROP_MODE_REPLACE,
                    sr->requestor, sr->property, sr->target, 8,
                    uint32_t(clipboard_local_text_.size()),
                    clipboard_local_text_.data());
                notify.property = sr->property;
            }
        }
        xcb_send_event(conn_, 0, sr->requestor,
            XCB_EVENT_MASK_NO_EVENT, (const char*)&notify);
        xcb_flush(conn_);
        break;
    }
    case XCB_SELECTION_CLEAR: {
        auto* sc = (xcb_selection_clear_event_t*)ev;
        if (sc->selection == clipboard_atom_) {
            owns_clipboard_ = false;
            clipboard_local_text_.clear();
        }
        break;
    }
    case 0: {
        // Error event. response_type==0 means xcb_generic_error_t.
        auto* er = (xcb_generic_error_t*)ev;
        std::fprintf(stderr, "X11 error: code=%u major=%u minor=%u\n",
                     er->error_code, er->major_code, er->minor_code);
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Clipboard (X11): CLIPBOARD selection, UTF8_STRING target.
// ---------------------------------------------------------------------------
void Window::clipboard_set_text(std::string_view utf8) {
    if (!conn_ || !win_ || !clipboard_atom_) return;
    clipboard_local_text_.assign(utf8);
    owns_clipboard_ = true;
    xcb_set_selection_owner(conn_, win_, clipboard_atom_, XCB_CURRENT_TIME);
    xcb_flush(conn_);
}

std::string Window::clipboard_get_text() {
    if (owns_clipboard_) return clipboard_local_text_;
    if (!conn_ || !win_ || !clipboard_atom_ || !utf8_string_atom_) return {};

    // Delete any leftover value on the scratch property before asking the
    // owner to write into it.
    xcb_delete_property(conn_, win_, stilus_paste_atom_);
    xcb_convert_selection(conn_, win_, clipboard_atom_, utf8_string_atom_,
                          stilus_paste_atom_, XCB_CURRENT_TIME);
    xcb_flush(conn_);

    // Wait briefly for SelectionNotify. Any *other* events we see while
    // waiting are dispatched normally so we don't drop pointer input during
    // the paste round-trip.
    const int budget_ms = 500;
    int spent = 0;
    xcb_selection_notify_event_t* notify = nullptr;
    while (spent < budget_ms) {
        xcb_generic_event_t* ev = xcb_poll_for_event(conn_);
        if (!ev) {
            int fd = xcb_get_file_descriptor(conn_);
            struct pollfd pfd{ fd, POLLIN, 0 };
            int t = std::min(50, budget_ms - spent);
            ::poll(&pfd, 1, t);
            spent += t;
            continue;
        }
        uint8_t type = ev->response_type & 0x7f;
        if (type == XCB_SELECTION_NOTIFY) {
            notify = (xcb_selection_notify_event_t*)ev;
            break;
        }
        dispatch_event_(ev);
        free(ev);
    }
    if (!notify) return {};
    xcb_atom_t prop = notify->property;
    free(notify);
    if (prop == XCB_ATOM_NONE) return {};

    // Fetch the delivered property. 0x1000000 units of 4 bytes covers any
    // single-shot transfer; owners that consider the data "large" answer
    // with type INCR instead and stream it in chunks.
    auto fetch_prop = [&]() -> xcb_get_property_reply_t* {
        return xcb_get_property_reply(conn_,
            xcb_get_property(conn_, 1 /* delete */, win_, prop,
                             XCB_GET_PROPERTY_TYPE_ANY, 0, 0x1000000),
            nullptr);
    };
    // Wait (bounded) for the next PropertyNotify new-value on our scratch
    // property, dispatching unrelated events normally in the meantime.
    auto wait_new_value = [&]() -> bool {
        const int budget = 2000;   // ms; matches large-transfer reality
        int waited = 0;
        while (waited < budget) {
            xcb_generic_event_t* ev = xcb_poll_for_event(conn_);
            if (!ev) {
                int fd = xcb_get_file_descriptor(conn_);
                struct pollfd pfd{ fd, POLLIN, 0 };
                int t = std::min(50, budget - waited);
                ::poll(&pfd, 1, t);
                waited += t;
                continue;
            }
            uint8_t type = ev->response_type & 0x7f;
            if (type == XCB_PROPERTY_NOTIFY) {
                auto* pn = (xcb_property_notify_event_t*)ev;
                bool hit = pn->window == win_ && pn->atom == prop &&
                           pn->state == XCB_PROPERTY_NEW_VALUE;
                free(ev);
                if (hit) return true;
                continue;
            }
            dispatch_event_(ev);
            free(ev);
        }
        return false;
    };

    xcb_get_property_reply_t* r = fetch_prop();
    if (!r) return {};

    std::string out;
    if (r->type == incr_atom_) {
        // INCR protocol: our delete of the INCR property (delete=1 above)
        // told the owner to start streaming. Each chunk arrives as a new
        // property value; a zero-length chunk terminates the transfer.
        free(r);
        for (;;) {
            if (!wait_new_value()) break;   // owner died / stalled
            xcb_get_property_reply_t* chunk = fetch_prop();
            if (!chunk) break;
            int len = xcb_get_property_value_length(chunk);
            if (len <= 0) { free(chunk); break; }   // done
            if (chunk->type == utf8_string_atom_ ||
                chunk->type == XCB_ATOM_STRING) {
                out.append((const char*)xcb_get_property_value(chunk),
                           size_t(len));
            }
            free(chunk);
        }
        return out;
    }

    if (r->type == utf8_string_atom_ || r->type == XCB_ATOM_STRING) {
        const char* data = (const char*)xcb_get_property_value(r);
        int len = xcb_get_property_value_length(r);
        out.assign(data, size_t(len));
    }
    free(r);
    return out;
}

} // namespace stilus::xcbw
