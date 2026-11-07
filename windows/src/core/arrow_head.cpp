#include "arrow_head.h"

#include <cmath>

namespace lz {

std::optional<std::pair<Point, Point>> ArrowHead::barbs(const std::vector<Point>& points, Real length,
                                                        Real angle) {
    if (points.size() < 2) return std::nullopt;
    const Point tip = points.back();
    // Walk back from the tip until the stroke has covered about the head's length.
    Point back = points[points.size() - 2];
    for (int i = static_cast<int>(points.size()) - 2; i >= 0; --i) {
        back = points[static_cast<size_t>(i)];
        if (hypot(tip, back) >= length) break;
    }
    const Real dx = tip.x - back.x;
    const Real dy = tip.y - back.y;
    const Real distance = std::hypot(dx, dy);
    if (distance < length * 0.25) return std::nullopt;
    const Real heading = std::atan2(dy, dx);
    auto barb = [&](Real side) {
        const Real a = heading + 3.14159265358979323846 + side * angle;
        return Point{tip.x + std::cos(a) * length, tip.y + std::sin(a) * length};
    };
    return std::make_pair(barb(1), barb(-1));
}

}  // namespace lz
