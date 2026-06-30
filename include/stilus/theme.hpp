// stilus/theme.hpp — shared visual style
#pragma once
#include <memory>
#include "geom.hpp"
#include "font.hpp"

namespace stilus {

struct Theme {
    // Colors
    Color bg        = Color::rgb(0x1c1c20);
    Color surface   = Color::rgb(0x2a2a30);
    Color surface_hi= Color::rgb(0x35353d);
    Color border    = Color::rgb(0x3f3f47);
    Color primary   = Color::rgb(0x61AFEF);
    Color accent    = Color::rgb(0xE5C07B);
    Color text      = Color::rgb(0xe4e4ea);
    Color text_dim  = Color::rgb(0x9aa0a6);

    // Metrics
    float radius    = 8.f;
    float padding   = 10.f;
    float gap       = 8.f;

    // Fonts (shared by widgets)
    std::shared_ptr<Font> font;
    std::shared_ptr<Font> font_bold;
};

} // namespace stilus
