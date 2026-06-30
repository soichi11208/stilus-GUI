// stilus/canvas.hpp - drawing API
#pragma once
#include <cstdint>
#include <string_view>
#include <vector>
#include "geom.hpp"
#include "path.hpp"

namespace stilus {

struct PixelImage {
    int width  = 0;
    int height = 0;
    std::vector<uint8_t> data; // RGBA, row-major, unmultiplied

    PixelImage() = default;
    explicit PixelImage(int w, int h, const uint8_t* rgba = nullptr)
        : width(w), height(h) {
        if (rgba) {
            data.assign(rgba, rgba + size_t(4 * w * h));
        } else {
            data.resize(size_t(4 * w * h), 0);
        }
    }
    size_t byte_size() const { return size_t(4 * width * height); }
};

class Font;

class Canvas {
public:
    virtual ~Canvas() = default;

    // Background / primitives (analytic AA, source-over blending)
    virtual void clear(Color c) = 0;
    virtual void fill_rect(Rect r, Color c) = 0;
    virtual void fill_rounded_rect(Rect r, float radius, Color c) = 0;
    virtual void fill_circle(Vec2 center, float radius, Color c) = 0;

    // Strokes — 1px minimum, AA'd outline.
    virtual void stroke_rect(Rect r, float width, Color c) = 0;
    virtual void stroke_rounded_rect(Rect r, float radius, float width, Color c) = 0;

    // Arbitrary filled paths (AA'd via 8× vertical SS + analytic horizontal).
    virtual void fill_path(const Path& p, Color c,
                           FillRule rule = FillRule::NonZero) = 0;

    // Text. 'pos' is the pen origin (baseline).
    virtual void draw_text(Vec2 pos, std::string_view utf8,
                           const Font& f, Color c) = 0;

    // Image. 'pos' is top-left. Alpha channel is used for blending.
    virtual void draw_image(Vec2 pos, const PixelImage& img,
                            float alpha = 1.0f) = 0;

    // Clip stack. Rectangular fast-path, plus arbitrary path clipping.
    virtual void push_clip(Rect r) = 0;
    virtual void push_clip_path(const Path& p,
                                FillRule rule = FillRule::NonZero) = 0;
    virtual void pop_clip() = 0;

    // Affine transform stack. push_transform composes on top of the current
    // transform (i.e. the new top equals current * t).
    virtual void push_transform(const Affine& t) = 0;
    virtual void pop_transform() = 0;
    virtual Affine current_transform() const = 0;

    // Convenience: push_translate(v) == push_transform(Affine::translate(v.x,v.y)).
    // pop_translate() pops the matching transform; kept for source compat.
    void push_translate(Vec2 offset) { push_transform(Affine::translate(offset.x, offset.y)); }
    void pop_translate() { pop_transform(); }

    // Size of the drawable area (in pixels).
    virtual int width()  const = 0;
    virtual int height() const = 0;
};

} // namespace stilus
