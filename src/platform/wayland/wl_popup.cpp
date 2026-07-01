// src/platform/wayland/wl_popup.cpp — xdg_popup implementation.
#include "wl_popup.hpp"
#include "wl_window.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "stilus/canvas.hpp"
#include "render/soft_canvas.hpp"

namespace stilus::wlw {

namespace {
// Anonymous file for a wl_shm pool.
int make_anon_file(size_t size) {
    int fd = ::memfd_create("stilus-popup", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (::ftruncate(fd, off_t(size)) < 0) { ::close(fd); return -1; }
    return fd;
}
} // namespace

PopupImpl::PopupImpl(Window& parent, Rect anchor, int w, int h)
    : parent_(&parent), width_(w), height_(h)
{
    auto& d = parent.display_();

    // 1) wl_surface for the popup.
    surface_ = d.new_id();
    {
        wl::Message m(parent.compositor_id_(),
                      wl::wl_compositor_req::create_surface);
        m.new_id(surface_);
        d.send(m);
    }
    d.set_handler(surface_,
        [](wl::ObjectId, uint16_t, const uint8_t*, size_t,
           const int*, size_t) { /* enter/leave ignored */ });

    // 2) xdg_positioner describing "attach popup below the anchor rect".
    wl::ObjectId positioner = d.new_id();
    {
        wl::Message m(parent.xdg_wm_base_id_(),
                      wl::xdg_wm_base_req::create_positioner);
        m.new_id(positioner);
        d.send(m);
    }
    {
        wl::Message m(positioner, wl::xdg_positioner_req::set_size);
        m.i32(w); m.i32(h);
        d.send(m);
    }
    {
        wl::Message m(positioner, wl::xdg_positioner_req::set_anchor_rect);
        m.i32(int32_t(anchor.x)); m.i32(int32_t(anchor.y));
        // The spec requires w and h > 0.
        m.i32(anchor.w > 0 ? int32_t(anchor.w) : 1);
        m.i32(anchor.h > 0 ? int32_t(anchor.h) : 1);
        d.send(m);
    }
    {
        wl::Message m(positioner, wl::xdg_positioner_req::set_anchor);
        m.u32(wl::xdg_positioner_anchor::bottom_left);
        d.send(m);
    }
    {
        wl::Message m(positioner, wl::xdg_positioner_req::set_gravity);
        m.u32(wl::xdg_positioner_gravity::bottom_right);
        d.send(m);
    }

    // 3) xdg_surface + xdg_popup.
    xdg_surface_ = d.new_id();
    {
        wl::Message m(parent.xdg_wm_base_id_(),
                      wl::xdg_wm_base_req::get_xdg_surface);
        m.new_id(xdg_surface_);
        m.object(surface_);
        d.send(m);
    }
    d.set_handler(xdg_surface_,
        [this, &d](wl::ObjectId id, uint16_t op, const uint8_t* pl, size_t n,
                   const int*, size_t) {
            if (op == wl::xdg_surface_evt::configure) {
                wl::Reader r{pl, n};
                uint32_t serial = r.u32();
                wl::Message m(id, wl::xdg_surface_req::ack_configure);
                m.u32(serial);
                d.send(m);
                configured_ = true;
                needs_redraw_ = true;
            }
        });

    xdg_popup_ = d.new_id();
    {
        wl::Message m(xdg_surface_, wl::xdg_surface_req::get_popup);
        m.new_id(xdg_popup_);
        m.object(parent.xdg_surface_id_());  // parent xdg_surface
        m.object(positioner);
        d.send(m);
    }
    d.set_handler(xdg_popup_,
        [this](wl::ObjectId, uint16_t op, const uint8_t*, size_t,
               const int*, size_t) {
            if (op == wl::xdg_popup_evt::popup_done) {
                // Compositor dismissed us (click outside, focus lost).
                open_ = false;
            }
        });

    // Optional but important: grab so clicks outside dismiss.
    if (parent.seat_id_() && parent.last_pointer_serial_val_()) {
        wl::Message m(xdg_popup_, wl::xdg_popup_req::grab);
        m.object(parent.seat_id_());
        m.u32(parent.last_pointer_serial_val_());
        d.send(m);
    }

    // Kick off the configure cycle with a bare commit.
    {
        wl::Message m(surface_, wl::wl_surface_req::commit);
        d.send(m);
    }
    // Positioner is only needed at creation time.
    {
        wl::Message m(positioner, wl::xdg_positioner_req::destroy);
        d.send(m);
    }

    open_ = true;
    parent.register_popup_(this);
}

PopupImpl::~PopupImpl() {
    if (parent_) parent_->unregister_popup_(this);
    for (auto& b : buffers_) destroy_buffer_(*b);
    auto& d = parent_->display_();
    if (xdg_popup_) {
        wl::Message m(xdg_popup_, wl::xdg_popup_req::destroy);
        d.send(m);
    }
    if (xdg_surface_) {
        wl::Message m(xdg_surface_, wl::xdg_surface_req::destroy);
        d.send(m);
    }
    if (surface_) {
        wl::Message m(surface_, wl::wl_surface_req::destroy);
        d.send(m);
    }
}

void PopupImpl::close() {
    open_ = false;
}

// ---------------------------------------------------------------------------
PopupImpl::ShmBuffer* PopupImpl::acquire_buffer_() {
    for (auto& b : buffers_) {
        if (!b->busy && b->width == width_ && b->height == height_)
            return b.get();
    }
    int stride = width_ * 4;
    size_t sz  = size_t(stride) * size_t(height_);
    int fd = make_anon_file(sz);
    if (fd < 0) return nullptr;
    void* map = ::mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) { ::close(fd); return nullptr; }

