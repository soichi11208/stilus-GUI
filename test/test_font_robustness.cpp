// test/test_font_robustness.cpp — malformed-font resilience.
//
// Font files come from disk and must be treated as untrusted input.
// stb_truetype does no bounds checking by itself, so Font::from_memory
// pre-validates the SFNT structure; these tests pin that behavior by
// feeding truncated and bit-flipped variants of real system fonts through
// the full load/resolve/measure/wrap pipeline. Run under ASan in CI for
// full effect, but even without it a crash fails the test run.
#include "test_framework.hpp"

#include <cstdio>
#include <fstream>
#include <random>
#include <vector>

#include "stilus/font.hpp"

using namespace stilus;

namespace {

std::vector<uint8_t> slurp(const char* p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

const char* find_test_font() {
    static const char* candidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    };
    for (auto* p : candidates) if (!slurp(p).empty()) return p;
    return nullptr;
}

// Push a (possibly garbage) buffer through everything a real caller would.
void exercise(std::vector<uint8_t> bytes) {
    auto f = Font::from_memory(std::move(bytes), 24.0f);
    if (!f.valid()) return;
    for (uint32_t cp : {0x41u, 0x3042u, 0x4E00u, 0x1F600u}) {
        f.has_glyph(cp);
        f.resolve(cp);
    }
    f.measure("Hello \xe4\xb8\x96\xe7\x95\x8c");
    f.wrap("Hello wrap wrap wrap", 60.0f);
}

} // namespace

TEST(font_rejects_empty_and_tiny_buffers) {
    exercise({});
    exercise({0x00});
    exercise({0x00, 0x01, 0x00, 0x00});
    if (Font::from_memory({}, 16.f).valid())
        throw std::runtime_error("empty buffer must not produce a valid font");
}

TEST(font_survives_truncation) {
    const char* path = find_test_font();
    if (!path) { std::printf("  [SKIPPED: no test font found] "); return; }
    auto base = slurp(path);
    for (size_t len : {size_t(4), size_t(11), size_t(12), size_t(64),
                       size_t(100), base.size()/8, base.size()/2,
                       base.size()-1}) {
        exercise({base.begin(), base.begin() + std::min(len, base.size())});
    }
}

TEST(font_survives_bit_flips) {
    const char* path = find_test_font();
    if (!path) { std::printf("  [SKIPPED: no test font found] "); return; }
    auto base = slurp(path);
    std::mt19937 rng(0xC0FFEE);
    for (int m = 0; m < 64; ++m) {
        auto mut = base;
        for (int k = 0; k < 8; ++k)
            mut[rng() % mut.size()] ^= uint8_t(1u << (rng() % 8));
        exercise(std::move(mut));
    }
}

TEST(font_ttc_face_index_out_of_range) {
    // A fake TTC header claiming 2 fonts but only bytes for the header —
    // any face index must be rejected without reading past the buffer.
    std::vector<uint8_t> ttc = {
        't','t','c','f',  0,1,0,0,  0,0,0,2,   // ttcf, ver 1.0, numFonts=2
        0,0,0,32,                              // offset[0] (past end)
    };
    if (Font::from_memory(ttc, 16.f).valid())
        throw std::runtime_error("bogus ttc must not validate");
    auto f2 = Font::from_memory(ttc, 16.f, 1);   // offset[1] not even present
    if (f2.valid())
        throw std::runtime_error("out-of-range ttc face must not validate");
}

int main(int argc, char** argv) {
    std::string filter = argc > 1 ? argv[1] : "";
    return test::TestRunner::instance().run(filter);
}
