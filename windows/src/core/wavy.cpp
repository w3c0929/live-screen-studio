#include "wavy.h"

#include <cmath>

namespace lz {

namespace {

constexpr Real kPi = 3.14159265358979323846;

Point lerp(Point a, Point b, Real t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

}  // namespace

std::vector<Point> WavyPath::offset(const std::vector<Point>& points, Real amplitude,
                                    Real wavelength) {
    if (points.size() < 2 || amplitude <= 0 || wavelength <= 0) return points;

    // Zero-length segments have no tangent; drop them so the normal stays well defined.
    std::vector<Point> path;
    path.reserve(points.size());
    for (const Point& point : points) {
        if (path.empty() || !(point == path.back())) path.push_back(point);
    }
    if (path.size() < 2) return path;

    const Real step = std::max<Real>(wavelength / 12, 0.5);
    const Real twoPi = 2 * kPi;
    std::vector<Point> out;
    out.push_back(path.front());  // sin(0) = 0: the wave starts on the stroke origin.

    Real nextSample = step;
    Real segmentStart = 0;
    for (size_t i = 1; i < path.size(); ++i) {
        const Point a = path[i - 1];
        const Point b = path[i];
        const Vector segment{b.x - a.x, b.y - a.y};
        const Real length = hypot(segment);
        if (length <= 0) continue;

        const Vector direction{segment.dx / length, segment.dy / length};
        const Vector normal{-direction.dy, direction.dx};
        const Real segmentEnd = segmentStart + length;
        while (nextSample < segmentEnd) {
            const Point point = lerp(a, b, (nextSample - segmentStart) / length);
            const Real wave = amplitude * std::sin(twoPi * nextSample / wavelength);
            out.push_back({point.x + normal.dx * wave, point.y + normal.dy * wave});
            nextSample += step;
        }
        if (i + 1 == path.size()) {
            const Real wave = amplitude * std::sin(twoPi * segmentEnd / wavelength);
            out.push_back({b.x + normal.dx * wave, b.y + normal.dy * wave});
        }
        segmentStart = segmentEnd;
    }
    return out;
}

}  // namespace lz
