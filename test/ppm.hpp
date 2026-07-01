// test/ppm.hpp — minimal PPM (P6) read/write for golden-image tests.
//
// Deliberately dependency-free: golden images are tiny (test-canvas sized)
// so an uncompressed binary format is fine, and it avoids pulling in any
// image codec just for testing.
#pragma once
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace test {

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgb; // 3 bytes/pixel, row-major, no padding

    bool operator==(const Image& o) const {
        return width == o.width && height == o.height && rgb == o.rgb;
    }
};

// Convert a stilus XRGB8888 framebuffer (as produced by SoftCanvas::bind)
// into a tightly-packed RGB Image, dropping the unused byte.
inline Image from_xrgb(const uint32_t* px, int w, int h, int stride_px) {
    Image img;
    img.width = w; img.height = h;
    img.rgb.resize(size_t(w) * size_t(h) * 3);
    for (int y = 0; y < h; ++y) {
        const uint32_t* row = px + y * stride_px;
        uint8_t* out = img.rgb.data() + size_t(y) * size_t(w) * 3;
        for (int x = 0; x < w; ++x) {
            uint32_t v = row[x];
            out[x*3 + 0] = uint8_t((v >> 16) & 0xff); // R
            out[x*3 + 1] = uint8_t((v >>  8) & 0xff); // G
            out[x*3 + 2] = uint8_t( v        & 0xff); // B
        }
    }
    return img;
}

inline bool write_ppm(const std::string& path, const Image& img) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << "P6\n" << img.width << " " << img.height << "\n255\n";
    f.write(reinterpret_cast<const char*>(img.rgb.data()), std::streamsize(img.rgb.size()));
    return bool(f);
}

inline bool read_ppm(const std::string& path, Image& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string magic;
    f >> magic;
    if (magic != "P6") return false;
    // Skip whitespace/comments, then read width/height/maxval as whitespace-
    // separated tokens (comments starting with '#' are not expected in our
    // own generated files, so we don't bother handling them).
    int w = 0, h = 0, maxval = 0;
    f >> w >> h >> maxval;
    f.get(); // consume the single whitespace byte before pixel data
    if (w <= 0 || h <= 0 || maxval != 255) return false;
    out.width = w; out.height = h;
    out.rgb.resize(size_t(w) * size_t(h) * 3);
    f.read(reinterpret_cast<char*>(out.rgb.data()), std::streamsize(out.rgb.size()));
    return bool(f);
}

// Returns the number of pixels differing by more than `tol` in any channel.
// Returns -1 if dimensions mismatch (caller should treat that as a hard fail).
inline int64_t diff_count(const Image& a, const Image& b, int tol = 0) {
    if (a.width != b.width || a.height != b.height) return -1;
    int64_t n = 0;
    for (size_t i = 0; i < a.rgb.size(); ++i) {
        int d = int(a.rgb[i]) - int(b.rgb[i]);
        if (d < 0) d = -d;
        if (d > tol) ++n;
    }
    return n;
}

} // namespace test
