// src/render/soft_canvas.cpp
#include "soft_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "stb/stb_truetype.h"
#include "text/font_impl.hpp"
#include "render/path_raster.hpp"

namespace stilus {
namespace detail { uint32_t utf8_decode_next(const char*& p, const char* end); }
}

namespace stilus::render {

// ---------------------------------------------------------------------------
// Pixel / color utilities
// ---------------------------------------------------------------------------
static inline uint32_t pack_xrgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t(0xff) << 24) | (uint32_t(r) << 16) |
           (uint32_t(g)    <<  8) |  uint32_t(b);
}

static inline uint8_t f2u8(float f) {
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    return uint8_t(f * 255.0f + 0.5f);
}

struct Premul { uint16_t r, g, b, a; };

static inline Premul premul(Color c, float cov /*0..1*/) {
    float a = c.a * cov;
    return {
        uint16_t(f2u8(c.r * a)),
        uint16_t(f2u8(c.g * a)),
        uint16_t(f2u8(c.b * a)),
        uint16_t(f2u8(a))
    };
}

static inline uint32_t blend_over(uint32_t dst, Premul s) {
    uint16_t inv = uint16_t(255 - s.a);
    uint16_t dr = (dst >> 16) & 0xff;
    uint16_t dg = (dst >>  8) & 0xff;
    uint16_t db =  dst        & 0xff;
    uint16_t r = s.r + uint16_t((dr * inv + 127) / 255);
    uint16_t g = s.g + uint16_t((dg * inv + 127) / 255);
    uint16_t b = s.b + uint16_t((db * inv + 127) / 255);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return pack_xrgb(uint8_t(r), uint8_t(g), uint8_t(b));
}

static inline uint8_t lerp_u8(uint8_t a, uint8_t b, float t) {
    return uint8_t(float(a) + (float(b) - float(a)) * t + 0.5f);
}

static uint8_t sample_alpha_linear(const uint8_t* src, int w, int h,
                                   float x, float y) {
    if (w <= 0 || h <= 0) return 0;
    x = std::clamp(x, 0.0f, float(w - 1));
    y = std::clamp(y, 0.0f, float(h - 1));
    int x0 = int(std::floor(x));
    int y0 = int(std::floor(y));
    int x1 = std::min(x0 + 1, w - 1);
    int y1 = std::min(y0 + 1, h - 1);
    float tx = x - float(x0);
    float ty = y - float(y0);
    uint8_t a0 = lerp_u8(src[y0 * w + x0], src[y0 * w + x1], tx);
    uint8_t a1 = lerp_u8(src[y1 * w + x0], src[y1 * w + x1], tx);
    return lerp_u8(a0, a1, ty);
}

static Premul sample_premul_linear(const uint8_t* src, int w, int h,
                                   float x, float y) {
    if (w <= 0 || h <= 0) return {0, 0, 0, 0};
    x = std::clamp(x, 0.0f, float(w - 1));
    y = std::clamp(y, 0.0f, float(h - 1));
    int x0 = int(std::floor(x));
    int y0 = int(std::floor(y));
    int x1 = std::min(x0 + 1, w - 1);
    int y1 = std::min(y0 + 1, h - 1);
    float tx = x - float(x0);
    float ty = y - float(y0);

    auto chan = [&](int px, int py, int c) -> uint8_t {
        return src[(py * w + px) * 4 + c];
    };
    Premul out{};
    uint16_t* dst[4] = {&out.r, &out.g, &out.b, &out.a};
    for (int c = 0; c < 4; ++c) {
        uint8_t v0 = lerp_u8(chan(x0, y0, c), chan(x1, y0, c), tx);
        uint8_t v1 = lerp_u8(chan(x0, y1, c), chan(x1, y1, c), tx);
        *dst[c] = lerp_u8(v0, v1, ty);
    }
    return out;
}

