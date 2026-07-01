// test/test_unit.cpp — headless unit tests for window-independent logic.
//
// These tests never open a window/connection, so they run anywhere
// (including CI without a display). Golden-image / X11 integration tests
// live in separate executables.
#include "test_framework.hpp"

#include <cmath>
#include <cstdio>

#include "stilus/geom.hpp"
#include "stilus/path.hpp"
#include "stilus/font.hpp"
#include "../src/damage.hpp"
#include "../src/render/path_raster.hpp"

using namespace stilus;

namespace {

bool approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
bool approx(Vec2 a, Vec2 b, float eps = 1e-3f) { return approx(a.x, b.x, eps) && approx(a.y, b.y, eps); }

} // namespace

// ---------------------------------------------------------------------------
// Affine
// ---------------------------------------------------------------------------
TEST(affine_identity_apply) {
    Affine id = Affine::identity();
    Vec2 p = id.apply({3, 4});
    if (!approx(p, {3, 4})) throw std::runtime_error("identity should be a no-op");
}

TEST(affine_translate) {
    Affine t = Affine::translate(10, -5);
    Vec2 p = t.apply({1, 1});
    if (!approx(p, {11, -4})) throw std::runtime_error("translate wrong");
}

TEST(affine_scale) {
    Affine s = Affine::scale(2, 3);
    Vec2 p = s.apply({1, 1});
    if (!approx(p, {2, 3})) throw std::runtime_error("scale wrong");
}

TEST(affine_rotate_90deg) {
    Affine r = Affine::rotate(float(M_PI) / 2.0f);
    Vec2 p = r.apply({1, 0});
    // 90deg CCW-in-math-coords (y-down screen space): (1,0) -> (0,1)
    if (!approx(p, {0, 1}, 1e-3f)) throw std::runtime_error("rotate wrong");
}

TEST(affine_compose_order) {
    // (translate * scale).apply(p) == translate.apply(scale.apply(p))
    Affine t = Affine::translate(5, 5);
    Affine s = Affine::scale(2, 2);
    Affine combined = t * s;
    Vec2 a = combined.apply({1, 1});
    Vec2 b = t.apply(s.apply({1, 1}));
    if (!approx(a, b)) throw std::runtime_error("composition order mismatch");
    if (!approx(a, {7, 7})) throw std::runtime_error("expected (7,7)");
}

TEST(affine_inverse_roundtrip) {
    Affine m = Affine::translate(3, -2) * Affine::rotate(0.4f) * Affine::scale(1.5f, 0.7f);
    Affine inv = m.inverse();
    Vec2 p{12.3f, -4.5f};
    Vec2 roundtrip = inv.apply(m.apply(p));
    if (!approx(roundtrip, p, 1e-2f)) throw std::runtime_error("inverse roundtrip failed");
}

TEST(affine_is_axis_aligned) {
    if (!Affine::identity().is_axis_aligned()) throw std::runtime_error("identity should be axis-aligned");
    if (!Affine::translate(3, 4).is_axis_aligned()) throw std::runtime_error("translate should be axis-aligned");
    if (!Affine::scale(2, 3).is_axis_aligned()) throw std::runtime_error("scale should be axis-aligned");
    if (Affine::rotate(0.1f).is_axis_aligned()) throw std::runtime_error("rotation should not be axis-aligned");
}

TEST(affine_is_translation) {
    if (!Affine::translate(1, 2).is_translation()) throw std::runtime_error("pure translate should qualify");
    if (Affine::scale(2, 1).is_translation()) throw std::runtime_error("non-unit scale should not qualify");
}

// ---------------------------------------------------------------------------
// Path flattening
// ---------------------------------------------------------------------------
TEST(path_add_rect_flattens_to_4_corners) {
    Path p;
    p.add_rect({10, 20, 30, 40});
    render::FlatPath flat;
    render::flatten(p, 0.25f, flat);
    if (flat.starts.size() != 2) throw std::runtime_error("expected a single contour");
    // Move + 3 line_to + Close (which re-appends the start point) => 5 points,
    // the last one coinciding with the first.
    if (flat.pts.size() != 5) throw std::runtime_error("closed rect should flatten to 5 points (start repeated at close)");
    if (!approx(flat.pts.front(), flat.pts.back(), 1e-3f))
        throw std::runtime_error("closed contour should end where it started");
    Rect expect{10, 20, 30, 40};
    bool saw[4] = {false,false,false,false};
    Vec2 corners[4] = {
        {expect.x, expect.y}, {expect.x+expect.w, expect.y},
        {expect.x+expect.w, expect.y+expect.h}, {expect.x, expect.y+expect.h}
    };
    for (auto& pt : flat.pts) {
        for (int i = 0; i < 4; ++i) if (approx(pt, corners[i], 1e-3f)) saw[i] = true;
    }
    for (int i = 0; i < 4; ++i) if (!saw[i]) throw std::runtime_error("missing expected corner");
}

