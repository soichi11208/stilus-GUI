// stilus/popup.hpp - transient overlay surface (menus / tooltips / combobox).
#pragma once
#include <functional>
#include <memory>

#include "event.hpp"
#include "geom.hpp"

namespace stilus {

class Canvas;
class Window;

namespace detail { class PopupImpl; }

// Short-lived surface anchored to a rect inside a parent Window. Typically
// used for menus, tooltips, and dropdown lists. The popup takes an implicit
// grab (Wayland xdg_popup grab / X11 override-redirect + confine): clicking
// outside it closes it.
class Popup {
public:
    Popup(Window& parent, Rect anchor_in_parent, int width, int height);
    ~Popup();
    Popup(const Popup&) = delete;
    Popup& operator=(const Popup&) = delete;

    void on_frame(std::function<void(Canvas&)> cb);
    void on_event(std::function<void(const Event&)> cb);

    void request_redraw();
    void close();
    bool is_open() const;

    int  width()  const;
    int  height() const;

private:
    std::unique_ptr<detail::PopupImpl> impl_;
};

} // namespace stilus
