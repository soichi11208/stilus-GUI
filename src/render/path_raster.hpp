// src/render/path_raster.hpp — scanline rasterizer (8× vertical SS + analytic H)
#pragma once
#include <vector>

#include "stilus/geom.hpp"
#include "stilus/path.hpp"

namespace stilus::render {

class SoftCanvas;

// Flatten all curves in `path` into line segments. Each contour ends either at
// a Close or when a new Move is encountered. The output is a flat list of
// points and a list of contour ranges [start, end).
struct FlatPath {
    std::vector<Vec2>     pts;
    std::vector<uint32_t> starts;  // pts-index where each contour begins
    // Implicit: contour i spans pts[starts[i] .. starts[i+1]).
    // starts.back() == pts.size().
};

void flatten(const Path& in, float tolerance, FlatPath& out);

// Rasterize `flat` into `canvas` using `color` and `rule`.
void fill_flat(SoftCanvas& canvas, const FlatPath& flat,
               Color color, FillRule rule);

} // namespace stilus::render
