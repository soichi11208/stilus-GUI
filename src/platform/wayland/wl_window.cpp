// src/platform/wayland/wl_window.cpp
#include "wl_window.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "stilus/canvas.hpp"
#include "stilus/event.hpp"
#include "render/soft_canvas.hpp"
#include "wl_keymap.hpp"

namespace stilus::detail {
uint32_t utf8_decode_next(const char*& p, const char* end);
}

// Linux evdev button codes (from linux/input-event-codes.h)
#define BTN_LEFT   0x110
#define BTN_RIGHT  0x111
#define BTN_MIDDLE 0x112
#define BTN_SIDE   0x113
#define BTN_EXTRA  0x114

namespace stilus::wlw {

namespace {

uint64_t now_ms() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000 + uint64_t(ts.tv_nsec) / 1000000;
}


// --- anonymous memfd for wl_shm pool ---------------------------------------
int make_anonymous_file(size_t size) {
    int fd = ::memfd_create("gui-wl-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) { std::perror("memfd_create"); return -1; }
    if (::ftruncate(fd, off_t(size)) < 0) {
        std::perror("ftruncate");
        ::close(fd);
        return -1;
    }
    return fd;
}

} // namespace

// ---------------------------------------------------------------------------

Window::Window(std::string_view title, int w, int h)
    : title_(title), width_(w), height_(h) {
    damage_.set_surface(w, h);
    if (!init()) {
        std::fprintf(stderr, "wlw::Window init failed\n");
        return;
    }
    open_ = true;
}

Window::~Window() {
    for (auto& b : buffers_) destroy_buffer_(*b);
    buffers_.clear();
    // Server side cleanup best-effort. The display dtor closes the socket.
    if (toplevel_decoration_) {
        wl::Message m(toplevel_decoration_,
                      wl::zxdg_toplevel_decoration_v1_req::destroy);
        d_.send(m);
    }
    if (xdg_toplevel_) {
        wl::Message m(xdg_toplevel_, wl::xdg_toplevel_req::destroy);
        d_.send(m);
    }
    if (xdg_surface_) {
        wl::Message m(xdg_surface_, wl::xdg_surface_req::destroy);
        d_.send(m);
    }
    if (surface_) {
        wl::Message m(surface_, wl::wl_surface_req::destroy);
        d_.send(m);
    }
}

void Window::close() {
    // Just transition state. We deliberately do NOT re-fire the Close event
    // here — close() is the *response* to a Close, not a new one. Firing it
    // would recurse forever when the user's handler calls window.close().
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
    if (!d_.connect()) return false;

    // -- wl_display handler: errors + delete_id (spec requires we accept it) --
    d_.set_handler(wl::DISPLAY_ID,
        [this](wl::ObjectId, uint16_t op, const uint8_t* pl, size_t n,
               const int*, size_t) {
            wl::Reader r{pl, n};
            if (op == wl::wl_display_evt::error) {
                uint32_t obj   = r.u32();
                uint32_t code  = r.u32();
                auto     msg   = r.string();
                std::fprintf(stderr,
                    "wl_display.error: obj=%u code=%u: %.*s\n",
                    obj, code, int(msg.size()), msg.data());
            } else if (op == wl::wl_display_evt::delete_id) {
                uint32_t id = r.u32();
                (void)id; // we don't reuse ids yet
            }
        });

    // -- Request registry -----------------------------------------------------
    wl::ObjectId registry = d_.new_id();

    d_.set_handler(registry,
        [this, registry](wl::ObjectId, uint16_t op, const uint8_t* pl, size_t n,
                         const int*, size_t) {
            wl::Reader r{pl, n};
            if (op == wl::wl_registry_evt::global) {
                uint32_t          name      = r.u32();
                std::string_view  interface = r.string();
                uint32_t          version   = r.u32();

                auto bind = [&](uint32_t want_ver, wl::ObjectId& out) {
                    out = d_.new_id();
                    wl::Message m(registry, wl::wl_registry_req::bind);
                    m.u32(name);
                    m.string(interface);
                    m.u32(want_ver);
                    m.new_id(out);
                    d_.send(m);
                };

                if (interface == "wl_compositor" && !compositor_) {
                    compositor_ver_ = std::min<uint32_t>(version, 4u);
                    bind(compositor_ver_, compositor_);
                } else if (interface == "wl_shm" && !shm_) {
                    bind(std::min<uint32_t>(version, 1u), shm_);
                    d_.set_handler(shm_,
                        [](wl::ObjectId, uint16_t, const uint8_t*, size_t,
                           const int*, size_t) { /* format events: ignore */ });
                } else if (interface == "xdg_wm_base" && !xdg_wm_base_) {
                    bind(std::min<uint32_t>(version, 3u), xdg_wm_base_);
                    // xdg_wm_base.ping -> pong
                    d_.set_handler(xdg_wm_base_,
                        [this](wl::ObjectId id, uint16_t op2, const uint8_t* pl2,
                               size_t n2, const int*, size_t) {
                            if (op2 == wl::xdg_wm_base_evt::ping) {
                                wl::Reader rr{pl2, n2};
                                uint32_t serial = rr.u32();
                                wl::Message m(id, wl::xdg_wm_base_req::pong);
                                m.u32(serial);
                                d_.send(m);
                            }
                        });
                } else if (interface == "wl_seat" && !seat_) {
                    seat_ver_ = std::min<uint32_t>(version, 5u);
                    bind(seat_ver_, seat_);
                    d_.set_handler(seat_,
                        [this](wl::ObjectId, uint16_t op2,
                               const uint8_t* pl2, size_t n2,
                               const int*, size_t) {
                            if (op2 == wl::wl_seat_evt::capabilities) {
                                wl::Reader r{pl2, n2};
                                setup_seat_(r.u32());
                            }
                            // name event: ignored
                        });
                } else if (interface == "zxdg_decoration_manager_v1"
                           && !decoration_manager_) {
                    bind(std::min<uint32_t>(version, 1u), decoration_manager_);
                } else if (interface == "zwp_text_input_manager_v3"
                           && !text_input_manager_) {
                    bind(std::min<uint32_t>(version, 1u), text_input_manager_);
                } else if (interface == "wl_output") {
                    // Bind every output so we can track its scale and which
                    // ones our surface enters.
                    wl::ObjectId out;
                    bind(std::min<uint32_t>(version, 2u), out);
                    OutputInfo oi; oi.id = out; oi.scale = 1;
                    outputs_[name] = oi;
                    output_by_id_[out] = name;
                    d_.set_handler(out,
                        [this](wl::ObjectId oid, uint16_t op, const uint8_t* pl2,
                               size_t n2, const int*, size_t) {
                            if (op == wl::wl_output_evt::scale) {
                                wl::Reader r{pl2, n2};
                                int32_t s = r.i32();
                                if (s < 1) s = 1;
                                auto it = output_by_id_.find(oid);
                                if (it != output_by_id_.end()) {
                                    outputs_[it->second].scale = s;
                                    // If we are currently on this output, re-evaluate.
                                    if (entered_outputs_.count(oid))
                                        update_scale_();
                                }
                            }
                            // geometry/mode/done/name/description: ignored
                        });
                }
            } else if (op == wl::wl_registry_evt::global_remove) {
                // Not handled; we don't hot-unplug yet.
            }
        });

    // Send wl_display.get_registry(registry)
    {
        wl::Message m(wl::DISPLAY_ID, wl::wl_display_req::get_registry);
        m.new_id(registry);
        d_.send(m);
    }

    // Sync roundtrip to get all globals.
    if (!d_.roundtrip()) return false;

    if (!compositor_ || !shm_ || !xdg_wm_base_) {
        std::fprintf(stderr,
            "missing required globals (compositor=%u shm=%u xdg_wm_base=%u)\n",
            compositor_, shm_, xdg_wm_base_);
        return false;
    }

    // -- Create wl_surface ----------------------------------------------------
    surface_ = d_.new_id();
    {
        wl::Message m(compositor_, wl::wl_compositor_req::create_surface);
        m.new_id(surface_);
        d_.send(m);
    }
    d_.set_handler(surface_,
        [this](wl::ObjectId, uint16_t op, const uint8_t* pl, size_t n,
               const int*, size_t) {
            wl::Reader r{pl, n};
            if (op == wl::wl_surface_evt::enter) {
                uint32_t out = r.u32();
                entered_outputs_.insert(out);
                update_scale_();
            } else if (op == wl::wl_surface_evt::leave) {
                uint32_t out = r.u32();
                entered_outputs_.erase(out);
                update_scale_();
            }
        });

    // -- xdg_surface + xdg_toplevel ------------------------------------------
    xdg_surface_ = d_.new_id();
    {
        wl::Message m(xdg_wm_base_, wl::xdg_wm_base_req::get_xdg_surface);
        m.new_id(xdg_surface_);
        m.object(surface_);
        d_.send(m);
    }
    d_.set_handler(xdg_surface_,
        [this](wl::ObjectId id, uint16_t op, const uint8_t* pl, size_t n,
               const int*, size_t) {
            if (op == wl::xdg_surface_evt::configure) {
                wl::Reader r{pl, n};
                uint32_t serial = r.u32();
                wl::Message m(id, wl::xdg_surface_req::ack_configure);
                m.u32(serial);
                d_.send(m);
                configured_ = true;
                if (pending_resize_) {
                    int new_w = pending_w_ > 0 ? pending_w_ : width_;
                    int new_h = pending_h_ > 0 ? pending_h_ : height_;
                    // pending_* is the *window* size (compositor's view);
                    // subtract our own titlebar to get the user content area.
                    int tb = csd_titlebar_h_eff_();
                    if (csd_enabled_) new_h -= tb;
                    if (new_h < 1) new_h = 1;
                    width_  = new_w;
                    height_ = new_h;
                    pending_resize_ = false;
                    damage_.set_surface(width_, height_);
                    last_attached_ = nullptr;
                    if (event_cb_) {
                        Event e; e.type = Event::Type::Resize;
                        e.width = width_; e.height = height_;
                        event_cb_(e);
                    }
                }
                damage_.mark_full();
                needs_redraw_ = true;
            }
        });

    xdg_toplevel_ = d_.new_id();
    {
        wl::Message m(xdg_surface_, wl::xdg_surface_req::get_toplevel);
        m.new_id(xdg_toplevel_);
        d_.send(m);
    }
    d_.set_handler(xdg_toplevel_,
        [this](wl::ObjectId, uint16_t op, const uint8_t* pl, size_t n,
               const int*, size_t) {
            if (op == wl::xdg_toplevel_evt::configure) {
                wl::Reader r{pl, n};
                int32_t w = r.i32();
                int32_t h = r.i32();
                // Parse the states array: a length-prefixed array of u32.
                bool now_max = false;
                auto [sp_b, sp_n] = r.array();
                const uint32_t* sp = reinterpret_cast<const uint32_t*>(sp_b);
                for (size_t i = 0; i < sp_n / sizeof(uint32_t); ++i) {
                    if (sp[i] == 1 /* xdg_toplevel.state.maximized */) now_max = true;
                }
                if (now_max != csd_maximized_) {
                    csd_maximized_ = now_max;
                    damage_.mark_full();
                    needs_redraw_ = true;
                }
                if (w > 0 && h > 0) {
                    int user_h = csd_enabled_ ? (h - csd_titlebar_h_) : h;
                    if (user_h < 1) user_h = 1;
                    if (w != width_ || user_h != height_) {
                        pending_w_ = w;
                        pending_h_ = h;
                        pending_resize_ = true;
                    }
                }
            } else if (op == wl::xdg_toplevel_evt::close) {
                open_ = false;
                send_close_();
            }
        });

    // STILUS_FORCE_CSD=1 short-circuits the negotiation: useful for testing the
    // CSD path on compositors that would otherwise grant SSD.
    const char* force_csd = std::getenv("STILUS_FORCE_CSD");
    const bool  force_csd_on = force_csd && force_csd[0] && force_csd[0] != '0';

    // Request server-side decorations. If the compositor doesn't advertise
    // xdg-decoration-v1, we fall through to client-side decorations below.
    if (decoration_manager_ && !force_csd_on) {
        toplevel_decoration_ = d_.new_id();
        {
            wl::Message m(decoration_manager_,
                          wl::zxdg_decoration_manager_v1_req::get_toplevel_decoration);
            m.new_id(toplevel_decoration_);
            m.object(xdg_toplevel_);
            d_.send(m);
        }
        {
            wl::Message m(toplevel_decoration_,
                          wl::zxdg_toplevel_decoration_v1_req::set_mode);
            m.u32(wl::zxdg_toplevel_decoration_v1_mode::server_side);
            d_.send(m);
        }
        // The compositor replies with the mode it actually applied. If it
        // forces client_side we switch into our own titlebar mode.
        d_.set_handler(toplevel_decoration_,
            [this](wl::ObjectId, uint16_t op, const uint8_t* pl, size_t n,
                   const int*, size_t) {
                if (op == wl::zxdg_toplevel_decoration_v1_evt::configure) {
                    wl::Reader r{pl, n};
                    uint32_t mode = r.u32();
                    bool want_csd = (mode == wl::zxdg_toplevel_decoration_v1_mode::client_side);
                    if (want_csd != csd_enabled_) {
                        csd_enabled_ = want_csd;
                        last_attached_ = nullptr;
                        damage_.mark_full();
                        needs_redraw_ = true;
                    }
                }
            });
    } else {
        // No decoration extension at all — assume the compositor expects us
        // to draw our own (GNOME / Mutter behaves this way).
        csd_enabled_ = true;
    }

    // Set title
    {
        wl::Message m(xdg_toplevel_, wl::xdg_toplevel_req::set_title);
        m.string(title_);
        d_.send(m);
    }
    {
        wl::Message m(xdg_toplevel_, wl::xdg_toplevel_req::set_app_id);
        m.string("stilus.app");
        d_.send(m);
    }

    // Initial commit (no buffer): required to kick off configure cycle.
    {
        wl::Message m(surface_, wl::wl_surface_req::commit);
        d_.send(m);
    }

    // Wait for the first configure.
    while (!configured_) {
        if (!d_.read_dispatch(-1)) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
Window::ShmBuffer* Window::acquire_buffer_(int w, int h) {
    for (auto& b : buffers_) {
        if (!b->busy && b->width == w && b->height == h) return b.get();
    }
    // Create a new one.
    int stride = w * 4;
    size_t size = size_t(stride) * size_t(h);

    int fd = make_anonymous_file(size);
    if (fd < 0) return nullptr;
    void* map = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) { std::perror("mmap"); ::close(fd); return nullptr; }

    // Create wl_shm_pool
    wl::ObjectId pool = d_.new_id();
    {
        wl::Message m(shm_, wl::wl_shm_req::create_pool);
        m.new_id(pool);
        m.fd(fd);
        m.i32(int32_t(size));
        if (!d_.send(m)) { ::munmap(map, size); ::close(fd); return nullptr; }
    }

    // Create buffer
    wl::ObjectId buf_id = d_.new_id();
    {
        wl::Message m(pool, wl::wl_shm_pool_req::create_buffer);
        m.new_id(buf_id);
        m.i32(0);                // offset
        m.i32(w);
        m.i32(h);
        m.i32(stride);
        m.u32(wl::shm_format::XRGB8888);
        d_.send(m);
    }
    // Destroy pool (buffer keeps the mapping alive server-side)
    {
        wl::Message m(pool, wl::wl_shm_pool_req::destroy);
        d_.send(m);
    }

    auto up = std::make_unique<ShmBuffer>();
    up->id       = buf_id;
    up->pixels   = reinterpret_cast<uint32_t*>(map);
    up->width    = w;
    up->height   = h;
    up->stride   = stride;
    up->size     = size;
    up->busy     = false;
    up->map      = map;
    up->map_fd   = fd; // kept for symmetry; we close our copy after send
    up->map_size = size;

    // We can close our fd copy — the server has its own via SCM_RIGHTS.
    ::close(fd);
    up->map_fd = -1;

    ShmBuffer* ref = up.get();
    buffers_.push_back(std::move(up));

    // Track release
    d_.set_handler(buf_id,
        [this, ref](wl::ObjectId, uint16_t op, const uint8_t*, size_t,
                    const int*, size_t) {
            if (op == wl::wl_buffer_evt::release) {
                ref->busy = false;
            }
        });
    return ref;
}

void Window::destroy_buffer_(ShmBuffer& b) {
    if (b.map && b.map != MAP_FAILED) ::munmap(b.map, b.map_size);
    b.map = nullptr;
    // buffer server-side will be GC'd by compositor when surface dies.
}

// ---------------------------------------------------------------------------
// Client-side decoration: titlebar layout + drawing
// ---------------------------------------------------------------------------
namespace {
constexpr float CSD_BTN_W   = 30.f;
constexpr float CSD_BTN_PAD = 2.f;
}

int Window::csd_button_at_(float x, float y) const {
    if (!csd_enabled_) return -1;
    if (y < 0 || y >= float(csd_titlebar_h_)) return -1;
    float right = float(width_);
    // Buttons laid out right-to-left: close, max, min.
    for (int i = 0; i < 3; ++i) {
        float bx1 = right - (CSD_BTN_W + CSD_BTN_PAD) * float(i) - CSD_BTN_PAD;
        float bx0 = bx1 - CSD_BTN_W;
        if (x >= bx0 && x < bx1) return 2 - i;   // i=0 close, i=1 max, i=2 min
    }
    return -1;
}

void Window::draw_csd_titlebar_(Canvas& c) {
    const float W  = float(width_);
    const float TH = float(csd_titlebar_h_);

    // Background.
    c.fill_rect({0, 0, W, TH}, Color::rgb(0x303034));
    // 1px hairline under the bar to delineate it from the content area.
    c.fill_rect({0, TH - 1.f, W, 1.f}, Color::rgb(0x18181a));

    // Title text — centered. Skip if no font is bound; we don't carry a
    // backend font reference, so just draw a small chrome dot for now.
    // (The compositor's window list shows the real title via xdg_toplevel.)
    (void)0;

    // Buttons: right-aligned. order = min, max, close (left -> right).
    for (int i = 0; i < 3; ++i) {
        // i=0 min, i=1 max, i=2 close
        int slot = 2 - i;   // distance from the right edge in button slots
        float bx1 = W   - (CSD_BTN_W + CSD_BTN_PAD) * float(slot) - CSD_BTN_PAD;
        float bx0 = bx1 - CSD_BTN_W;
        float by0 = 0.f;
        float by1 = TH;
        Rect br{bx0, by0, bx1 - bx0, by1 - by0};

        // Hover/press background tint.
        bool hover   = (csd_hover_button_   == i);
        bool pressed = (csd_pressed_button_ == i);
        if (pressed) {
            Color bg = (i == 2) ? Color::rgb(0xb04030) : Color::rgb(0x404048);
            c.fill_rect(br, bg);
        } else if (hover) {
            Color bg = (i == 2) ? Color::rgb(0xc0463a) : Color::rgb(0x3a3a40);
            c.fill_rect(br, bg);
        }

        // Icon glyphs, centered, ~10px square.
        float cx = bx0 + (bx1 - bx0) * 0.5f;
        float cy = by0 + (by1 - by0) * 0.5f;
        Color fg = Color::rgb(0xdddddd);
        if (i == 0) {
            // Minimize: bottom horizontal bar.
            c.fill_rect({cx - 5.f, cy + 4.f, 10.f, 1.5f}, fg);
        } else if (i == 1) {
            // Maximize: outline square (or restore variant when maximized).
            if (csd_maximized_) {
                // Two overlapping squares to suggest "restore".
                c.stroke_rect({cx - 5.f, cy - 3.f, 8.f, 8.f}, 1.0f, fg);
                c.stroke_rect({cx - 3.f, cy - 5.f, 8.f, 8.f}, 1.0f, fg);
            } else {
                c.stroke_rect({cx - 5.f, cy - 5.f, 10.f, 10.f}, 1.0f, fg);
            }
        } else {
            // Close: X. Two thin filled rects rotated 45° via affine.
            c.push_transform(Affine::translate(cx, cy));
            c.push_transform(Affine::rotate(0.7853981633974f /* pi/4 */));
            c.fill_rect({-6.f, -0.7f, 12.f, 1.4f}, fg);
            c.fill_rect({-0.7f, -6.f, 1.4f, 12.f}, fg);
            c.pop_transform();
            c.pop_transform();
        }
    }
}

bool Window::csd_update_hover_(float x, float y) {
    int b = csd_button_at_(x, y);
    if (b != csd_hover_button_) {
        csd_hover_button_ = b;
        damage_.mark_full();
        needs_redraw_ = true;
        return true;
    }
    return false;
}

bool Window::csd_handle_pointer_button_(uint32_t serial, bool pressed,
                                        float x, float y) {
    if (!csd_enabled_) return false;
    if (pressed) {
        if (y >= float(csd_titlebar_h_)) return false;
        int btn = csd_button_at_(x, y);
        csd_pressed_button_ = btn;
        damage_.mark_full();
        needs_redraw_ = true;
        if (btn < 0) {
            // Drag the window: ask the compositor to start an interactive move.
            if (seat_) {
                wl::Message m(xdg_toplevel_, wl::xdg_toplevel_req::move);
                m.object(seat_);
                m.u32(serial);
                d_.send(m);
            }
        }
        return true;
    } else {
        // Release: if released over the same button, trigger its action.
        int btn = csd_button_at_(x, y);
        int prev = csd_pressed_button_;
        csd_pressed_button_ = -1;
        damage_.mark_full();
        needs_redraw_ = true;
        if (prev >= 0 && btn == prev) {
            if (prev == 0) {
                wl::Message m(xdg_toplevel_, wl::xdg_toplevel_req::set_minimized);
                d_.send(m);
            } else if (prev == 1) {
                wl::Message m(xdg_toplevel_,
                    csd_maximized_ ? wl::xdg_toplevel_req::unset_maximized
                                   : wl::xdg_toplevel_req::set_maximized);
                d_.send(m);
                csd_maximized_ = !csd_maximized_;
            } else if (prev == 2) {
                open_ = false;
                send_close_();
            }
        }
        // Suppress forwarding if the press began inside the titlebar.
        return prev >= 0 || y < float(csd_titlebar_h_);
    }
}

// ---------------------------------------------------------------------------
void Window::update_scale_() {
    int s = 1;
    for (wl::ObjectId oid : entered_outputs_) {
        auto it = output_by_id_.find(oid);
        if (it == output_by_id_.end()) continue;
        int os = outputs_[it->second].scale;
        if (os > s) s = os;
    }
    if (s != scale_) {
        scale_ = s;
        // Buffer dimensions changed — drop any retained buffer.
        last_attached_ = nullptr;
        damage_.mark_full();
        needs_redraw_ = true;
    }
}

// ---------------------------------------------------------------------------
void Window::paint_frame_() {
    if (!configured_) return;
    if (damage_.empty()) {
        // Nothing requested redraw but we got here — treat as full.
        damage_.mark_full();
    }

    const int tb = csd_titlebar_h_eff_();
    const int surf_h = height_ + tb;
    int phys_w = width_  * scale_;
    int phys_h = surf_h  * scale_;
    ShmBuffer* b = acquire_buffer_(phys_w, phys_h);
    if (!b) return;

    if (applied_scale_ != scale_) {
        wl::Message m(surface_, wl::wl_surface_req::set_buffer_scale);
        m.i32(scale_);
        d_.send(m);
        applied_scale_ = scale_;
    }

    // CSD takes the simple route: always full repaint (avoids tracking damage
    // for both the user area and the titlebar).
    const bool full = csd_enabled_ ||
                      damage_.is_full() || !last_attached_ ||
                      last_attached_ == b ||
                      last_attached_->width  != b->width ||
                      last_attached_->height != b->height;

    if (!full) {
        std::memcpy(b->pixels, last_attached_->pixels, b->size);
    }

    render::SoftCanvas canvas;
    canvas.bind(b->pixels, b->width, b->height, b->stride / 4);

    // Step 1: draw the titlebar (in surface logical coords). Done at the
    // canvas's initial identity transform; only the scale is applied below
    // for the user content area.
    if (csd_enabled_) {
        if (scale_ != 1) canvas.push_transform(Affine::scale(float(scale_)));
        draw_csd_titlebar_(canvas);
        if (scale_ != 1) canvas.pop_transform();
    }

    // Step 2: position user-area drawing below the titlebar and scale to
    // physical pixels.
    if (scale_ != 1) canvas.push_transform(Affine::scale(float(scale_)));
    if (tb)          canvas.push_transform(Affine::translate(0, float(tb)));

    if (full) {
        // Clear only the user area (titlebar already painted).
        canvas.fill_rect({0, 0, float(width_), float(height_)},
                         Color::rgb(0x202024));
        if (frame_cb_) frame_cb_(canvas);
    } else {
        Rect bb = damage_.bounds();
        canvas.push_clip(bb);
        canvas.fill_rect(bb, Color::rgb(0x202024));
        if (frame_cb_) frame_cb_(canvas);
        canvas.pop_clip();
    }

    if (tb)          canvas.pop_transform();
    if (scale_ != 1) canvas.pop_transform();

    // Attach.
    {
        wl::Message m(surface_, wl::wl_surface_req::attach);
        m.object(b->id);
        m.i32(0);
        m.i32(0);
        d_.send(m);
    }
    // Damage. For v4+ we use damage_buffer (buffer-local coords).
    const uint16_t dmg_op = compositor_ver_ >= 4
                                ? wl::wl_surface_req::damage_buffer
                                : wl::wl_surface_req::damage;
    // damage_buffer takes buffer-local pixel coords; damage takes surface
    // (logical) coords. Scale accordingly.
    const int dmg_scale = (dmg_op == wl::wl_surface_req::damage_buffer)
                              ? scale_ : 1;
    if (full) {
        wl::Message m(surface_, dmg_op);
        m.i32(0); m.i32(0);
        m.i32(width_  * dmg_scale);
        m.i32(surf_h  * dmg_scale);
        d_.send(m);
    } else {
        for (const Rect& r : damage_.rects()) {
            auto ir = damage_.to_pixels(r);
            if (ir.w <= 0 || ir.h <= 0) continue;
            wl::Message m(surface_, dmg_op);
            // damage rects are in user-area coords; shift by tb in surface.
            int dx = ir.x * dmg_scale;
            int dy = (ir.y + tb) * dmg_scale;
            int dw = ir.w * dmg_scale;
            int dh = ir.h * dmg_scale;
            m.i32(dx); m.i32(dy); m.i32(dw); m.i32(dh);
            d_.send(m);
        }
    }
    b->busy = true;
    last_attached_ = b;
    {
        wl::Message m(surface_, wl::wl_surface_req::commit);
        d_.send(m);
    }
    damage_.clear();
    needs_redraw_ = false;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
void Window::setup_seat_(uint32_t caps) {
    // Pointer
    if ((caps & wl::wl_seat_caps::pointer) && !pointer_) {
        pointer_ = d_.new_id();
        wl::Message m(seat_, wl::wl_seat_req::get_pointer);
        m.new_id(pointer_);
        d_.send(m);
        d_.set_handler(pointer_,
            [this](wl::ObjectId, uint16_t op, const uint8_t* pl, size_t n,
                   const int*, size_t) {
                on_pointer_event_(op, pl, n);
            });
    }
    // Keyboard
    if ((caps & wl::wl_seat_caps::keyboard) && !keyboard_) {
        keyboard_ = d_.new_id();
        wl::Message m(seat_, wl::wl_seat_req::get_keyboard);
        m.new_id(keyboard_);
        d_.send(m);
        d_.set_handler(keyboard_,
            [this](wl::ObjectId, uint16_t op, const uint8_t* pl, size_t n,
                   const int* fds, size_t nfds) {
                // The keymap event carries the active xkb keymap text via
                // an SCM_RIGHTS fd. We mmap it, parse it ourselves (no
                // libxkbcommon), and close the fd here — always — so the
                // shared memory doesn't leak on parse failure.
                if (op == wl::wl_keyboard_evt::keymap && nfds >= 1) {
                    wl::Reader r{pl, n};
                    uint32_t format = r.u32();
                    uint32_t size   = r.u32();
                    int fd = fds[0];
                    if (format == 1 /* xkb_v1 */ && size > 0) {
                        void* m = ::mmap(nullptr, size, PROT_READ,
                                         MAP_PRIVATE, fd, 0);
                        if (m != MAP_FAILED) {
                            XkbKeymap km;
                            if (km.parse((const char*)m, size)) xkb_ = std::move(km);
                            ::munmap(m, size);
                        }
                    }
                    ::close(fd);
                    // Still call on_keyboard_event_ for uniformity (it's a no-op
                    // for keymap today, but may track modifier layout later).
                    on_keyboard_event_(op, pl, n);
                    return;
                }
                on_keyboard_event_(op, pl, n);
                for (size_t i = 0; i < nfds; ++i) ::close(fds[i]);
            });
    }
    // Text Input (IME) - initialized when keyboard cap appears
    if ((caps & wl::wl_seat_caps::keyboard) && !text_input_ && text_input_manager_) {
        setup_text_input_();
    }
}

void Window::on_pointer_event_(uint16_t op, const uint8_t* pl, size_t n) {
    wl::Reader r{pl, n};
    const float tb = float(csd_titlebar_h_eff_());
    switch (op) {
    case wl::wl_pointer_evt::enter: {
        last_pointer_enter_serial_ = r.u32();
        r.u32();                          // surface
        float x = wl::fixed_to_float(r.i32());
        float y = wl::fixed_to_float(r.i32());
        mouse_x_ = x; mouse_y_ = y;
        mouse_in_ = true;
        if (csd_enabled_) csd_update_hover_(x, y);
        if (event_cb_ && y >= tb) {
            Event e; e.type = Event::Type::MouseMove;
            e.x = x; e.y = y - tb;
            event_cb_(e);
        }
        break;
    }
    case wl::wl_pointer_evt::leave: {
        mouse_in_ = false;
        if (csd_enabled_ && csd_hover_button_ != -1) {
            csd_hover_button_ = -1;
            damage_.mark_full();
            needs_redraw_ = true;
        }
        break;
    }
    case wl::wl_pointer_evt::motion: {
        r.u32();                          // time
        float x = wl::fixed_to_float(r.i32());
        float y = wl::fixed_to_float(r.i32());
        mouse_x_ = x; mouse_y_ = y;
        if (csd_enabled_) csd_update_hover_(x, y);
        if (event_cb_ && y >= tb) {
            Event e; e.type = Event::Type::MouseMove;
            e.x = x; e.y = y - tb;
            event_cb_(e);
        }
        break;
    }
    case wl::wl_pointer_evt::button: {
        uint32_t serial = r.u32();
        last_pointer_serial_ = serial;
        r.u32();                          // time
        uint32_t button = r.u32();
        uint32_t state  = r.u32();
        const bool pressed = (state == wl::wl_pointer_btn_state::pressed);
        MouseButton mb = MouseButton::Left;
        switch (button) {
            case BTN_LEFT:   mb = MouseButton::Left;   break;
            case BTN_RIGHT:  mb = MouseButton::Right;  break;
            case BTN_MIDDLE: mb = MouseButton::Middle; break;
            case BTN_SIDE:   mb = MouseButton::X1;     break;
            case BTN_EXTRA:  mb = MouseButton::X2;     break;
            default: return;
        }
        // Titlebar consumes the left button only (drag + buttons).
        if (csd_enabled_ && mb == MouseButton::Left) {
            if (csd_handle_pointer_button_(serial, pressed, mouse_x_, mouse_y_))
                return;
        }
        if (event_cb_) {
            Event e;
            e.type = pressed ? Event::Type::MouseDown : Event::Type::MouseUp;
            e.x = mouse_x_; e.y = mouse_y_ - tb;
            e.button = mb;
            event_cb_(e);
        }
        break;
    }
    case wl::wl_pointer_evt::axis: {
        r.u32();                          // time
        uint32_t axis = r.u32();
        float    val  = wl::fixed_to_float(r.i32());
        if (event_cb_ && mouse_y_ >= tb) {
            Event e; e.type = Event::Type::MouseWheel;
            e.x = mouse_x_; e.y = mouse_y_ - tb;
            if (axis == wl::wl_pointer_axis::vertical_scroll)
                e.wheel_dy = -val;    // Wayland: positive = down; our convention: up-positive
            else
                e.wheel_dx = -val;
            event_cb_(e);
        }
        break;
    }
    default: break; // frame, axis_source, axis_stop, axis_discrete, ...: ignored
    }
}

void Window::on_keyboard_event_(uint16_t op, const uint8_t* pl, size_t n) {
    wl::Reader r{pl, n};
    switch (op) {
    case wl::wl_keyboard_evt::keymap: {
        // format, fd(from SCM_RIGHTS), size — we don't parse the xkb map yet.
        (void)r;
        break;
    }
    case wl::wl_keyboard_evt::enter: {
        if (event_cb_) { Event e; e.type = Event::Type::Focus; event_cb_(e); }
        break;
    }
    case wl::wl_keyboard_evt::leave: {
        held_evkey_ = 0;
        mods_ = KeyMods{};
        if (event_cb_) { Event e; e.type = Event::Type::Unfocus; event_cb_(e); }
        break;
    }
    case wl::wl_keyboard_evt::key: {
        r.u32();                              // serial
        r.u32();                              // time
        uint32_t evkey = r.u32();             // evdev keycode
        uint32_t state = r.u32();
        const bool pressed = (state == wl::wl_keyboard_key_state::pressed);
        Event e;
        e.type = pressed ? Event::Type::KeyDown : Event::Type::KeyUp;
        e.key  = evdev_to_key(evkey);
        e.mods = mods_;
        if (event_cb_) event_cb_(e);

        uint32_t cp = 0;
        char utf8_buf[5] = {0};
        if (pressed && !mods_.ctrl && !mods_.alt) {
            if (xkb_.valid()) {
                cp = xkb_.codepoint(evkey, mods_.shift, level3_, caps_lock_);
            } else {
                cp = evdev_to_codepoint(evkey, mods_.shift);
            }
            if (cp) {
                utf8_encode(cp, utf8_buf);
                Event t;
                t.type = Event::Type::TextInput;
                t.mods = mods_;
                t.codepoint = cp;
                t.text = utf8_buf;
                if (event_cb_) event_cb_(t);
            }
        }

        // Key repeat bookkeeping.
        if (pressed && repeat_rate_ > 0) {
            held_evkey_     = evkey;
            held_key_       = e.key;
            held_codepoint_ = cp;
            std::memcpy(held_text_, utf8_buf, sizeof(utf8_buf));
            held_next_ms_   = now_ms() + repeat_delay_ms_;
        } else if (!pressed && evkey == held_evkey_) {
            held_evkey_ = 0;
        }
        break;
    }
    case wl::wl_keyboard_evt::repeat_info: {
        int32_t rate  = (int32_t)r.u32();
        int32_t delay = (int32_t)r.u32();
        repeat_rate_     = (rate  > 0) ? uint32_t(rate)  : 0;
        repeat_delay_ms_ = (delay > 0) ? uint32_t(delay) : 400;
        if (repeat_rate_ == 0) held_evkey_ = 0;
        break;
    }
    case wl::wl_keyboard_evt::modifiers: {
        r.u32();                              // serial
        uint32_t depressed = r.u32();
        r.u32();                              // latched
        uint32_t locked = r.u32();
        r.u32();                              // group
        // xkb core modifier bit positions (fixed by convention in every
        // default keymap): Shift=0, Lock/Caps=1, Control=2, Mod1/Alt=3,
        // Mod2/NumLock=4, Mod3=5, Mod4/Super=6, Mod5/Level3/AltGr=7.
        uint32_t eff = depressed | locked;
        mods_.shift = eff & (1u << 0);
        mods_.ctrl  = eff & (1u << 2);
        mods_.alt   = eff & (1u << 3);
        mods_.super = eff & (1u << 6);
        caps_lock_  = locked & (1u << 1);
        level3_     = eff    & (1u << 7);
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------------------
// IME / Text Input (zwp_text_input_v3)
// ---------------------------------------------------------------------------
void Window::setup_text_input_() {
    text_input_ = d_.new_id();
    {
        wl::Message m(text_input_manager_,
                       wl::zwp_text_input_manager_v3_req::get_text_input);
        m.new_id(text_input_);
        m.object(seat_);
        d_.send(m);
    }

    // Set content type: hints=none(0), purpose=normal(0). The v3 protocol
    // packs both into the set_content_type request as (hint, purpose).
    {
        wl::Message m(text_input_, wl::zwp_text_input_v3_req::set_content_type);
        m.u32(0); // content_hint: none
        m.u32(0); // content_purpose: normal
        d_.send(m);
    }
    {
        wl::Message m(text_input_, wl::zwp_text_input_v3_req::commit);
        d_.send(m);
    }

    // Register handler for text input events
    d_.set_handler(text_input_,
        [this](wl::ObjectId, uint16_t op, const uint8_t* pl, size_t n,
               const int*, size_t) {
            on_text_input_event_(op, pl, n);
        });
}

void Window::text_input_enable_() {
    if (!text_input_ || !event_cb_) return;

    // Send cursor rectangle (window dimensions)
    {
        wl::Message m(text_input_, wl::zwp_text_input_v3_req::set_cursor_rectangle);
        m.i32(0);          // x
        m.i32(0);          // y
        m.i32(width_);     // width
        m.i32(height_);    // height
        d_.send(m);
    }
    // Reset surrounding text (no pre-filled context)
    {
        wl::Message m(text_input_, wl::zwp_text_input_v3_req::set_surrounding_text);
        m.string("");
        m.u32(0);  // cursor position
        m.u32(0);  // selection end
        d_.send(m);
    }
    // Enable the text input
    {
        wl::Message m(text_input_, wl::zwp_text_input_v3_req::enable);
        m.object(seat_);
        d_.send(m);
    }
    {
        wl::Message m(text_input_, wl::zwp_text_input_v3_req::commit);
        d_.send(m);
    }
}

void Window::text_input_disable_() {
    if (!text_input_) return;

    {
        wl::Message m(text_input_, wl::zwp_text_input_v3_req::disable);
        m.object(seat_);
        d_.send(m);
    }
    {
        wl::Message m(text_input_, wl::zwp_text_input_v3_req::commit);
        d_.send(m);
    }
}

void Window::on_text_input_event_(uint16_t op, const uint8_t* pl, size_t n) {
    if (!event_cb_) return;
    wl::Reader r{pl, n};
    switch (op) {
    case wl::zwp_text_input_v3_evt::enter: {
        // Text input entered our surface
        r.u32(); // serial
        break;
    }
    case wl::zwp_text_input_v3_evt::leave: {
        // Text input left our surface - clear preedit state
        r.u32(); // serial
        break;
    }
    case wl::zwp_text_input_v3_evt::preedit_string: {
        // IME composition (preedit) text. The text-input-v3 protocol carries
        // cursor_begin/cursor_end as byte offsets into the preedit string
        // (or both -1 to hide the cursor).
        r.u32(); // serial
        std::string_view text = r.string();
        int32_t cb = r.i32();
        int32_t ce = r.i32();
        Event e;
        e.type = Event::Type::Preedit;
        e.text = std::string(text);
        e.preedit_cursor_begin = cb;
        e.preedit_cursor_end   = ce;
        e.codepoint = 0;
        event_cb_(e);
        break;
    }
    case wl::zwp_text_input_v3_evt::commit_string: {
        // IME committed text
        r.u32(); // serial
        std::string_view text = r.string();

        if (!text.empty()) {
            Event e;
            e.type = Event::Type::TextInput;
            e.text = std::string(text);
            // For single-codepoint text, surface the codepoint too.
            e.codepoint = 0;
            if (text.size() <= 4) {
                const char* p = text.data();
                e.codepoint = detail::utf8_decode_next(p, text.data() + text.size());
            }
            event_cb_(e);
        }
        break;
    }
    case wl::zwp_text_input_v3_evt::delete_surrounding_text: {
        // IME requests deletion of surrounding text. Approximated by emitting
        // KeyDown(Backspace) events; widgets that don't subscribe simply
        // ignore them. (Proper surrounding-text editing would need a richer
        // widget protocol — deferred until we have one.)
        r.u32(); // serial
        int32_t before_length = r.i32();
        r.i32();                 // after_length unused
        for (int i = 0; i < before_length; ++i) {
            Event e;
            e.type = Event::Type::KeyDown;
            e.key  = Key::Backspace;
            event_cb_(e);
        }
        break;
    }
    case wl::zwp_text_input_v3_evt::done: {
        // End of IME event sequence
        r.u32(); // serial
        break;
    }
    default: break;
    }
}

void Window::ime_set_enabled(bool enable) {
    ime_enabled_ = enable;
    if (enable) {
        text_input_enable_();
    } else {
        text_input_disable_();
    }
}

// ---------------------------------------------------------------------------
bool Window::pump() {
    if (!open_) return false;

    if (needs_redraw_) paint_frame_();

    // Fire any due key repeats before blocking on the socket.
    if (held_evkey_ && repeat_rate_ > 0) {
        uint64_t now = now_ms();
        uint32_t period = 1000u / repeat_rate_;
        if (period == 0) period = 1;
        while (held_evkey_ && now >= held_next_ms_) {
            Event e;
            e.type = Event::Type::KeyDown;
            e.key  = held_key_;
            e.mods = mods_;
            if (event_cb_) event_cb_(e);
            if (held_codepoint_) {
                Event t;
                t.type = Event::Type::TextInput;
                t.mods = mods_;
                t.codepoint = held_codepoint_;
                t.text = held_text_;
                if (event_cb_) event_cb_(t);
            }
            held_next_ms_ += period;
        }
    }

    // Wait for the next event with a small timeout so redraw requests are
    // picked up promptly.
    if (!d_.read_dispatch(16)) {
        open_ = false;
        return false;
    }
    return open_;
}

} // namespace stilus::wlw
