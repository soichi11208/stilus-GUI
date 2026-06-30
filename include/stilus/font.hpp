// stilus/font.hpp - font loading, metrics, layout
#pragma once
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "geom.hpp"

namespace stilus {

class Font {
public:
    // Load a TTF/OTF (or one face out of a TTC/OTC) from memory or file.
    // `face_index` picks which face inside a TrueType collection; pass 0 for
    // a plain TTF/OTF.
    static Font from_memory(std::vector<uint8_t> bytes, float pixel_size,
                            int face_index = 0);
    static Font from_file  (std::string_view path,       float pixel_size,
                            int face_index = 0);

    Font();
    ~Font();
    Font(Font&&) noexcept;
    Font& operator=(Font&&) noexcept;
    Font(const Font&)            = delete;
    Font& operator=(const Font&) = delete;

    bool   valid() const;
    float  pixel_size()     const;
    float  ascent()         const;   // positive, pixels
    float  descent()        const;   // positive, pixels
    float  line_gap()       const;
    float  line_height()    const { return ascent() + descent() + line_gap(); }

    // Append a fallback face. When a codepoint is missing from this font (and
    // any earlier fallback), the canvas will pull the glyph from the first
    // fallback that has it. Cross-face kerning is intentionally suppressed.
    // The fallback's pixel_size is independent — it's typically a good idea
    // to load CJK fallbacks at the same pixel size as the primary.
    void add_fallback(Font fallback);

    // True if the primary face or any fallback contains a glyph for `cp`.
    bool has_glyph(uint32_t cp) const;

    // Width of a UTF-8 string in pixels, using fallbacks as needed.
    float  measure(std::string_view utf8) const;

    // Soft-wrap `utf8` to `max_width`. Returns line ranges as byte offsets
    // into the input. Whitespace breaks for Latin, per-character breaks for
    // CJK with basic line-start/line-end forbiddens (kinsoku shori).
    struct Line {
        size_t start;   // byte offset (inclusive)
        size_t end;     // byte offset (exclusive)
        float  width;   // pixels
    };
    std::vector<Line> wrap(std::string_view utf8, float max_width) const;

    // Internal — opaque handles. The software canvas uses these.
    struct Impl;
    Impl*       impl()       { return faces_.empty() ? nullptr : faces_[0].get(); }
    const Impl* impl() const { return faces_.empty() ? nullptr : faces_[0].get(); }

    // Resolve `cp` against the face chain. Returns the face that owns the
    // glyph and a pointer to its cached bitmap (advance/metrics live there).
    // Returns {nullptr, nullptr} if no face has it.
    struct Resolved { const Impl* face; const struct GlyphBitmap* glyph; };
    Resolved resolve(uint32_t cp) const;

private:
    // Primary at faces_[0], fallbacks at faces_[1..]. Order matters.
    std::vector<std::unique_ptr<Impl>> faces_;
};

} // namespace stilus
