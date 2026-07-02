// src/text/font.cpp
#include "font_impl.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG          // only PNG — CBDT emoji use PNG data
#define STBI_NO_STDIO          // we always decode from memory
// With STBI_ONLY_PNG some overflow-check helpers are compiled but unused.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb/stb_image.h"
#pragma GCC diagnostic pop

namespace stilus {

// ---- CBDT/CBLC (color emoji bitmap) parsing -------------------------------
// SFNT big-endian reads.
static uint16_t be16(const uint8_t* p) { return uint16_t(uint16_t(p[0]) << 8 | p[1]); }
static uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) <<  8) |  uint32_t(p[3]);
}
static constexpr uint32_t kTagCBDT = 0x43424454u; // 'CBDT'
static constexpr uint32_t kTagCBLC = 0x4342'4C43u; // 'CBLC'

// Validate the SFNT structure at `fontstart` well enough that stb_truetype's
// unchecked reads stay within the buffer: header magic, table directory
// bounds, and every table's (offset, length) range. stb_truetype explicitly
// does no bounds checking ("Not safe on untrusted fonts"), so this pre-pass
// is what stands between a truncated/corrupt font file and a wild read.
static bool sfnt_validate(const uint8_t* data, size_t len, uint32_t fontstart) {
    if (fontstart > len || len - fontstart < 12) return false;
    const uint8_t* p = data + fontstart;
    uint32_t ver = be32(p);
    if (ver != 0x00010000u && ver != 0x4F54544Fu /*OTTO*/ &&
        ver != 0x74727565u /*true*/ && ver != 0x74797031u /*typ1*/)
        return false;
    uint32_t numTables = be16(p + 4);
    // Directory must fit.
    if (numTables > 4096) return false;
    uint64_t dir_end = uint64_t(fontstart) + 12 + uint64_t(numTables) * 16;
    if (dir_end > len) return false;
    for (uint32_t i = 0; i < numTables; ++i) {
        const uint8_t* e = p + 12 + i * 16;
        uint64_t off = be32(e + 8);
        uint64_t tl  = be32(e + 12);
        if (off + tl > len || off + tl < off) return false;
    }
    return true;
}

// Locate an SFNT table inside a face at `fontstart`. Populates {off, len} in
// file-absolute units. Returns false if the table isn't present.
static bool find_sfnt_table(const uint8_t* data, size_t data_len,
                            uint32_t fontstart, uint32_t tag,
                            uint32_t& out_off, uint32_t& out_len) {
    if (fontstart + 12 > data_len) return false;
    uint16_t numTables = be16(data + fontstart + 4);
    uint32_t p = fontstart + 12;
    if (p + size_t(numTables) * 16 > data_len) return false;
    for (uint16_t i = 0; i < numTables; ++i, p += 16) {
        if (be32(data + p) == tag) {
            out_off = be32(data + p + 8);
            out_len = be32(data + p + 12);
            return true;
        }
    }
    return false;
}

// Returned metadata for one CBDT glyph. src_w/src_h are the native bitmap
// size (matching the PNG we decode); bearingX/bearingY follow the OpenType
// small-metrics convention (bearingY = pixels from baseline to top-of-glyph).
struct EmojiHit {
    std::vector<uint8_t> png;    // encoded PNG blob
    int src_w = 0, src_h = 0;     // (filled in after PNG decode)
    int bearingX = 0;
    int bearingY = 0;
    int adv      = 0;             // native pixel advance
    int src_ppem = 0;             // ppemY of the selected bitmap strike
};