TEST(path_add_circle_bbox) {
    Path p;
    p.add_circle({0, 0}, 10.0f);
    render::FlatPath flat;
    render::flatten(p, 0.1f, flat);
    if (flat.pts.empty()) throw std::runtime_error("circle should flatten to points");
    float xmin=1e30f, xmax=-1e30f, ymin=1e30f, ymax=-1e30f;
    for (auto& pt : flat.pts) {
        xmin = std::min(xmin, pt.x); xmax = std::max(xmax, pt.x);
        ymin = std::min(ymin, pt.y); ymax = std::max(ymax, pt.y);
    }
    // Flattened circle bbox should closely match the analytic bbox
    // (tolerance-limited chordal error keeps it just inside).
    if (!approx(xmax - xmin, 20.0f, 0.1f)) throw std::runtime_error("circle width off");
    if (!approx(ymax - ymin, 20.0f, 0.1f)) throw std::runtime_error("circle height off");
}

TEST(path_rounded_rect_degenerates_to_rect_at_zero_radius) {
    Path p;
    p.add_rounded_rect({0, 0, 50, 50}, 0.0f);
    render::FlatPath flat;
    render::flatten(p, 0.25f, flat);
    if (flat.pts.size() != 5) throw std::runtime_error("zero-radius rounded rect should flatten like a closed rect (5 points)");
}

// ---------------------------------------------------------------------------
// DamageRegion
// ---------------------------------------------------------------------------
TEST(damage_starts_empty) {
    detail::DamageRegion d;
    d.set_surface(100, 100);
    if (!d.empty()) throw std::runtime_error("fresh region should be empty");
    if (d.is_full()) throw std::runtime_error("fresh region should not be full");
}

TEST(damage_add_small_rect_stays_partial) {
    detail::DamageRegion d;
    d.set_surface(1000, 1000);
    d.add({10, 10, 20, 20});
    if (d.is_full()) throw std::runtime_error("small rect should not promote to full");
    if (d.rects().size() != 1) throw std::runtime_error("expected exactly one rect");
}

TEST(damage_large_rect_promotes_to_full) {
    detail::DamageRegion d;
    d.set_surface(100, 100);
    d.add({0, 0, 90, 90}); // 81% of surface > kFullThreshold(0.6)
    if (!d.is_full()) throw std::runtime_error("large rect should promote to full");
}

TEST(damage_coalesces_overlapping_rects) {
    detail::DamageRegion d;
    d.set_surface(1000, 1000);
    d.add({0, 0, 10, 10});
    d.add({5, 5, 10, 10}); // overlaps the first
    if (d.rects().size() != 1) throw std::runtime_error("overlapping rects should coalesce");
    Rect b = d.bounds();
    if (!approx(b.x, 0) || !approx(b.y, 0) || !approx(b.w, 15) || !approx(b.h, 15))
        throw std::runtime_error("coalesced bounds wrong");
}

TEST(damage_collapses_beyond_max_rects) {
    detail::DamageRegion d;
    d.set_surface(10000, 10000);
    // Scatter far-apart tiny rects so none coalesce, forcing the >kMaxRects
    // collapse-to-bbox path.
    for (int i = 0; i < 20; ++i) {
        d.add({float(i * 100), float(i * 100), 1, 1});
    }
    if (d.is_full()) throw std::runtime_error("scattered tiny rects should not hit the full threshold");
    if (d.rects().size() > detail::DamageRegion::kMaxRects)
        throw std::runtime_error("should have collapsed to <= kMaxRects");
}

TEST(damage_clear_resets_state) {
    detail::DamageRegion d;
    d.set_surface(100, 100);
    d.mark_full();
    d.clear();
    if (!d.empty()) throw std::runtime_error("clear() should reset to empty");
}

