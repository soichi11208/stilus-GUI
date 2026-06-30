// src/platform/wayland/wl_factory.cpp
#include "wl_window.hpp"

namespace stilus::detail {

std::unique_ptr<WindowImpl>
create_wayland_window(std::string_view title, int w, int h) {
    return std::make_unique<stilus::wlw::Window>(title, w, h);
}

} // namespace stilus::detail