// Walk CBLC/CBDT to find PNG data for a given glyph index at a target ppem.
// We pick the bitmap strike whose ppemY is closest to `target_ppem`.
static bool cbdt_find_glyph(const uint8_t* ttf, size_t ttf_len,
                            uint32_t cblc_off, uint32_t cblc_len,
                            uint32_t cbdt_off, uint32_t cbdt_len,
                            int glyph_id, float target_ppem,
                            EmojiHit& hit) {
    if (cblc_len < 8 || cbdt_len == 0) return false;
    if (cblc_off + cblc_len > ttf_len) return false;
    if (cbdt_off + cbdt_len > ttf_len) return false;
    const uint8_t* p = ttf + cblc_off;

    uint32_t numSizes = be32(p + 4);
    if (numSizes == 0 || numSizes > 100) return false;
    if (8 + numSizes * 48 > cblc_len) return false;

    // 1) Pick the best BitmapSize entry containing our glyph.
    uint32_t best_bs = 0;
    int      best_diff = 1'000'000;
    bool     found_bs = false;
    for (uint32_t i = 0; i < numSizes; ++i) {
        uint32_t bs = 8 + i * 48;
        uint16_t startGid = be16(p + bs + 40);
        uint16_t endGid   = be16(p + bs + 42);
        if (glyph_id < startGid || glyph_id > endGid) continue;
        uint8_t ppemY = p[bs + 45];
        int diff = int(ppemY) - int(target_ppem);
        if (diff < 0) diff = -diff;
        if (diff < best_diff) { best_diff = diff; best_bs = bs; found_bs = true; }
    }
    if (!found_bs) return false;

    uint32_t idxArrOff = be32(p + best_bs + 0);
    uint32_t numIdx    = be32(p + best_bs + 8);
    uint8_t  ppemY     = p[best_bs + 45];
    if (idxArrOff + numIdx * 8 > cblc_len) return false;
    hit.src_ppem = int(ppemY);

    // 2) Locate the indexSubTable covering `glyph_id`.
    for (uint32_t i = 0; i < numIdx; ++i) {
        uint32_t entry = idxArrOff + i * 8;
        uint16_t first = be16(p + entry + 0);
        uint16_t last  = be16(p + entry + 2);
        if (glyph_id < first || glyph_id > last) continue;
        uint32_t extra = be32(p + entry + 4);
        uint32_t sub   = idxArrOff + extra;
        if (sub + 8 > cblc_len) return false;

        uint16_t indexFormat = be16(p + sub + 0);
        uint16_t imageFormat = be16(p + sub + 2);
        uint32_t imgOffBase  = be32(p + sub + 4);
        int      rel         = glyph_id - first;

        uint32_t g_off = 0, g_len = 0;
        if (indexFormat == 1) {
            uint32_t oa = sub + 8;
            if (oa + (rel + 2) * 4 > cblc_len) return false;
            uint32_t o0 = be32(p + oa + rel * 4);
            uint32_t o1 = be32(p + oa + (rel + 1) * 4);
            g_off = imgOffBase + o0;
            g_len = o1 > o0 ? o1 - o0 : 0;
        } else if (indexFormat == 3) {
            uint32_t oa = sub + 8;
            if (oa + (rel + 2) * 2 > cblc_len) return false;
            uint16_t o0 = be16(p + oa + rel * 2);
            uint16_t o1 = be16(p + oa + (rel + 1) * 2);
            g_off = imgOffBase + o0;
            g_len = o1 > o0 ? o1 - o0 : 0;
        } else {
            return false; // other index formats not supported yet
        }

        // 3) Read the CBDT record. Only imageFormats 17 (small metrics + PNG)
        //    and 18 (big metrics + PNG) are supported.
        if (g_off + g_len > cbdt_len) return false;
        const uint8_t* g = ttf + cbdt_off + g_off;
        if (imageFormat == 17) {
            if (g_len < 9) return false;
            int8_t  bx = int8_t(g[2]);
            int8_t  by = int8_t(g[3]);
            uint8_t av = g[4];
            uint32_t dataLen = be32(g + 5);
            if (uint64_t(9) + dataLen > g_len) return false;
            hit.png.assign(g + 9, g + 9 + dataLen);
            hit.bearingX = bx; hit.bearingY = by; hit.adv = av;
            return true;
        } else if (imageFormat == 18) {
            if (g_len < 12) return false;
            int8_t  bx = int8_t(g[2]);
            int8_t  by = int8_t(g[3]);
            uint8_t av = g[4];
            uint32_t dataLen = be32(g + 8);
            if (uint64_t(12) + dataLen > g_len) return false;
            hit.png.assign(g + 12, g + 12 + dataLen);
            hit.bearingX = bx; hit.bearingY = by; hit.adv = av;
            return true;
        }
        return false;
    }
    return false;
}


