// src/render/path_raster.cpp — curve flattening + scanline AA rasterizer
#include "path_raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "soft_canvas.hpp"

namespace stilus::render {

// ---------------------------------------------------------------------------
// Flattening
// ---------------------------------------------------------------------------
// Adaptive subdivision. We bail out when the curve is "flat enough": the
// control polygon is within `tol` of the chord.

static float dist_to_line_sq(Vec2 p, Vec2 a, Vec2 b) {
    float dx = b.x - a.x, dy = b.y - a.y;
    float len2 = dx*dx + dy*dy;
    if (len2 < 1e-20f) {
        float ex = p.x - a.x, ey = p.y - a.y;
        return ex*ex + ey*ey;
    }
    float t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
    float cx = a.x + dx * t, cy = a.y + dy * t;
    float ex = p.x - cx, ey = p.y - cy;
    return ex*ex + ey*ey;
}

static void flatten_quad(Vec2 p0, Vec2 c, Vec2 p1, float tol2,
                         std::vector<Vec2>& out, int depth = 0) {
    if (depth > 18 || dist_to_line_sq(c, p0, p1) <= tol2) {
        out.push_back(p1);
        return;
    }
    Vec2 p01{(p0.x + c.x ) * 0.5f, (p0.y + c.y ) * 0.5f};
    Vec2 p12{(c.x  + p1.x) * 0.5f, (c.y  + p1.y) * 0.5f};
    Vec2 pm {(p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f};
    flatten_quad(p0, p01, pm,  tol2, out, depth + 1);
    flatten_quad(pm, p12, p1,  tol2, out, depth + 1);
}

static void flatten_cubic(Vec2 p0, Vec2 c1, Vec2 c2, Vec2 p1, float tol2,
                          std::vector<Vec2>& out, int depth = 0) {
    if (depth > 18) { out.push_back(p1); return; }
    float d1 = dist_to_line_sq(c1, p0, p1);
    float d2 = dist_to_line_sq(c2, p0, p1);
    if (d1 <= tol2 && d2 <= tol2) {
        out.push_back(p1);
        return;
    }
    Vec2 a  {(p0.x + c1.x) * 0.5f, (p0.y + c1.y) * 0.5f};
    Vec2 b  {(c1.x + c2.x) * 0.5f, (c1.y + c2.y) * 0.5f};
    Vec2 c  {(c2.x + p1.x) * 0.5f, (c2.y + p1.y) * 0.5f};
    Vec2 ab {(a.x  + b.x ) * 0.5f, (a.y  + b.y ) * 0.5f};
    Vec2 bc {(b.x  + c.x ) * 0.5f, (b.y  + c.y ) * 0.5f};
    Vec2 mid{(ab.x + bc.x) * 0.5f, (ab.y + bc.y) * 0.5f};
    flatten_cubic(p0, a,  ab, mid, tol2, out, depth + 1);
    flatten_cubic(mid, bc, c,  p1, tol2, out, depth + 1);
}

void flatten(const Path& in, float tolerance, FlatPath& out) {
    out.pts.clear();
    out.starts.clear();

    float tol2 = tolerance * tolerance;
    Vec2 cur {0, 0};
    Vec2 start{0, 0};
    bool open = false;

    auto begin_contour = [&](Vec2 p) {
        if (open && !out.pts.empty() &&
            out.pts.back().x != start.x && out.pts.back().y != start.y) {
            // Implicit open subpath: leave as-is (filled anyway).
        }
        out.starts.push_back(uint32_t(out.pts.size()));
        out.pts.push_back(p);
        cur = p;
        start = p;
        open = true;
    };

    for (const auto& c : in.commands()) {
        switch (c.cmd) {
            case Path::Cmd::Move:
                begin_contour(c.p[0]);
                break;
            case Path::Cmd::Line:
                if (!open) begin_contour(c.p[0]);
                else { out.pts.push_back(c.p[0]); cur = c.p[0]; }
                break;
            case Path::Cmd::Quad:
                if (!open) begin_contour(cur);
                flatten_quad(cur, c.p[0], c.p[1], tol2, out.pts);
                cur = c.p[1];
                break;
            case Path::Cmd::Cubic:
                if (!open) begin_contour(cur);
                flatten_cubic(cur, c.p[0], c.p[1], c.p[2], tol2, out.pts);
                cur = c.p[2];
                break;
            case Path::Cmd::Close:
                if (open) {
                    // Close the contour by appending the start point if needed.
                    if (out.pts.back().x != start.x || out.pts.back().y != start.y)
                        out.pts.push_back(start);
                    cur = start;
                    open = false;
                }
                break;
        }
    }
    out.starts.push_back(uint32_t(out.pts.size()));  // sentinel
}

// ---------------------------------------------------------------------------
// Rasterizer
// ---------------------------------------------------------------------------
// Vertical SS factor. 8 gives good quality-vs-cost; can be tuned.
static constexpr int SS = 8;

struct Edge {
    float x0, y0, x1, y1;   // y0 < y1
    int   sign;              // +1 when original was going downward (y-increasing)
};

// A single source-over blend of a row's coverage buffer into the canvas.
static inline uint8_t f2u8(float f) {
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    return uint8_t(f * 255.0f + 0.5f);
}

static inline uint32_t pack_xrgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t(0xff) << 24) | (uint32_t(r) << 16) |
           (uint32_t(g)    <<  8) |  uint32_t(b);
}

