// stilus/geom.hpp - basic geometric types
#pragma once
#include <cmath>
#include <cstdint>

namespace stilus {

struct Vec2 { float x = 0, y = 0;
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    float left()   const { return x; }
    float top()    const { return y; }
    float right()  const { return x + w; }
    float bottom() const { return y + h; }
    bool contains(Vec2 p) const {
        return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
    }
};

// 2x3 affine matrix:
//   [ a c e ]   [ x ]   [ a*x + c*y + e ]
//   [ b d f ] * [ y ] = [ b*x + d*y + f ]
//   [ 0 0 1 ]   [ 1 ]   [       1       ]
struct Affine {
    float a = 1, b = 0;   // column 0 (x basis)
    float c = 0, d = 1;   // column 1 (y basis)
    float e = 0, f = 0;   // translation

    static Affine identity()                  { return {}; }
    static Affine translate(float tx, float ty){ return {1,0, 0,1, tx, ty}; }
    static Affine scale(float sx, float sy)   { return {sx,0, 0,sy, 0, 0}; }
    static Affine scale(float s)              { return scale(s, s); }
    static Affine rotate(float radians) {
        float cs = std::cos(radians), sn = std::sin(radians);
        return {cs, sn, -sn, cs, 0, 0};
    }
    // Skew / shear by (kx, ky): x' = x + kx*y, y' = ky*x + y.
    static Affine skew(float kx, float ky)    { return {1, ky, kx, 1, 0, 0}; }

    // Apply this transform to a point / vector. Vec2 (no translation) variant
    // is useful for offsets and basis lengths.
    Vec2 apply (Vec2 p) const { return {a*p.x + c*p.y + e, b*p.x + d*p.y + f}; }
    Vec2 apply_vec(Vec2 v) const { return {a*v.x + c*v.y, b*v.x + d*v.y}; }

    // Compose: (lhs * rhs) applied to p == lhs.apply(rhs.apply(p)).
    Affine operator*(const Affine& r) const {
        return {
            a*r.a + c*r.b,    b*r.a + d*r.b,
            a*r.c + c*r.d,    b*r.c + d*r.d,
            a*r.e + c*r.f + e, b*r.e + d*r.f + f
        };
    }

    float determinant() const { return a*d - b*c; }

    // True if the transform is purely translation+axis-aligned-scale (no
    // rotation/shear). Many renderers take a fast path in this case.
    bool is_axis_aligned() const { return b == 0.0f && c == 0.0f; }
    // Same plus uniform unit scale: just a translation.
    bool is_translation()  const { return is_axis_aligned() && a == 1.0f && d == 1.0f; }

    Affine inverse() const {
        float det = determinant();
        if (det == 0.0f) return identity();
        float inv = 1.0f / det;
        return {
             d * inv, -b * inv,
            -c * inv,  a * inv,
            (c*f - d*e) * inv,
            (b*e - a*f) * inv
        };
    }
};

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
    static Color rgb(uint32_t hex) {
        return { ((hex >> 16) & 0xff) / 255.0f,
                 ((hex >>  8) & 0xff) / 255.0f,
                 ( hex        & 0xff) / 255.0f, 1.0f };
    }
    static Color rgba(uint32_t hex) {
        return { ((hex >> 24) & 0xff) / 255.0f,
                 ((hex >> 16) & 0xff) / 255.0f,
                 ((hex >>  8) & 0xff) / 255.0f,
                 ( hex        & 0xff) / 255.0f };
    }
};

} // namespace stilus