// ---- UTF-8 ----------------------------------------------------------------
static uint32_t utf8_next(const char*& p, const char* end) {
    if (p >= end) return 0;
    uint8_t c = uint8_t(*p++);
    if (c < 0x80) return c;
    int extra;
    uint32_t cp;
    if      ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    else return 0xFFFD;
    while (extra--) {
        if (p >= end) return 0xFFFD;
        uint8_t n = uint8_t(*p++);
        if ((n & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (n & 0x3F);
    }
    return cp;
}

namespace detail {
uint32_t utf8_decode_next(const char*& p, const char* end) {
    return utf8_next(p, end);
}
}

// ---- Per-face glyph operations --------------------------------------------
bool Font::Impl::has_glyph(uint32_t cp) const {
    return info && stbtt_FindGlyphIndex(info.get(), int(cp)) != 0;
}

const GlyphBitmap* Font::Impl::glyph(uint32_t cp) const {
    auto it = cache.find(cp);
    if (it != cache.end()) return &it->second;

    GlyphBitmap g;

    // Fast path for color bitmap fonts (Noto Color Emoji etc.). If the face
    // has CBDT/CBLC tables and this codepoint has a bitmap glyph, decode
    // the embedded PNG, nearest-scale to the target size and cache as RGBA.
    if (cbdt_off && cblc_off) {
        int gid = stbtt_FindGlyphIndex(info.get(), int(cp));
        if (gid != 0) {
            EmojiHit hit;
            if (cbdt_find_glyph(ttf.data(), ttf.size(),
                                cblc_off, cblc_len, cbdt_off, cbdt_len,
                                gid, pixel_size, hit)) {
                int iw = 0, ih = 0, ic = 0;
                stbi_uc* px = stbi_load_from_memory(
                    hit.png.data(), int(hit.png.size()),
                    &iw, &ih, &ic, 4);
                if (px) {
                    float sc = pixel_size / float(hit.src_ppem);
                    int tw = std::max(1, int(iw * sc + 0.5f));
                    int th = std::max(1, int(ih * sc + 0.5f));
                    int advance_i = 0, lsb = 0;
                    stbtt_GetCodepointHMetrics(info.get(), int(cp),
                                               &advance_i, &lsb);
                    g.advance  = advance_i * scale;
                    g.w = tw; g.h = th;
                    g.x_off = int(std::round(hit.bearingX * sc));
                    g.y_off = -int(std::round(hit.bearingY * sc));
                    g.is_color = true;
                    g.rgba.assign(size_t(tw) * size_t(th) * 4, 0);
                    for (int y = 0; y < th; ++y) {
                        int sy = int((float(y) + 0.5f) / sc);
                        if (sy < 0) sy = 0; else if (sy >= ih) sy = ih - 1;
                        for (int x = 0; x < tw; ++x) {
                            int sx = int((float(x) + 0.5f) / sc);
                            if (sx < 0) sx = 0; else if (sx >= iw) sx = iw - 1;
                            const stbi_uc* s = px + (sy * iw + sx) * 4;
                            uint8_t r = s[0], gc = s[1], b = s[2], a = s[3];
                            // Premultiply so the canvas can blit source-over
                            // directly without a per-pixel multiply.
                            uint8_t* d = g.rgba.data() + (y * tw + x) * 4;
                            d[0] = uint8_t((int(r)  * a + 127) / 255);
                            d[1] = uint8_t((int(gc) * a + 127) / 255);
                            d[2] = uint8_t((int(b)  * a + 127) / 255);
                            d[3] = a;
                        }
                    }
                    stbi_image_free(px);
                    auto [ins, _] = cache.emplace(cp, std::move(g));
                    return &ins->second;
                }
            }
        }
    }

    int advance, lsb;
    stbtt_GetCodepointHMetrics(info.get(), int(cp), &advance, &lsb);
    g.advance = advance * scale;

    // Rasterize monochrome glyphs at higher resolution and downsample in the
    // canvas. This keeps small text smoother without changing layout metrics.
    constexpr float kGlyphOversample = 2.0f;
    float render_scale = scale * kGlyphOversample;
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBoxSubpixel(
        info.get(), int(cp), render_scale, render_scale,
        0.0f, 0.0f, &x0, &y0, &x1, &y1);
    g.w = x1 - x0;
    g.h = y1 - y0;
    g.x_off = x0;
    g.y_off = y0;
    g.bitmap_scale = kGlyphOversample;

    if (g.w > 0 && g.h > 0) {
        g.alpha.assign(size_t(g.w) * size_t(g.h), 0);
        stbtt_MakeCodepointBitmapSubpixel(
            info.get(), g.alpha.data(),
            g.w, g.h, g.w,
            render_scale, render_scale,
            0.0f, 0.0f,
            int(cp));
    }
    auto [ins, _] = cache.emplace(cp, std::move(g));
    return &ins->second;
}

// ---- Font public API ------------------------------------------------------
Font::Font() = default;
Font::~Font() = default;
Font::Font(Font&&) noexcept = default;
Font& Font::operator=(Font&&) noexcept = default;

Font Font::from_memory(std::vector<uint8_t> bytes, float pixel_size,
                       int face_index) {
    Font f;
    // Reject anything smaller than an SFNT offset table before handing the
    // buffer to stb_truetype — it dereferences the data pointer without a
    // length check, so an empty vector (data() == nullptr) would crash.
    if (bytes.size() < 12) return f;
    // For TrueType collections, stbtt_GetFontOffsetForIndex reads the TTC
    // header entry for `face_index` without a bounds check — verify it fits.
    if (be32(bytes.data()) == 0x74746366u /*ttcf*/) {
        uint64_t need = 12u + 4u * (uint64_t(face_index) + 1);
        if (face_index < 0 || need > bytes.size()) return f;
        uint32_t n_fonts = be32(bytes.data() + 8);
        if (uint32_t(face_index) >= n_fonts) return f;
    }

    auto p     = std::make_unique<Impl>();
    p->ttf     = std::move(bytes);
    p->info    = std::make_unique<stbtt_fontinfo>();

    int offset = stbtt_GetFontOffsetForIndex(p->ttf.data(), face_index);
    if (offset < 0) return f;
    if (!sfnt_validate(p->ttf.data(), p->ttf.size(), uint32_t(offset)))
        return f;
    if (!stbtt_InitFont(p->info.get(), p->ttf.data(), offset)) return f;

    p->fontstart = uint32_t(offset);
    p->pixel_size = pixel_size;
    p->scale = stbtt_ScaleForPixelHeight(p->info.get(), pixel_size);

    stbtt_GetFontVMetrics(p->info.get(), &p->ascent, &p->descent, &p->line_gap);

    // Detect color-bitmap tables (Noto Color Emoji et al). Absent tables
    // just leave the offsets at 0, in which case glyph() falls straight
    // through to outline rasterization.
    find_sfnt_table(p->ttf.data(), p->ttf.size(), p->fontstart, kTagCBDT,
                    p->cbdt_off, p->cbdt_len);
    find_sfnt_table(p->ttf.data(), p->ttf.size(), p->fontstart, kTagCBLC,
                    p->cblc_off, p->cblc_len);

    f.faces_.push_back(std::move(p));
    return f;
}

Font Font::from_file(std::string_view path, float pixel_size, int face_index) {
    std::string s(path);
    std::ifstream ifs(s, std::ios::binary);
    if (!ifs) {
        std::fprintf(stderr, "font: cannot open %s\n", s.c_str());
        return Font();
    }
    std::vector<uint8_t> bytes(
        (std::istreambuf_iterator<char>(ifs)),
        std::istreambuf_iterator<char>());
    return from_memory(std::move(bytes), pixel_size, face_index);
}

void Font::add_fallback(Font fallback) {
    for (auto& face : fallback.faces_) faces_.push_back(std::move(face));
}

bool Font::has_glyph(uint32_t cp) const {
    for (auto& f : faces_) if (f->has_glyph(cp)) return true;
    return false;
}

Font::Resolved Font::resolve(uint32_t cp) const {
    // Prefer faces that have the codepoint; fall back to primary if none.
    for (auto& f : faces_) {
        if (f->has_glyph(cp)) return { f.get(), f->glyph(cp) };
    }
    if (!faces_.empty()) return { faces_[0].get(), faces_[0]->glyph(cp) };
    return { nullptr, nullptr };
}

bool  Font::valid()        const { return !faces_.empty() && faces_[0]->info != nullptr; }
float Font::pixel_size()   const { return valid() ? faces_[0]->pixel_size : 0; }
float Font::ascent()       const { return valid() ?  faces_[0]->ascent   * faces_[0]->scale : 0; }
float Font::descent()      const { return valid() ? -faces_[0]->descent  * faces_[0]->scale : 0; }
float Font::line_gap()     const { return valid() ?  faces_[0]->line_gap * faces_[0]->scale : 0; }

float Font::measure(std::string_view s) const {
    if (!valid()) return 0;
    const char* p   = s.data();
    const char* end = p + s.size();
    float x = 0;
    const Impl* prev_face = nullptr;
    uint32_t prev_cp = 0;
    while (p < end) {
        uint32_t cp = utf8_next(p, end);
        if (!cp) break;
        auto r = resolve(cp);
        if (!r.glyph) continue;
        x += r.glyph->advance;
        if (prev_cp && prev_face == r.face) {
            int k = stbtt_GetCodepointKernAdvance(r.face->info.get(),
                                                  int(prev_cp), int(cp));
            x += k * r.face->scale;
        }
        prev_cp = cp; prev_face = r.face;
    }
    return x;
}

// ---------------------------------------------------------------------------
// Line breaking
// ---------------------------------------------------------------------------
// CJK Unicode ranges where breaks may occur between any two codepoints.
static bool is_cjk(uint32_t cp) {
    return (cp >= 0x3000  && cp <= 0x30FF) ||   // Japanese kana + CJK symbols
           (cp >= 0x3400  && cp <= 0x4DBF) ||   // CJK Ext A
           (cp >= 0x4E00  && cp <= 0x9FFF) ||   // CJK Unified
           (cp >= 0xFF00  && cp <= 0xFFEF) ||   // Halfwidth/Fullwidth
           (cp >= 0xAC00  && cp <= 0xD7AF) ||   // Hangul
           (cp >= 0x20000 && cp <= 0x2FFFF);    // CJK Ext B+
}

static bool is_space(uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == 0x3000 /* ideographic space */;
}

// Basic kinsoku — characters not allowed at line start (line-start forbidden).
// Includes common Japanese closing brackets, sentence enders, small kana.
static bool kinsoku_no_line_start(uint32_t cp) {
    switch (cp) {
    case ')': case ']': case '}': case '!': case '?':
    case ',': case '.': case ':': case ';':
    case 0x3001: /* 、 */ case 0x3002: /* 。 */
    case 0xFF09: /* ） */ case 0xFF3D: /* ］ */ case 0xFF5D: /* ｝ */
    case 0x300D: /* 」 */ case 0x300F: /* 』 */
    case 0x3015: /* 〕 */ case 0x3017: /* 〗 */ case 0x3019: /* 〙 */
    case 0xFF01: /* ！ */ case 0xFF1F: /* ？ */
    case 0xFF0C: /* ， */ case 0xFF0E: /* ． */ case 0xFF1A: /* ： */ case 0xFF1B: /* ； */
    case 0x30FC: /* ー */ case 0x301C: /* 〜 */
    case 0x3041: case 0x3043: case 0x3045: case 0x3047: case 0x3049: // ぁぃぅぇぉ
    case 0x3083: case 0x3085: case 0x3087: case 0x3063: // ゃゅょっ
    case 0x30A1: case 0x30A3: case 0x30A5: case 0x30A7: case 0x30A9: // ァィゥェォ
    case 0x30E3: case 0x30E5: case 0x30E7: case 0x30C3: // ャュョッ
        return true;
    }
    return false;
}

// Forbidden at line end (line-end forbidden).
static bool kinsoku_no_line_end(uint32_t cp) {
    switch (cp) {
    case '(': case '[': case '{':
    case 0xFF08: /* （ */ case 0xFF3B: /* ［ */ case 0xFF5B: /* ｛ */
    case 0x300C: /* 「 */ case 0x300E: /* 『 */
    case 0x3014: /* 〔 */ case 0x3016: /* 〖 */ case 0x3018: /* 〘 */
        return true;
    }
    return false;
}

std::vector<Font::Line> Font::wrap(std::string_view utf8, float max_width) const {
    std::vector<Line> out;
    if (!valid() || utf8.empty()) return out;

    const char* base = utf8.data();
    const char* end  = base + utf8.size();
    const char* p    = base;

    // Per-line scan state.
    size_t  line_start    = 0;
    float   line_width    = 0;
    size_t  last_break    = SIZE_MAX;  // byte offset of last good break point
    float   width_at_break = 0;
    const Impl* prev_face = nullptr;
    uint32_t prev_cp = 0;

    auto flush_line = [&](size_t end_off) {
        Line ln{ line_start, end_off, 0 };
        // Recompute width from start to end_off (excluding trailing spaces).
        const char* q = base + line_start;
        const char* qe = base + end_off;
        float w = 0;
        const Impl* pf = nullptr;
        uint32_t pc = 0;
        while (q < qe) {
            uint32_t cp = utf8_next(q, qe);
            if (!cp) break;
            auto r = resolve(cp);
            if (!r.glyph) continue;
            w += r.glyph->advance;
            if (pc && pf == r.face) {
                int k = stbtt_GetCodepointKernAdvance(r.face->info.get(),
                                                      int(pc), int(cp));
                w += k * r.face->scale;
            }
            pc = cp; pf = r.face;
        }
        ln.width = w;
        out.push_back(ln);
    };

    while (p < end) {
        const char* cp_start = p;
        uint32_t cp = utf8_next(p, end);
        if (!cp) break;
        size_t cp_offset = size_t(cp_start - base);
        size_t next_offset = size_t(p - base);

        // Hard newline.
        if (cp == '\n') {
            flush_line(cp_offset);
            line_start = next_offset;
            line_width = 0;
            last_break = SIZE_MAX;
            prev_cp = 0; prev_face = nullptr;
            continue;
        }

        auto r = resolve(cp);
        float glyph_w = r.glyph ? r.glyph->advance : 0;
        if (prev_cp && r.face && prev_face == r.face) {
            int k = stbtt_GetCodepointKernAdvance(r.face->info.get(),
                                                  int(prev_cp), int(cp));
            glyph_w += k * r.face->scale;
        }

        // Test fit. If overflowing the line width and we have a break point,
        // commit a line up to the last break and restart from there.
        if (line_width + glyph_w > max_width && cp_offset > line_start) {
            if (last_break != SIZE_MAX && last_break > line_start) {
                flush_line(last_break);
                // Skip leading whitespace after the break.
                const char* q = base + last_break;
                while (q < end) {
                    const char* qstart = q;
                    uint32_t qcp = utf8_next(q, end);
                    if (!qcp || !is_space(qcp)) { q = qstart; break; }
                }
                line_start = size_t(q - base);
                // Re-scan from line_start.
                p = base + line_start;
                line_width = 0;
                last_break = SIZE_MAX;
                prev_cp = 0; prev_face = nullptr;
                continue;
            } else {
                // No break point — hard-break before this codepoint.
                flush_line(cp_offset);
                line_start = cp_offset;
                line_width = 0;
                last_break = SIZE_MAX;
                prev_cp = 0; prev_face = nullptr;
                // re-add this codepoint width below
            }
        }

        line_width += glyph_w;
        prev_cp = cp; prev_face = r.face;

        // After processing this cp, decide whether `next_offset` is a valid
        // break candidate.
        // Latin: break after whitespace.
        // CJK: break between any two CJK characters, subject to kinsoku.
        bool can_break_here = false;
        if (is_space(cp)) {
            can_break_here = true;
        } else if (p < end) {
            const char* peek = p;
            uint32_t next_cp = utf8_next(peek, end);
            if (next_cp) {
                bool cur_cjk  = is_cjk(cp);
                bool next_cjk = is_cjk(next_cp);
                if ((cur_cjk || next_cjk) &&
                    !kinsoku_no_line_end(cp) &&
                    !kinsoku_no_line_start(next_cp)) {
                    can_break_here = true;
                }
            }
        }
        if (can_break_here) {
            last_break = next_offset;
            width_at_break = line_width;
            (void)width_at_break;
        }
    }

    if (line_start < utf8.size()) flush_line(utf8.size());
    return out;
}

} // namespace stilus
