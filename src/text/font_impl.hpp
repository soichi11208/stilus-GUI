// src/text/font_impl.hpp — internal font / glyph cache
#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "stilus/font.hpp"

// stb_truetype is included in the cpp (implementation lives there).
struct stbtt_fontinfo;

namespace stilus {

// Per-glyph raster. Either an 8bpp alpha mask (outline-derived) or a
// premultiplied RGBA bitmap (color emoji from CBDT / sbix / COLRv1 raster).
struct GlyphBitmap {
    std::vector<uint8_t> alpha;  // w*h bytes when !is_color
    std::vector<uint8_t> rgba;   // w*h*4 bytes when is_color (premultiplied)
    int w = 0, h = 0;
    int x_off = 0;              // offset from pen origin (left of bitmap)
    int y_off = 0;              // offset from baseline (top of bitmap, usually negative)
    float advance = 0;          // advance width in pixels
    bool  is_color = false;
};

struct Font::Impl {
    std::vector<uint8_t>            ttf;        // owned font file bytes
    std::unique_ptr<stbtt_fontinfo> info;
    float scale     = 0;                        // px per em-unit
    float pixel_size= 0;
    int   ascent    = 0;
    int   descent   = 0;
    int   line_gap  = 0;

    // Glyph cache keyed by codepoint.
    mutable std::unordered_map<uint32_t, GlyphBitmap> cache;

    // Offsets into `ttf` for CBDT (bitmap data) and CBLC (bitmap location).
    // 0 == not present in this face.
    uint32_t cbdt_off = 0;
    uint32_t cbdt_len = 0;
    uint32_t cblc_off = 0;
    uint32_t cblc_len = 0;
    // Offset of this face's SFNT header inside `ttf` (non-zero for TTC faces
    // beyond index 0).
    uint32_t fontstart = 0;

    // True iff this face has a real glyph for `cp` (stbtt_FindGlyphIndex != 0).
    bool has_glyph(uint32_t cp) const;
    // Rasterize (or return cached) bitmap for `cp`. Caller should check
    // has_glyph() first to know whether the face covers it — this never
    // returns nullptr, but returns a zero-sized/blank bitmap for unknown
    // codepoints (matching legacy behavior).
    const GlyphBitmap* glyph(uint32_t codepoint) const;
};

} // namespace stilus
