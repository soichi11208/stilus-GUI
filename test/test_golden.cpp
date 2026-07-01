// test/test_golden.cpp — golden-image regression tests for SoftCanvas.
//
// Renders deterministic, font-free primitives into a small offscreen buffer
// and compares against a checked-in reference PPM under test/golden/. Any
// pixel-level regression in the rasterizer trips these.
//
// Run with --regen to (re)write the reference images from the current
// renderer output — use this deliberately, after visually confirming the
// new output is correct (e.g. by opening the written PPM).
#include "test_framework.hpp"
#include "ppm.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "stilus/geom.hpp"
#include "stilus/path.hpp"
#include "../src/render/soft_canvas.hpp"

using namespace stilus;

namespace {

bool g_regen = false;
const char* kGoldenDir = STILUS_TEST_GOLDEN_DIR; // set by CMake, see below

struct Canvas64 {
    static constexpr int W = 64, H = 64;
    std::vector<uint32_t> buf;
    render::SoftCanvas canvas;

    Canvas64() : buf(size_t(W) * size_t(H), 0) {
        canvas.bind(buf.data(), W, H, W);
    }
};

void check_golden(const char* name, const render::SoftCanvas& c,
                  const std::vector<uint32_t>& buf, int w, int h) {
    test::Image actual = test::from_xrgb(buf.data(), w, h, w);
    std::string path = std::string(kGoldenDir) + "/" + name + ".ppm";

    if (g_regen) {
        if (!test::write_ppm(path, actual))
            throw std::runtime_error("failed to write golden: " + path);
        std::printf("  [REGENERATED %s] ", path.c_str());
        return;
    }

    test::Image expected;
    if (!test::read_ppm(path, expected)) {
        throw std::runtime_error(
            "missing golden reference: " + path +
            " (run test_golden --regen to create it, then verify visually)");
    }
    int64_t diffs = test::diff_count(expected, actual, /*tol=*/1);
    if (diffs != 0) {
        throw std::runtime_error(
            name + std::string(": ") + std::to_string(diffs) +
            " pixel(s) differ from golden (or size mismatch if -1)");
    }
    (void)c;
}

} // namespace

// ---------------------------------------------------------------------------
TEST(golden_fill_rect) {
    Canvas64 cv;
    cv.canvas.clear(Color::rgb(0x101010));
    cv.canvas.fill_rect({8, 8, 32, 24}, Color::rgb(0xff8040));
    check_golden("fill_rect", cv.canvas, cv.buf, Canvas64::W, Canvas64::H);
}

TEST(golden_fill_rounded_rect) {
    Canvas64 cv;
    cv.canvas.clear(Color::rgb(0x101010));
    cv.canvas.fill_rounded_rect({6, 6, 40, 30}, 10.0f, Color::rgb(0x40a0ff));
    check_golden("fill_rounded_rect", cv.canvas, cv.buf, Canvas64::W, Canvas64::H);
}

TEST(golden_fill_circle) {
    Canvas64 cv;
    cv.canvas.clear(Color::rgb(0x101010));
    cv.canvas.fill_circle({32, 32}, 20.0f, Color::rgb(0x60d060));
    check_golden("fill_circle", cv.canvas, cv.buf, Canvas64::W, Canvas64::H);
}

TEST(golden_stroke_rect) {
    Canvas64 cv;
    cv.canvas.clear(Color::rgb(0x101010));
    cv.canvas.stroke_rect({10, 10, 40, 30}, 3.0f, Color::rgb(0xffffff));
    check_golden("stroke_rect", cv.canvas, cv.buf, Canvas64::W, Canvas64::H);
}

TEST(golden_affine_rotated_rect) {
    Canvas64 cv;
    cv.canvas.clear(Color::rgb(0x101010));
    cv.canvas.push_transform(Affine::translate(32, 32));
    cv.canvas.push_transform(Affine::rotate(0.4636476090f)); // ~26.57deg (3-4-5)
    cv.canvas.fill_rect({-15, -10, 30, 20}, Color::rgb(0xd06060));
    cv.canvas.pop_transform();
    cv.canvas.pop_transform();
    check_golden("affine_rotated_rect", cv.canvas, cv.buf, Canvas64::W, Canvas64::H);
}

TEST(golden_path_clip_circle) {
    Canvas64 cv;
    cv.canvas.clear(Color::rgb(0x101010));
    Path mask;
    mask.add_circle({32, 32}, 18.0f);
    cv.canvas.push_clip_path(mask);
    for (int i = 0; i < 6; ++i) {
        cv.canvas.fill_rect({float(i * 10), 0, 8, 64}, Color::rgb(0xc04040));
    }
    cv.canvas.pop_clip();
    check_golden("path_clip_circle", cv.canvas, cv.buf, Canvas64::W, Canvas64::H);
}

TEST(golden_nested_clip_rects) {
    Canvas64 cv;
    cv.canvas.clear(Color::rgb(0x101010));
    cv.canvas.push_clip({4, 4, 56, 56});
    cv.canvas.push_clip({20, 0, 30, 64}); // intersects to a narrower strip
    cv.canvas.fill_rect({0, 0, 64, 64}, Color::rgb(0x8040c0));
    cv.canvas.pop_clip();
    cv.canvas.pop_clip();
    check_golden("nested_clip_rects", cv.canvas, cv.buf, Canvas64::W, Canvas64::H);
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    std::string filter;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--regen") g_regen = true;
        else filter = a;
    }
    return test::TestRunner::instance().run(filter);
}
