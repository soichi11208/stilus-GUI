// src/platform/x11/xcb_factory.cpp
#include "../platform.hpp"
#include "xcb_window.hpp"

namespace stilus::detail {

std::unique_ptr<WindowImpl> create_x11_window(std::string_view title, int w, int h) {
    return std::make_unique<xcbw::Window>(title, w, h);
}

} // namespace stilus::detail
