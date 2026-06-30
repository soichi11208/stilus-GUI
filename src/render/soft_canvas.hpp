// src/render/soft_canvas.hpp — CPU software canvas (XRGB8888, source-over AA)
#pragma once
#include <cstdint>
#include <vector>

#include "stilus/canvas.hpp"
#include "stilus/font.hpp"
#include "stilus/path.hpp"

namespace stilus::render {

// Borrows the pixel buffer (does not own).
// Pixel format: 0xXXRRGGBB little-endian uint32_t (matches wl_shm XRGB8888
// byte order B,G,R,X).
class SoftCanvas final : public Canvas {
public:
    SoftCanvas() = default;

    // Rebind to a new backing buffer.
    void bind(uint32_t* px, int w, int h, int stride_px);

    // Canvas interface ------------------------------------------------------
    int  width()  const override { return w_; }
    int  height() const override { return h_; }

    void clear(Color c) override;
    void fill_rect(Rect r, Color c) override;
    void fill_rounded_rect(Rect r, float radius, Color c) override;
    void fill_circle(Vec2 center, float radius, Color c) override;

    void stroke_rect(Rect r, float width, Color c) override;
    void stroke_rounded_rect(Rect r, float radius, float width, Color c) override;

    void fill_path(const Path& p, Color c,
                   FillRule rule = FillRule::NonZero) override;

   void draw_text(Vec2 pos, std::string_view utf8,
                    const Font& f, Color c) override;

    void draw_image(Vec2 pos, const PixelImage& img,
                    float alpha = 1.0f) override;

    void push_clip(Rect r) override;
    void push_clip_path(const Path& p,
                        FillRule rule = FillRule::NonZero) override;
    void pop_clip() override;

    void push_transform(const Affine& t) override;
    void pop_transform() override;
    Affine current_transform() const override { return current_xform_(); }

    // Internal — exposed for the path rasterizer.
    uint8_t* raw_row(int y) {
        if (y < 0 || y >= h_) return nullptr;
        return reinterpret_cast<uint8_t*>(px_ + y * stride_);
    }
    Rect current_clip() const { return current_clip_(); }

    // True if the current clip stack carries a path-coverage mask on top.
    bool has_mask_clip() const { return !mask_clips_.empty(); }
    // Sample the topmost mask coverage at integer pixel (x,y).
    // Returns 1.0 outside the topmost mask bbox (the rectangular clip already
    // excluded that region). For any mask, 0..1 is returned.
    float sample_mask(int x, int y) const;

private:
    // Compose a coverage-weighted color into a single pixel (source-over).
    inline void blend_(int x, int y, uint32_t rgb_prem, uint32_t a_prem);

    // SDF-driven fill: (x,y) -> distance func d; fill pixels where d<=0.
    // Uses a screen-space coverage from pixel center.
    template <typename SDF>
    void fill_sdf_(Rect bb, Color c, SDF sdf);

    Rect   current_clip_() const;
    Affine current_xform_() const;
    // Apply the current transform to an absolute point.
    Vec2   tr_(Vec2 v) const;

    // Each mask clip is a rectangular coverage buffer in surface (pixel)
    // coordinates. clip_stack_ also pushes the bbox so the rectangular fast
    // path tightens accordingly.
    struct MaskClip {
        int x = 0, y = 0;          // top-left in pixel coords
        int w = 0, h = 0;          // dimensions
        std::vector<uint8_t> cov;  // w*h, 0..255 coverage
        size_t clip_stack_depth = 0; // matching position in clip_stack_
    };

    uint32_t*            px_    = nullptr;
    int                  w_     = 0;
    int                  h_     = 0;
    int                  stride_= 0;  // in pixels
    std::vector<Rect>    clip_stack_;
    std::vector<Affine>  xform_stack_;
    std::vector<MaskClip> mask_clips_;
};

} // namespace stilus::render