static inline uint32_t blend_over(uint32_t dst, uint16_t sr, uint16_t sg,
                                  uint16_t sb, uint16_t sa) {
    uint16_t inv = uint16_t(255 - sa);
    uint16_t dr = (dst >> 16) & 0xff;
    uint16_t dg = (dst >>  8) & 0xff;
    uint16_t db =  dst        & 0xff;
    uint16_t r = sr + uint16_t((dr * inv + 127) / 255);
    uint16_t g = sg + uint16_t((dg * inv + 127) / 255);
    uint16_t b = sb + uint16_t((db * inv + 127) / 255);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return pack_xrgb(uint8_t(r), uint8_t(g), uint8_t(b));
}

void fill_flat(SoftCanvas& canvas, const FlatPath& flat,
               Color color, FillRule rule) {
    const int W = canvas.width();
    const int H = canvas.height();
    if (W <= 0 || H <= 0) return;
    if (flat.starts.size() < 2) return;

    // Honor the canvas clip rectangle and any path-mask clip on top.
    Rect cp = canvas.current_clip();
    int clip_l = std::max(0, int(std::floor(cp.x)));
    int clip_t = std::max(0, int(std::floor(cp.y)));
    int clip_r = std::min(W, int(std::ceil (cp.right())));
    int clip_b = std::min(H, int(std::ceil (cp.bottom())));
    if (clip_r <= clip_l || clip_b <= clip_t) return;
    const bool use_mask = canvas.has_mask_clip();

    // 1) Collect edges (skip horizontal ones).
    std::vector<Edge> edges;
    edges.reserve(flat.pts.size());
    float y_min = 1e30f, y_max = -1e30f;

    for (size_t i = 0; i + 1 < flat.starts.size(); ++i) {
        uint32_t a = flat.starts[i];
        uint32_t b = flat.starts[i + 1];
        if (b - a < 2) continue;
        for (uint32_t j = a; j + 1 < b; ++j) {
            Vec2 p0 = flat.pts[j];
            Vec2 p1 = flat.pts[j + 1];
            if (p0.y == p1.y) continue;
            Edge e;
            if (p0.y < p1.y) {
                e = { p0.x, p0.y, p1.x, p1.y, +1 };
            } else {
                e = { p1.x, p1.y, p0.x, p0.y, -1 };
            }
            edges.push_back(e);
            if (e.y0 < y_min) y_min = e.y0;
            if (e.y1 > y_max) y_max = e.y1;
        }
    }
    if (edges.empty()) return;

    int iy0 = std::max(clip_t, int(std::floor(y_min)));
    int iy1 = std::min(clip_b, int(std::ceil (y_max)));
    if (iy1 <= iy0) return;

    // Precompute premultiplied source components (full-coverage = 255).
    float sa_f = color.a;
    uint16_t src_r = f2u8(color.r * sa_f);
    uint16_t src_g = f2u8(color.g * sa_f);
    uint16_t src_b = f2u8(color.b * sa_f);
    uint16_t src_a_full = f2u8(sa_f);

    // Per-row coverage buffer (float accumulator in [0..1]).
    std::vector<float> row(size_t(W), 0.f);
    std::vector<float> xs;
    xs.reserve(64);

    // 2) For each integer scanline, accumulate coverage from SS sub-rows.
    for (int y = iy0; y < iy1; ++y) {
        std::fill(row.begin(), row.end(), 0.f);

        // Dirty range tracked to cheapen the apply pass.
        int dirty_l = W, dirty_r = 0;

        for (int s = 0; s < SS; ++s) {
            float ys = float(y) + (s + 0.5f) / float(SS);

            // Find x-intersections for this sub-row. Using non-zero winding
            // count; also used for even-odd by taking mod 2.
            xs.clear();
            // We encode as pairs (x, sign); flatten later.
            // For correctness with non-zero, we sort and walk, accumulating.
            // For even-odd we just pair.
            for (const auto& e : edges) {
                if (ys < e.y0 || ys >= e.y1) continue;
                float t = (ys - e.y0) / (e.y1 - e.y0);
                float x = e.x0 + (e.x1 - e.x0) * t;
                // Pack sign in the low bit of a float? No — use sentinel list.
                xs.push_back(x);
                xs.push_back(float(e.sign));
            }
            if (xs.empty()) continue;

            // Pair-sort by x. We reshape into (x, sign) pairs via manual
            // sort since the array is interleaved.
            size_t n = xs.size() / 2;
            // Simple index sort for clarity; edges per scanline are few.
            std::vector<size_t> idx(n);
            for (size_t i = 0; i < n; ++i) idx[i] = i;
            std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
                return xs[a * 2] < xs[b * 2];
            });

            float inv_ss = 1.0f / float(SS);
            if (rule == FillRule::EvenOdd) {
                for (size_t i = 0; i + 1 < n; i += 2) {
                    float xa = xs[idx[i]     * 2];
                    float xb = xs[idx[i + 1] * 2];
                    if (xa >= float(W) || xb <= 0.f) continue;
                    if (xa < 0) xa = 0;
                    if (xb > float(W)) xb = float(W);
                    if (xa >= xb) continue;
                    int lo = int(std::floor(xa));
                    int hi = int(std::ceil (xb));
                    if (lo < 0) lo = 0;
                    if (hi > W) hi = W;
                    if (lo < dirty_l) dirty_l = lo;
                    if (hi > dirty_r) dirty_r = hi;
                    if (lo + 1 == hi) {
                        row[lo] += (xb - xa) * inv_ss;
                    } else {
                        row[lo]     += (float(lo + 1) - xa) * inv_ss;
                        row[hi - 1] += (xb - float(hi - 1)) * inv_ss;
                        for (int x = lo + 1; x < hi - 1; ++x) row[x] += inv_ss;
                    }
                }
            } else {
                // Non-zero winding: walk, accumulate, fill where wind != 0.
                int wind = 0;
                float span_start = 0;
                bool in_span = false;
                for (size_t i = 0; i < n; ++i) {
                    float x    = xs[idx[i] * 2];
                    int   sign = int(xs[idx[i] * 2 + 1]);
                    int   prev = wind;
                    wind += sign;
                    if (prev == 0 && wind != 0) {
                        span_start = x; in_span = true;
                    } else if (prev != 0 && wind == 0 && in_span) {
                        float xa = span_start, xb = x;
                        if (xa < float(W) && xb > 0.f) {
                            if (xa < 0) xa = 0;
                            if (xb > float(W)) xb = float(W);
                            if (xa < xb) {
                                int lo = int(std::floor(xa));
                                int hi = int(std::ceil (xb));
                                if (lo < 0) lo = 0;
                                if (hi > W) hi = W;
                                if (lo < dirty_l) dirty_l = lo;
                                if (hi > dirty_r) dirty_r = hi;
                                if (lo + 1 == hi) {
                                    row[lo] += (xb - xa) * inv_ss;
                                } else {
                                    row[lo]     += (float(lo + 1) - xa) * inv_ss;
                                    row[hi - 1] += (xb - float(hi - 1)) * inv_ss;
                                    for (int x2 = lo + 1; x2 < hi - 1; ++x2)
                                        row[x2] += inv_ss;
                                }
                            }
                        }
                        in_span = false;
                    }
                }
            }
        }

        if (dirty_r <= dirty_l) continue;

        // 3) Apply row coverage with clipping.
        Rect cp_rect = {0.f, 0.f, float(W), float(H)};  // canvas clip handled via SoftCanvas API
        // We honor the SoftCanvas clip stack by asking for the current clip.
        // Access it via the public API:
        // (SoftCanvas::push/pop clip only; no getter — but we know the canvas
        // is the intended target and the caller has set the clip. The outer
        // clear/blend paths use the clip via Rect; we intersect here manually
        // by computing an effective range through width/height and assuming
        // the user-specified clip has already narrowed the target region via
        // bbox constraints.)
        (void)cp_rect;

        if (float(y) < 0) continue;
        int l = std::max(clip_l, dirty_l);
        int r = std::min(clip_r, dirty_r);
        uint32_t* dst_row = reinterpret_cast<uint32_t*>(canvas.raw_row(y));
        if (!dst_row) continue;

        for (int x = l; x < r; ++x) {
            float cov = row[x];
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            if (use_mask) {
                cov *= canvas.sample_mask(x, y);
                if (cov <= 0) continue;
            }
            uint16_t sa = uint16_t(f2u8(sa_f * cov));
            if (sa == 0) continue;
            if (cov >= 0.999f) {
                dst_row[x] = blend_over(dst_row[x], src_r, src_g, src_b, src_a_full);
            } else {
                uint16_t sr = uint16_t(f2u8(color.r * sa_f * cov));
                uint16_t sg = uint16_t(f2u8(color.g * sa_f * cov));
                uint16_t sb = uint16_t(f2u8(color.b * sa_f * cov));
                dst_row[x] = blend_over(dst_row[x], sr, sg, sb, sa);
            }
        }
    }
}

} // namespace stilus::render
