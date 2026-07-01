// src/platform/wayland/wl_popup.hpp — xdg_popup-backed transient surface.
#pragma once
#include <functional>
#include <memory>
#include <vector>

#include "../platform.hpp"
#include "wl_proto.hpp"

namespace stilus::wlw {

class Window;

class PopupImpl final : public detail::PopupImpl {
public:
    PopupImpl(Window& parent, Rect anchor, int w, int h);
    ~PopupImpl() override;

    int  width()  const override { return width_; }
    int  height() const override { return height_; }
    bool is_open() const override { return open_; }
    void close() override;
    void request_redraw() override { needs_redraw_ = true; }

    void set_frame_cb(std::function<void(Canvas&)> cb) override { frame_cb_ = std::move(cb); }
    void set_event_cb(std::function<void(const Event&)> cb) override { event_cb_ = std::move(cb); }

    // Called by parent Window's pump each frame.
    void tick_paint();

private:
    struct ShmBuffer {
        wl::ObjectId id = 0;
        uint32_t*    pixels = nullptr;
        int          width = 0, height = 0, stride = 0;
        size_t       size = 0;
        bool         busy = false;
        void*        map = nullptr;
        size_t       map_size = 0;
    };
    ShmBuffer* acquire_buffer_();
    void       destroy_buffer_(ShmBuffer& b);
    void       paint_frame_();

    Window* parent_ = nullptr;
    int  width_  = 0;
    int  height_ = 0;
    bool open_        = false;
    bool needs_redraw_ = true;
    bool configured_   = false;

    wl::ObjectId surface_     = 0;
    wl::ObjectId xdg_surface_ = 0;
    wl::ObjectId xdg_popup_   = 0;

    std::vector<std::unique_ptr<ShmBuffer>> buffers_;
    ShmBuffer* last_attached_ = nullptr;

    std::function<void(Canvas&)>       frame_cb_;
    std::function<void(const Event&)>  event_cb_;
};

} // namespace stilus::wlw