// ---------------------------------------------------------------------------
void SoftCanvas::bind(uint32_t* px, int w, int h, int stride_px) {
    px_ = px; w_ = w; h_ = h; stride_ = stride_px;
    clip_stack_.clear();
    clip_stack_.push_back({0.f, 0.f, float(w), float(h)});
    xform_stack_.clear();
    xform_stack_.push_back(Affine::identity());
    mask_clips_.clear();
}

Rect SoftCanvas::current_clip_() const {
    return clip_stack_.back();
}

Affine SoftCanvas::current_xform_() const {
    return xform_stack_.empty() ? Affine::identity() : xform_stack_.back();
}

void SoftCanvas::push_transform(const Affine& t) {
    xform_stack_.push_back(current_xform_() * t);
}

void SoftCanvas::pop_transform() {
    if (xform_stack_.size() > 1) xform_stack_.pop_back();
}

Vec2 SoftCanvas::tr_(Vec2 v) const { return current_xform_().apply(v); }

// ---------------------------------------------------------------------------
// Clip
// ---------------------------------------------------------------------------
// Transform rectangle through an affine to get its axis-aligned bounding box.
static Rect xform_bbox(const Affine& m, Rect r) {
    Vec2 corners[4] = {
        m.apply({r.x,         r.y}),
        m.apply({r.x + r.w,   r.y}),
        m.apply({r.x,         r.y + r.h}),
        m.apply({r.x + r.w,   r.y + r.h})
    };
    float x0 = corners[0].x, x1 = x0;
    float y0 = corners[0].y, y1 = y0;
    for (int i = 1; i < 4; ++i) {
        x0 = std::min(x0, corners[i].x); x1 = std::max(x1, corners[i].x);
        y0 = std::min(y0, corners[i].y); y1 = std::max(y1, corners[i].y);
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

void SoftCanvas::push_clip(Rect r) {
    Rect tr  = xform_bbox(current_xform_(), r);
    Rect cur = current_clip_();
    float x0 = std::max(cur.x,        tr.x);
    float y0 = std::max(cur.y,        tr.y);
    float x1 = std::min(cur.right(),  tr.right());
    float y1 = std::min(cur.bottom(), tr.bottom());
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    clip_stack_.push_back({x0, y0, x1 - x0, y1 - y0});
}

void SoftCanvas::pop_clip() {
    if (clip_stack_.size() > 1) {
        // Pop any mask clip that matched this clip stack depth.
        size_t depth = clip_stack_.size() - 1;
        while (!mask_clips_.empty() && mask_clips_.back().clip_stack_depth == depth) {
            mask_clips_.pop_back();
        }
        clip_stack_.pop_back();
    }
}

float SoftCanvas::sample_mask(int x, int y) const {
    if (mask_clips_.empty()) return 1.0f;
    float cov = 1.0f;
    for (const auto& m : mask_clips_) {
        int lx = x - m.x, ly = y - m.y;
        if (lx < 0 || ly < 0 || lx >= m.w || ly >= m.h) return 0.0f;
        cov *= m.cov[size_t(ly) * size_t(m.w) + size_t(lx)] / 255.0f;
        if (cov <= 0) return 0.0f;
    }
    return cov;
}

// ---------------------------------------------------------------------------
// fill_rect: axis-aligned fast path when the transform is axis-aligned;
// otherwise route through fill_path.
// ---------------------------------------------------------------------------
static void blit_axis_aligned_rect(uint32_t* px, int W, int H, int stride,
                                   Rect rt, Rect clip, Color c,
                                   const SoftCanvas* sc) {
    float x0 = std::max(rt.x,        clip.x);
    float y0 = std::max(rt.y,        clip.y);
    float x1 = std::min(rt.right(),  clip.right());
    float y1 = std::min(rt.bottom(), clip.bottom());
    if (x1 <= x0 || y1 <= y0) return;
    (void)W; (void)H;

    int ix0 = int(std::floor(x0));
    int iy0 = int(std::floor(y0));
    int ix1 = int(std::ceil (x1));
    int iy1 = int(std::ceil (y1));

    bool mask = sc->has_mask_clip();
    for (int y = iy0; y < iy1; ++y) {
        float cov_y = std::min(float(y + 1), y1) - std::max(float(y), y0);
        if (cov_y <= 0) continue;
        uint32_t* row = px + y * stride;
        for (int x = ix0; x < ix1; ++x) {
            float cov_x = std::min(float(x + 1), x1) - std::max(float(x), x0);
            if (cov_x <= 0) continue;
            float cov = cov_x * cov_y;
            if (mask) cov *= sc->sample_mask(x, y);
            if (cov <= 0) continue;
            row[x] = blend_over(row[x], premul(c, cov));
        }
    }
}

// ---------------------------------------------------------------------------
void SoftCanvas::clear(Color c) {
    uint32_t v = pack_xrgb(f2u8(c.r), f2u8(c.g), f2u8(c.b));
    for (int y = 0; y < h_; ++y) {
        uint32_t* row = px_ + y * stride_;
        for (int x = 0; x < w_; ++x) row[x] = v;
    }
}

void SoftCanvas::fill_rect(Rect r, Color c) {
    const Affine& m = current_xform_();
    if (m.is_axis_aligned()) {
        // Map corners directly.
        float x0 = m.a * r.x          + m.e;
        float y0 = m.d * r.y          + m.f;
        float x1 = m.a * (r.x + r.w)  + m.e;
        float y1 = m.d * (r.y + r.h)  + m.f;
        if (x1 < x0) std::swap(x0, x1);
        if (y1 < y0) std::swap(y0, y1);
        Rect tr{x0, y0, x1 - x0, y1 - y0};
        blit_axis_aligned_rect(px_, w_, h_, stride_, tr, current_clip_(), c, this);
        return;
    }
    Path p; p.add_rect(r);
    fill_path(p, c);
}

// ---------------------------------------------------------------------------
template <typename SDF>
void SoftCanvas::fill_sdf_(Rect bb, Color c, SDF sdf) {
    Rect cp = current_clip_();
    float x0 = std::max(bb.x,        cp.x);
    float y0 = std::max(bb.y,        cp.y);
    float x1 = std::min(bb.right(),  cp.right());
    float y1 = std::min(bb.bottom(), cp.bottom());
    if (x1 <= x0 || y1 <= y0) return;

    int ix0 = int(std::floor(x0));
    int iy0 = int(std::floor(y0));
    int ix1 = int(std::ceil (x1));
    int iy1 = int(std::ceil (y1));

    bool mask = has_mask_clip();
    for (int y = iy0; y < iy1; ++y) {
        uint32_t* row = px_ + y * stride_;
        float cy = y + 0.5f;
        for (int x = ix0; x < ix1; ++x) {
            float cx = x + 0.5f;
            float d = sdf(cx, cy);
            float cov = 0.5f - d;
            if (cov <= 0) continue;
            if (cov >= 1) cov = 1;
            if (mask) cov *= sample_mask(x, y);
            if (cov <= 0) continue;
            row[x] = blend_over(row[x], premul(c, cov));
        }
    }
}

// ---------------------------------------------------------------------------
// Rounded rect / circle: keep SDF fast path only for axis-aligned uniform
// scale; arbitrary affine routes through path.
// ---------------------------------------------------------------------------
void SoftCanvas::fill_rounded_rect(Rect r, float radius, Color c) {
    const Affine& m = current_xform_();
    if (!m.is_axis_aligned() || m.a != m.d) {
        Path p; p.add_rounded_rect(r, radius);
        fill_path(p, c);
        return;
    }
    float s = m.a;
    Rect tr{m.a * r.x + m.e, m.d * r.y + m.f, m.a * r.w, m.d * r.h};
    if (tr.w < 0) { tr.x += tr.w; tr.w = -tr.w; }
    if (tr.h < 0) { tr.y += tr.h; tr.h = -tr.h; }
    float rad = radius * std::fabs(s);

    float hw = tr.w * 0.5f, hh = tr.h * 0.5f;
    if (rad <= 0.0f || hw <= 0 || hh <= 0) {
        blit_axis_aligned_rect(px_, w_, h_, stride_, tr, current_clip_(), c, this);
        return;
    }
    if (rad > hw) rad = hw;
    if (rad > hh) rad = hh;
    float cx = tr.x + hw, cy = tr.y + hh;
    auto sdf = [=](float x, float y) {
        float qx = std::fabs(x - cx) - (hw - rad);
        float qy = std::fabs(y - cy) - (hh - rad);
        float ax = std::max(qx, 0.0f);
        float ay = std::max(qy, 0.0f);
        return std::sqrt(ax*ax + ay*ay) + std::min(std::max(qx, qy), 0.0f) - rad;
    };
    fill_sdf_(tr, c, sdf);
}

void SoftCanvas::fill_circle(Vec2 center, float radius, Color c) {
    const Affine& m = current_xform_();
    if (!m.is_axis_aligned() || m.a != m.d) {
        Path p; p.add_circle(center, radius);
        fill_path(p, c);
        return;
    }
    Vec2 ce = m.apply(center);
    float rad = radius * std::fabs(m.a);
    Rect bb{ce.x - rad - 1, ce.y - rad - 1, 2*rad + 2, 2*rad + 2};
    auto sdf = [=](float x, float y) {
        float dx = x - ce.x, dy = y - ce.y;
        return std::sqrt(dx*dx + dy*dy) - rad;
    };
    fill_sdf_(bb, c, sdf);
}

// ---------------------------------------------------------------------------
// Strokes: under affine, route to path-based stroked outline (currently
// implemented as 4 thin rects for the AA case). For non-axis-aligned, route
// the four side rects through fill_rect which handles affine.
// ---------------------------------------------------------------------------
void SoftCanvas::stroke_rect(Rect r, float w, Color c) {
    if (w <= 0) return;
    float hw = w * 0.5f;
    fill_rect({r.x - hw,        r.y - hw,        r.w + w, w     }, c); // top
    fill_rect({r.x - hw,        r.y + r.h - hw,  r.w + w, w     }, c); // bottom
    fill_rect({r.x - hw,        r.y + hw,        w,       r.h - w}, c); // left
    fill_rect({r.x + r.w - hw,  r.y + hw,        w,       r.h - w}, c); // right
}

void SoftCanvas::stroke_rounded_rect(Rect r, float radius, float w, Color c) {
    if (w <= 0) return;
    const Affine& m = current_xform_();
    if (!m.is_axis_aligned() || m.a != m.d) {
        // Cheap approximation: two filled rounded rects (outer minus inner)
        // via even-odd path.
        Path p;
        p.add_rounded_rect({r.x - w*0.5f, r.y - w*0.5f, r.w + w, r.h + w},
                           radius + w*0.5f);
        p.add_rounded_rect({r.x + w*0.5f, r.y + w*0.5f, r.w - w, r.h - w},
                           std::max(0.f, radius - w*0.5f));
        fill_path(p, c, FillRule::EvenOdd);
        return;
    }
    float s = m.a;
    Rect tr{m.a * r.x + m.e, m.d * r.y + m.f, m.a * r.w, m.d * r.h};
    if (tr.w < 0) { tr.x += tr.w; tr.w = -tr.w; }
    if (tr.h < 0) { tr.y += tr.h; tr.h = -tr.h; }
    float rad = radius * std::fabs(s);
    float ww  = w      * std::fabs(s);

    float hw = ww * 0.5f;
    Rect outer{ tr.x - hw, tr.y - hw, tr.w + ww, tr.h + ww };
    float r_outer = rad + hw;
    float r_inner = std::max(0.f, rad - hw);
    float cx = tr.x + tr.w * 0.5f;
    float cy = tr.y + tr.h * 0.5f;
    float hw1 = outer.w * 0.5f, hh1 = outer.h * 0.5f;
    float hw2 = (tr.w - ww) * 0.5f, hh2 = (tr.h - ww) * 0.5f;
    auto sdf = [=](float x, float y) {
        auto d_rr = [](float qx, float qy, float hwx, float hwy, float rr) {
            float px = std::fabs(qx) - (hwx - rr);
            float py = std::fabs(qy) - (hwy - rr);
            float ax = std::max(px, 0.0f);
            float ay = std::max(py, 0.0f);
            return std::sqrt(ax*ax + ay*ay) + std::min(std::max(px, py), 0.0f) - rr;
        };
        float d1 = d_rr(x - cx, y - cy, hw1, hh1, r_outer);
        float d2 = d_rr(x - cx, y - cy, hw2, hh2, r_inner);
        return std::max(d1, -d2);
    };
    fill_sdf_(outer, c, sdf);
}

// ---------------------------------------------------------------------------
// Text. Currently honors translation + uniform axis-aligned scale only.
// Rotated/sheared transforms fall back to placing glyphs at the transformed
// pen positions but with un-rotated bitmaps — adequate as a placeholder until
// we have a real glyph-path rasterizer.
// ---------------------------------------------------------------------------
void SoftCanvas::draw_text(Vec2 pos, std::string_view utf8,
                           const Font& f, Color c) {
    if (!f.valid() || utf8.empty()) return;
    const Affine& m = current_xform_();
    Vec2 origin = m.apply(pos);

    Rect cp = current_clip_();

    // Effective horizontal scale = length of x basis vector after transform.
    // (Equal to |a| for axis-aligned.) Used to scale glyph bitmaps too.
    float sx = std::sqrt(m.a*m.a + m.b*m.b);
    if (sx <= 0) sx = 1;

    const bool integer_scale = m.is_axis_aligned() && m.a > 0 && m.d > 0;
    bool mask = has_mask_clip();

    const char* p   = utf8.data();
    const char* end = p + utf8.size();
    float pen_x = origin.x;
    float pen_y = origin.y;

    const Font::Impl* prev_face = nullptr;
    uint32_t prev = 0;
    while (p < end) {
        uint32_t codepoint = detail::utf8_decode_next(p, end);
        if (!codepoint) break;
        auto res = f.resolve(codepoint);
        if (!res.glyph) continue;
        const GlyphBitmap* g = res.glyph;
        const Font::Impl*  fi = res.face;

        if (g->w > 0 && g->h > 0 && integer_scale) {
            float bitmap_scale = g->is_color ? 1.0f : g->bitmap_scale;
            if (bitmap_scale <= 0.0f) bitmap_scale = 1.0f;
            float scx = m.a, scy = m.d;
            int gx0 = int(std::round(pen_x + (float(g->x_off) / bitmap_scale) * scx));
            int gy0 = int(std::round(pen_y + (float(g->y_off) / bitmap_scale) * scy));
            int gw  = std::max(1, int(std::round((float(g->w) / bitmap_scale) * scx)));
            int gh  = std::max(1, int(std::round((float(g->h) / bitmap_scale) * scy)));
            int gx1 = gx0 + gw;
            int gy1 = gy0 + gh;

            int cx0 = std::max(gx0, int(std::floor(cp.x)));
            int cy0 = std::max(gy0, int(std::floor(cp.y)));
            int cx1 = std::min(gx1, int(std::ceil (cp.right())));
            int cy1 = std::min(gy1, int(std::ceil (cp.bottom())));
            cx0 = std::max(cx0, 0);
            cy0 = std::max(cy0, 0);
            cx1 = std::min(cx1, w_);
            cy1 = std::min(cy1, h_);

            if (g->is_color) {
                // Premultiplied RGBA source-over; linearly filtered.
                for (int y = cy0; y < cy1; ++y) {
                    float sy = ((float(y) + 0.5f - float(gy0)) * float(g->h) / float(gh)) - 0.5f;
                    uint32_t* row = px_ + y * stride_;
                    for (int x = cx0; x < cx1; ++x) {
                        float sxp = ((float(x) + 0.5f - float(gx0)) * float(g->w) / float(gw)) - 0.5f;
                        Premul sp = sample_premul_linear(g->rgba.data(), g->w, g->h, sxp, sy);
                        if (sp.a == 0) continue;
                        if (mask) {
                            float mv = sample_mask(x, y);
                            if (mv <= 0) continue;
                            if (mv < 1) {
                                sp.r = uint16_t(sp.r * mv);
                                sp.g = uint16_t(sp.g * mv);
                                sp.b = uint16_t(sp.b * mv);
                                sp.a = uint16_t(sp.a * mv);
                            }
                        }
                        row[x] = blend_over(row[x], sp);
                    }
                }
            } else {
                for (int y = cy0; y < cy1; ++y) {
                    float sy = ((float(y) + 0.5f - float(gy0)) * float(g->h) / float(gh)) - 0.5f;
                    uint32_t* row = px_ + y * stride_;
                    for (int x = cx0; x < cx1; ++x) {
                        float sxp = ((float(x) + 0.5f - float(gx0)) * float(g->w) / float(gw)) - 0.5f;
                        uint8_t a = sample_alpha_linear(g->alpha.data(), g->w, g->h, sxp, sy);
                        if (!a) continue;
                        float cov = a / 255.0f;
                        if (mask) cov *= sample_mask(x, y);
                        if (cov <= 0) continue;
                        row[x] = blend_over(row[x], premul(c, cov));
                    }
                }
            }
        }
        pen_x += g->advance * sx;

        // Kern only within the same face. stb's kerning table is per-font;
        // cross-face kerning would be meaningless.
        if (prev && prev_face == fi) {
            int k = stbtt_GetCodepointKernAdvance(fi->info.get(),
                                                  int(prev), int(codepoint));
            pen_x += k * fi->scale * sx;
        }
        prev = codepoint;
        prev_face = fi;
    }
}

// ---------------------------------------------------------------------------
// Image: axis-aligned only fast path. Affine rotation/shear falls back to a
// per-pixel inverse-mapped sampler.
// ---------------------------------------------------------------------------
void SoftCanvas::draw_image(Vec2 pos, const PixelImage& img, float alpha) {
    if (img.width <= 0 || img.height <= 0 || !img.data.data()) return;
    if (alpha <= 0) return;

    const Affine& m = current_xform_();
    bool mask = has_mask_clip();
    Rect cp = current_clip_();

    if (m.is_axis_aligned()) {
        Vec2 p0 = m.apply(pos);
        float scx = m.a, scy = m.d;
        Rect r{p0.x, p0.y, img.width * scx, img.height * scy};
        if (r.w < 0) { r.x += r.w; r.w = -r.w; }
        if (r.h < 0) { r.y += r.h; r.h = -r.h; }
        float x0 = std::max(r.x, cp.x), y0 = std::max(r.y, cp.y);
        float x1 = std::min(r.right(), cp.right()), y1 = std::min(r.bottom(), cp.bottom());
        if (x1 <= x0 || y1 <= y0) return;
        int ix0 = int(std::floor(x0)), iy0 = int(std::floor(y0));
        int ix1 = int(std::ceil (x1)), iy1 = int(std::ceil (y1));
        for (int y = iy0; y < iy1; ++y) {
            uint32_t* row = px_ + y * stride_;
            int sy = int((y - r.y) * img.height / r.h);
            if (sy < 0) sy = 0;
            if (sy >= img.height) sy = img.height - 1;
            for (int x = ix0; x < ix1; ++x) {
                int sx2 = int((x - r.x) * img.width / r.w);
                if (sx2 < 0) sx2 = 0;
                if (sx2 >= img.width) sx2 = img.width - 1;
                const uint8_t* src = img.data.data() + (sy * img.width + sx2) * 4;
                float cov = (src[3] / 255.0f) * alpha;
                if (mask) cov *= sample_mask(x, y);
                if (cov <= 0) continue;
                Color sc{ src[0]/255.0f, src[1]/255.0f, src[2]/255.0f, 1.0f };
                row[x] = blend_over(row[x], premul(sc, cov));
            }
        }
        return;
    }

    // Arbitrary affine: rasterize the transformed image rectangle and sample
    // the source by inverse-mapping pixel centers.
    Rect bb = xform_bbox(m, {pos.x, pos.y, float(img.width), float(img.height)});
    float x0 = std::max(bb.x, cp.x), y0 = std::max(bb.y, cp.y);
    float x1 = std::min(bb.right(), cp.right()), y1 = std::min(bb.bottom(), cp.bottom());
    if (x1 <= x0 || y1 <= y0) return;
    int ix0 = int(std::floor(x0)), iy0 = int(std::floor(y0));
    int ix1 = int(std::ceil (x1)), iy1 = int(std::ceil (y1));
    Affine inv = m.inverse();
    for (int y = iy0; y < iy1; ++y) {
        uint32_t* row = px_ + y * stride_;
        for (int x = ix0; x < ix1; ++x) {
            Vec2 u = inv.apply({x + 0.5f, y + 0.5f});
            int sx2 = int(u.x - pos.x);
            int sy  = int(u.y - pos.y);
            if (sx2 < 0 || sy < 0 || sx2 >= img.width || sy >= img.height) continue;
            const uint8_t* src = img.data.data() + (sy * img.width + sx2) * 4;
            float cov = (src[3] / 255.0f) * alpha;
            if (mask) cov *= sample_mask(x, y);
            if (cov <= 0) continue;
            Color sc{ src[0]/255.0f, src[1]/255.0f, src[2]/255.0f, 1.0f };
            row[x] = blend_over(row[x], premul(sc, cov));
        }
    }
}

void SoftCanvas::blend_(int, int, uint32_t, uint32_t) {}

// ---------------------------------------------------------------------------
void SoftCanvas::fill_path(const Path& p, Color c, FillRule rule) {
    static thread_local FlatPath flat;
    flatten(p, 0.25f, flat);
    const Affine& m = current_xform_();
    if (!m.is_translation() && !(m.is_axis_aligned() && m.a == 1 && m.d == 1)) {
        for (auto& pt : flat.pts) pt = m.apply(pt);
    } else if (m.e != 0 || m.f != 0) {
        for (auto& pt : flat.pts) { pt.x += m.e; pt.y += m.f; }
    }
    fill_flat(*this, flat, c, rule);
}

// ---------------------------------------------------------------------------
// push_clip_path: rasterize the path under the current transform into an
// 8-bit coverage buffer covering its bbox (intersected with the current rect
// clip). The bbox is also pushed as a rectangular clip so the rectangular
// fast paths still benefit.
// ---------------------------------------------------------------------------
void SoftCanvas::push_clip_path(const Path& p, FillRule rule) {
    static thread_local FlatPath flat;
    flatten(p, 0.25f, flat);
    const Affine& m = current_xform_();
    for (auto& pt : flat.pts) pt = m.apply(pt);

    float xmin = 1e30f, xmax = -1e30f, ymin = 1e30f, ymax = -1e30f;
    for (const auto& pt : flat.pts) {
        if (pt.x < xmin) xmin = pt.x;
        if (pt.x > xmax) xmax = pt.x;
        if (pt.y < ymin) ymin = pt.y;
        if (pt.y > ymax) ymax = pt.y;
    }
    Rect cp = current_clip_();
    if (xmin < cp.x)        xmin = cp.x;
    if (ymin < cp.y)        ymin = cp.y;
    if (xmax > cp.right())  xmax = cp.right();
    if (ymax > cp.bottom()) ymax = cp.bottom();
    if (xmax <= xmin || ymax <= ymin) {
        clip_stack_.push_back({xmin, ymin, 0, 0});
        return;
    }
    int x0 = int(std::floor(xmin)), y0 = int(std::floor(ymin));
    int x1 = int(std::ceil (xmax)), y1 = int(std::ceil (ymax));
    x0 = std::max(0, x0); y0 = std::max(0, y0);
    x1 = std::min(w_, x1); y1 = std::min(h_, y1);
    int mw = x1 - x0, mh = y1 - y0;
    if (mw <= 0 || mh <= 0) {
        clip_stack_.push_back({float(x0), float(y0), 0, 0});
        return;
    }

    // Rasterize into a temporary alpha buffer using a stripped-down version of
    // the scanline AA rasterizer (8x vertical SS).
    constexpr int SS = 8;
    std::vector<uint8_t> cov(size_t(mw) * size_t(mh), 0);
    std::vector<float>   row(size_t(mw), 0.f);

    struct E { float x0,y0,x1,y1; int sign; };
    std::vector<E> edges;
    edges.reserve(flat.pts.size());
    for (size_t i = 0; i + 1 < flat.starts.size(); ++i) {
        uint32_t a = flat.starts[i];
        uint32_t b = flat.starts[i + 1];
        if (b - a < 2) continue;
        for (uint32_t j = a; j + 1 < b; ++j) {
            Vec2 q0 = flat.pts[j], q1 = flat.pts[j + 1];
            if (q0.y == q1.y) continue;
            E e = q0.y < q1.y
                ? E{q0.x, q0.y, q1.x, q1.y, +1}
                : E{q1.x, q1.y, q0.x, q0.y, -1};
            edges.push_back(e);
        }
    }

    auto add_span = [&](int rowx, float xa, float xb, float inv_ss) {
        if (xa >= xb) return;
        if (xa < float(x0)) xa = float(x0);
        if (xb > float(x1)) xb = float(x1);
        if (xa >= xb) return;
        int lo = int(std::floor(xa)) - x0;
        int hi = int(std::ceil (xb)) - x0;
        if (lo < 0) lo = 0;
        if (hi > mw) hi = mw;
        if (lo + 1 == hi) {
            row[lo] += (xb - xa) * inv_ss;
        } else {
            row[lo]     += (float(lo + 1 + x0) - xa) * inv_ss;
            row[hi - 1] += (xb - float(hi - 1 + x0)) * inv_ss;
            for (int x = lo + 1; x < hi - 1; ++x) row[x] += inv_ss;
        }
        (void)rowx;
    };

    for (int y = y0; y < y1; ++y) {
        std::fill(row.begin(), row.end(), 0.f);
        float inv_ss = 1.0f / float(SS);
        for (int s = 0; s < SS; ++s) {
            float ys = float(y) + (s + 0.5f) / float(SS);
            // Gather x intersections + signs.
            std::vector<std::pair<float,int>> xs;
            for (const auto& e : edges) {
                if (ys < e.y0 || ys >= e.y1) continue;
                float t = (ys - e.y0) / (e.y1 - e.y0);
                xs.push_back({e.x0 + (e.x1 - e.x0) * t, e.sign});
            }
            std::sort(xs.begin(), xs.end(),
                      [](auto& A, auto& B){ return A.first < B.first; });
            if (rule == FillRule::EvenOdd) {
                for (size_t i = 0; i + 1 < xs.size(); i += 2)
                    add_span(y, xs[i].first, xs[i+1].first, inv_ss);
            } else {
                int wind = 0; float span_start = 0; bool in_span = false;
                for (auto& [x, sn] : xs) {
                    int prev = wind; wind += sn;
                    if (prev == 0 && wind != 0) { span_start = x; in_span = true; }
                    else if (prev != 0 && wind == 0 && in_span) {
                        add_span(y, span_start, x, inv_ss);
                        in_span = false;
                    }
                }
            }
        }
        uint8_t* dst = cov.data() + size_t(y - y0) * size_t(mw);
        for (int x = 0; x < mw; ++x) {
            float v = row[x];
            if (v < 0) v = 0;
            if (v > 1) v = 1;
            dst[x] = uint8_t(v * 255.0f + 0.5f);
        }
    }

    // Push the bbox as a rectangular clip and store the mask.
    clip_stack_.push_back({float(x0), float(y0), float(mw), float(mh)});
    MaskClip mc;
    mc.x = x0; mc.y = y0; mc.w = mw; mc.h = mh;
    mc.cov = std::move(cov);
    mc.clip_stack_depth = clip_stack_.size() - 1;
    mask_clips_.push_back(std::move(mc));
}

} // namespace stilus::render
