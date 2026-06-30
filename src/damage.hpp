// src/damage.hpp — internal damage-region accumulator.
//
// A DamageRegion tracks which parts of the window need repainting. It stays
// small (a handful of coalesced rects), promotes to "full" when the set grows
// beyond a budget or covers most of the surface, and rounds to integer pixel
// rects on demand for wl_surface.damage_buffer.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "stilus/geom.hpp"

namespace stilus::detail {

struct IRect { int x, y, w, h; };

class DamageRegion {
public:
    // Maximum number of rects we track before collapsing to a single bound.
    static constexpr size_t kMaxRects      = 8;
    // If any single added rect exceeds this fraction of the surface, promote
    // to full. Tuned so common "resize"-sized invalidations become full.
    static constexpr float  kFullThreshold = 0.6f;

    void set_surface(int w, int h) { surf_w_ = w; surf_h_ = h; }

    bool empty() const { return !full_ && rects_.empty(); }
    bool is_full() const { return full_; }
    const std::vector<Rect>& rects() const { return rects_; }

    void clear() {
        rects_.clear();
        full_ = false;
    }

    void mark_full() {
        full_ = true;
        rects_.clear();
    }

    // Add a damage rect in surface-local coords. No-op if already full.
    void add(Rect r) {
        if (full_) return;

        // Clip to surface bounds.
        float x0 = std::max(0.0f, r.x);
        float y0 = std::max(0.0f, r.y);
        float x1 = std::min(float(surf_w_), r.x + r.w);
        float y1 = std::min(float(surf_h_), r.y + r.h);
        if (x1 <= x0 || y1 <= y0) return;
        r = {x0, y0, x1 - x0, y1 - y0};

        // Heuristic: a single huge rect => full damage.
        if (surf_w_ > 0 && surf_h_ > 0) {
            float frac = (r.w * r.h) / float(surf_w_ * surf_h_);
            if (frac >= kFullThreshold) { mark_full(); return; }
        }

        // Try to coalesce with an existing rect that overlaps or touches.
        for (auto& existing : rects_) {
            if (overlap_or_touch_(existing, r)) {
                existing = union_(existing, r);
                return;
            }
        }

        rects_.push_back(r);
        if (rects_.size() > kMaxRects) collapse_();
    }

    // Bounding rect of the damage (undefined if empty).
    Rect bounds() const {
        if (full_) return Rect{0, 0, float(surf_w_), float(surf_h_)};
        if (rects_.empty()) return Rect{0, 0, 0, 0};
        Rect b = rects_[0];
        for (size_t i = 1; i < rects_.size(); ++i) b = union_(b, rects_[i]);
        return b;
    }

    // Integer pixel rect (inclusive min, exclusive max), clipped to surface.
    IRect to_pixels(Rect r) const {
        int x = std::max(0, int(std::floor(r.x)));
        int y = std::max(0, int(std::floor(r.y)));
        int xe = std::min(surf_w_, int(std::ceil(r.x + r.w)));
        int ye = std::min(surf_h_, int(std::ceil(r.y + r.h)));
        return IRect{x, y, std::max(0, xe - x), std::max(0, ye - y)};
    }

private:
    static bool overlap_or_touch_(const Rect& a, const Rect& b) {
        return !(a.x + a.w < b.x || b.x + b.w < a.x ||
                 a.y + a.h < b.y || b.y + b.h < a.y);
    }
    static Rect union_(const Rect& a, const Rect& b) {
        float x0 = std::min(a.x, b.x);
        float y0 = std::min(a.y, b.y);
        float x1 = std::max(a.x + a.w, b.x + b.w);
        float y1 = std::max(a.y + a.h, b.y + b.h);
        return Rect{x0, y0, x1 - x0, y1 - y0};
    }
    void collapse_() {
        Rect b = rects_[0];
        for (size_t i = 1; i < rects_.size(); ++i) b = union_(b, rects_[i]);
        rects_.clear();
        rects_.push_back(b);
    }

    std::vector<Rect> rects_;
    bool              full_   = false;
    int               surf_w_ = 0;
    int               surf_h_ = 0;
};

} // namespace stilus::detail
