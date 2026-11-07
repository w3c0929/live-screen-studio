#include "text_lines.h"

#include <algorithm>

namespace lz {

std::optional<Rect> TextLines::line(Point point, const std::vector<Rect>& segments) {
    // Segments the point is on or close to: pointing just below or between lines still counts, and so does
    // pointing a little past the end of a line. Of those, the nearest row wins.
    std::vector<Rect> near;
    for (const auto& r : segments) {
        if (point.y >= r.minY() - r.height * 0.8 && point.y <= r.maxY() + r.height * 0.8 &&
            horizontalGap(point, r) <= r.height * 4) {
            near.push_back(r);
        }
    }
    if (near.empty()) return std::nullopt;
    const Rect seed = *std::min_element(near.begin(), near.end(), [&](const Rect& a, const Rect& b) {
        return score(point, a) < score(point, b);
    });

    Rect line = seed;
    std::vector<Rect> rest;
    for (const auto& r : segments) {
        if (!(r == seed) &&
            verticalOverlap(r, seed) >= 0.5 * std::min(r.height, seed.height) &&
            std::min(r.height, seed.height) >= 0.7 * std::max(r.height, seed.height)) {
            rest.push_back(r);
        }
    }
    bool grew = true;
    while (grew) {
        grew = false;
        for (int index = static_cast<int>(rest.size()) - 1; index >= 0; --index) {
            const Rect r = rest[static_cast<size_t>(index)];
            const Real gap = std::max({r.minX() - line.maxX(), line.minX() - r.maxX(), 0.0});
            if (gap <= seed.height * 1.5) {
                line = line.united(r);
                rest.erase(rest.begin() + index);
                grew = true;
            }
        }
    }
    return line;
}

Real TextLines::score(Point p, const Rect& r) {
    const Real vertical = std::max({r.minY() - p.y, p.y - r.maxY(), 0.0});
    return vertical * 1000 + horizontalGap(p, r);
}

Real TextLines::horizontalGap(Point p, const Rect& r) {
    return std::max({r.minX() - p.x, p.x - r.maxX(), 0.0});
}

Real TextLines::verticalOverlap(const Rect& a, const Rect& b) {
    return std::max(std::min(a.maxY(), b.maxY()) - std::max(a.minY(), b.minY()), 0.0);
}

}  // namespace lz