// ---------------------------------------------------------------------------
// Font::wrap — only runs if a system font is available (keeps this test
// binary portable to environments without fonts installed).
// ---------------------------------------------------------------------------
namespace {
Font load_test_font(float px) {
    static const char* candidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    };
    for (auto* path : candidates) {
        Font f = Font::from_file(path, px);
        if (f.valid()) return f;
    }
    return Font();
}
Font load_cjk_fallback(float px) {
    static const char* candidates[] = {
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
    };
    for (auto* path : candidates) {
        Font f = Font::from_file(path, px, 0);
        if (f.valid()) return f;
    }
    return Font();
}
}

TEST(font_wrap_breaks_on_whitespace) {
    Font f = load_test_font(16.0f);
    if (!f.valid()) { std::printf("  [SKIPPED: no test font found] "); return; }
    // A width tight enough to force at least one break, but wide enough that
    // no single word overflows on its own.
    float word_w = f.measure("elephant");
    auto lines = f.wrap("elephant elephant elephant", word_w * 1.5f);
    if (lines.size() < 2) throw std::runtime_error("expected wrapping to produce multiple lines");
    for (auto& ln : lines) {
        if (ln.width > word_w * 1.5f + 1.0f)
            throw std::runtime_error("line exceeds max_width");
    }
}

TEST(font_wrap_hard_newline) {
    Font f = load_test_font(16.0f);
    if (!f.valid()) { std::printf("  [SKIPPED: no test font found] "); return; }
    auto lines = f.wrap("a\nb\nc", 10000.0f);
    if (lines.size() != 3) throw std::runtime_error("expected 3 lines from 2 hard newlines");
}

TEST(font_wrap_cjk_kinsoku_no_line_start_punct) {
    Font f = load_test_font(16.0f);
    if (!f.valid()) { std::printf("  [SKIPPED: no test font found] "); return; }
    Font cjk = load_cjk_fallback(16.0f);
    if (!cjk.valid()) { std::printf("  [SKIPPED: no CJK fallback font found] "); return; }
    f.add_fallback(std::move(cjk));

    // Construct text where a naive character-count wrap would place a closing
    // punctuation mark at a line start; verify our wrap avoids that.
    std::string text = "あいうえおかきくけこ、さしすせそ";
    // Pick a width that lands the break right around the "、".
    float full_w = f.measure(text);
    float target = full_w * 0.55f;
    auto lines = f.wrap(text, target);
    if (lines.size() < 2) throw std::runtime_error("expected the CJK text to wrap");
    for (size_t i = 1; i < lines.size(); ++i) {
        std::string_view sv(text.data() + lines[i].start, lines[i].end - lines[i].start);
        if (sv.empty()) continue;
        // Decode the first codepoint of this (non-first) line.
        const char* p = sv.data();
        uint32_t cp = 0;
        // Minimal UTF-8 decode of the first codepoint for the check.
        unsigned char c0 = (unsigned char)p[0];
        if (c0 < 0x80) cp = c0;
        else if ((c0 & 0xE0) == 0xC0 && sv.size() >= 2) cp = ((c0 & 0x1F) << 6) | (p[1] & 0x3F);
        else if ((c0 & 0xF0) == 0xE0 && sv.size() >= 3)
            cp = ((c0 & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        if (cp == 0x3001 /* 、 */)
            throw std::runtime_error("kinsoku violated: line starts with 、");
    }
}

TEST(font_fallback_resolves_missing_glyph) {
    Font f = load_test_font(16.0f);
    if (!f.valid()) { std::printf("  [SKIPPED: no test font found] "); return; }
    Font cjk = load_cjk_fallback(16.0f);
    if (!cjk.valid()) { std::printf("  [SKIPPED: no CJK fallback font found] "); return; }

    uint32_t kanji = 0x65E5; // 日
    if (f.has_glyph(kanji)) throw std::runtime_error("Latin test font unexpectedly has CJK glyph");
    f.add_fallback(std::move(cjk));
    if (!f.has_glyph(kanji)) throw std::runtime_error("fallback should provide the CJK glyph");
    auto r = f.resolve(kanji);
    if (!r.glyph) throw std::runtime_error("resolve() should return a glyph via fallback");
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    std::string filter = argc > 1 ? argv[1] : "";
    return test::TestRunner::instance().run(filter);
}
