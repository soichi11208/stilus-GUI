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

namespace stilus {

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
    int advance, lsb;
    stbtt_GetCodepointHMetrics(info.get(), int(cp), &advance, &lsb);
    g.advance = advance * scale;

    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(info.get(), int(cp), scale, scale,
                                &x0, &y0, &x1, &y1);
    g.w = x1 - x0; g.h = y1 - y0;
    g.x_off = x0;
    g.y_off = y0;

    if (g.w > 0 && g.h > 0) {
        g.alpha.assign(size_t(g.w) * size_t(g.h), 0);
        stbtt_MakeCodepointBitmap(info.get(), g.alpha.data(),
                                  g.w, g.h, g.w, scale, scale, int(cp));
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
    auto p     = std::make_unique<Impl>();
    p->ttf     = std::move(bytes);
    p->info    = std::make_unique<stbtt_fontinfo>();

    int offset = stbtt_GetFontOffsetForIndex(p->ttf.data(), face_index);
    if (offset < 0) return f;
    if (!stbtt_InitFont(p->info.get(), p->ttf.data(), offset)) return f;

    p->pixel_size = pixel_size;
    p->scale = stbtt_ScaleForPixelHeight(p->info.get(), pixel_size);

    stbtt_GetFontVMetrics(p->info.get(), &p->ascent, &p->descent, &p->line_gap);

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