    auto& d = parent_->display_();
    wl::ObjectId pool = d.new_id();
    {
        wl::Message m(parent_->shm_id_(), wl::wl_shm_req::create_pool);
        m.new_id(pool);
        m.fd(fd);
        m.i32(int32_t(sz));
        d.send(m);
    }
    wl::ObjectId buf_id = d.new_id();
    {
        wl::Message m(pool, wl::wl_shm_pool_req::create_buffer);
        m.new_id(buf_id);
        m.i32(0);
        m.i32(width_); m.i32(height_);
        m.i32(stride);
        m.u32(wl::shm_format::XRGB8888);
        d.send(m);
    }
    {
        wl::Message m(pool, wl::wl_shm_pool_req::destroy);
        d.send(m);
    }
    ::close(fd);

    auto up = std::make_unique<ShmBuffer>();
    up->id = buf_id;
    up->pixels = reinterpret_cast<uint32_t*>(map);
    up->width = width_; up->height = height_; up->stride = stride;
    up->size = sz; up->map = map; up->map_size = sz;

    ShmBuffer* ref = up.get();
    buffers_.push_back(std::move(up));
    d.set_handler(buf_id,
        [ref](wl::ObjectId, uint16_t op, const uint8_t*, size_t,
              const int*, size_t) {
            if (op == wl::wl_buffer_evt::release) ref->busy = false;
        });
    return ref;
}

void PopupImpl::destroy_buffer_(ShmBuffer& b) {
    if (b.map && b.map != MAP_FAILED) ::munmap(b.map, b.map_size);
    b.map = nullptr;
}

void PopupImpl::paint_frame_() {
    auto& d = parent_->display_();
    ShmBuffer* b = acquire_buffer_();
    if (!b) return;

    render::SoftCanvas canvas;
    canvas.bind(b->pixels, b->width, b->height, b->stride / 4);
    needs_redraw_ = false;

    canvas.clear(Color::rgb(0x2a2a30));
    if (frame_cb_) frame_cb_(canvas);

    {
        wl::Message m(surface_, wl::wl_surface_req::attach);
        m.object(b->id);
        m.i32(0); m.i32(0);
        d.send(m);
    }
    {
        wl::Message m(surface_, wl::wl_surface_req::damage_buffer);
        m.i32(0); m.i32(0);
        m.i32(width_); m.i32(height_);
        d.send(m);
    }
    b->busy = true;
    last_attached_ = b;
    {
        wl::Message m(surface_, wl::wl_surface_req::commit);
        d.send(m);
    }
}

void PopupImpl::tick_paint() {
    if (open_ && configured_ && needs_redraw_) paint_frame_();
}

// ---------------------------------------------------------------------------
// Window ↔ popups integration
// ---------------------------------------------------------------------------
std::unique_ptr<detail::PopupImpl>
Window::create_popup(Rect anchor, int w, int h) {
    if (!xdg_wm_base_ || !compositor_ || !shm_) return nullptr;
    return std::make_unique<PopupImpl>(*this, anchor, w, h);
}

void Window::register_popup_(PopupImpl* p) { popups_.push_back(p); }
void Window::unregister_popup_(PopupImpl* p) {
    popups_.erase(std::remove(popups_.begin(), popups_.end(), p), popups_.end());
}
void Window::paint_popups_() {
    // Iterate a copy to tolerate close() removing the popup mid-loop.
    auto snapshot = popups_;
    for (auto* p : snapshot) p->tick_paint();
}

} // namespace stilus::wlw
