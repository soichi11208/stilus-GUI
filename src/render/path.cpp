// src/render/path.cpp — Path helpers (primitive-shape constructors)
#include "stilus/path.hpp"

namespace stilus {

// Cubic Bezier approximation factor for a 90° arc — the classic constant.
// Four segments approximate a circle to ~0.02% error.
static constexpr float kQuarterArcK = 0.5522847498307936f;

Path& Path::add_rect(Rect r) {
    return move_to({r.x,       r.y})
          .line_to({r.x + r.w, r.y})
          .line_to({r.x + r.w, r.y + r.h})
          .line_to({r.x,       r.y + r.h})
          .close_();
}

Path& Path::add_rounded_rect(Rect r, float radius) {
    float rx = radius, ry = radius;
    if (rx > r.w * 0.5f) rx = r.w * 0.5f;
    if (ry > r.h * 0.5f) ry = r.h * 0.5f;
    if (rx <= 0 && ry <= 0) return add_rect(r);

    float kx = kQuarterArcK * rx;
    float ky = kQuarterArcK * ry;
    float x0 = r.x,           y0 = r.y;
    float x1 = r.x + r.w,     y1 = r.y + r.h;

    move_to ({x0 + rx, y0});
    line_to ({x1 - rx, y0});
    cubic_to({x1 - rx + kx, y0}, {x1, y0 + ry - ky}, {x1, y0 + ry});
    line_to ({x1, y1 - ry});
    cubic_to({x1, y1 - ry + ky}, {x1 - rx + kx, y1}, {x1 - rx, y1});
    line_to ({x0 + rx, y1});
    cubic_to({x0 + rx - kx, y1}, {x0, y1 - ry + ky}, {x0, y1 - ry});
    line_to ({x0, y0 + ry});
    cubic_to({x0, y0 + ry - ky}, {x0 + rx - kx, y0}, {x0 + rx, y0});
    close_();
    return *this;
}

Path& Path::add_circle(Vec2 c, float r) {
    float k = kQuarterArcK * r;
    move_to ({c.x + r, c.y});
    cubic_to({c.x + r, c.y + k}, {c.x + k, c.y + r}, {c.x,     c.y + r});
    cubic_to({c.x - k, c.y + r}, {c.x - r, c.y + k}, {c.x - r, c.y    });
    cubic_to({c.x - r, c.y - k}, {c.x - k, c.y - r}, {c.x,     c.y - r});
    cubic_to({c.x + k, c.y - r}, {c.x + r, c.y - k}, {c.x + r, c.y    });
    close_();
    return *this;
}

} // namespace stilus
