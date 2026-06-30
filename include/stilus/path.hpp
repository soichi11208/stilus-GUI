// stilus/path.hpp - 2D path description
#pragma once
#include <cstdint>
#include <vector>

#include "geom.hpp"

namespace stilus {

enum class FillRule : uint8_t { NonZero, EvenOdd };

class Path {
public:
    enum class Cmd : uint8_t { Move, Line, Quad, Cubic, Close };
    struct Command {
        Cmd  cmd;
        Vec2 p[3];   // Move/Line use p[0]; Quad uses p[0..1]; Cubic uses p[0..2]
    };

    Path& move_to (Vec2 a)                    { add_(Cmd::Move,  a, {}, {}); return *this; }
    Path& line_to (Vec2 a)                    { add_(Cmd::Line,  a, {}, {}); return *this; }
    Path& quad_to (Vec2 c, Vec2 a)            { add_(Cmd::Quad,  c, a,  {}); return *this; }
    Path& cubic_to(Vec2 c1, Vec2 c2, Vec2 a)  { add_(Cmd::Cubic, c1,c2, a ); return *this; }
    Path& close_()                            { add_(Cmd::Close, {}, {}, {}); return *this; }

    // Helpers for common shapes — expressed purely in terms of the primitives.
    Path& add_rect(Rect r);
    Path& add_rounded_rect(Rect r, float radius);
    Path& add_circle(Vec2 center, float radius);

    const std::vector<Command>& commands() const { return cmds_; }
    void clear() { cmds_.clear(); }

private:
    void add_(Cmd c, Vec2 a, Vec2 b, Vec2 d) {
        cmds_.push_back({c, {a, b, d}});
    }
    std::vector<Command> cmds_;
};

} // namespace stilus
