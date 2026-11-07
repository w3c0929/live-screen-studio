#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

namespace lz {

using Real = double;

struct Point {
    Real x = 0;
    Real y = 0;

    bool operator==(const Point&) const = default;
};

struct Vector {
    Real dx = 0;
    Real dy = 0;

    bool operator==(const Vector&) const = default;
};

struct Size {
    Real width = 0;
    Real height = 0;

    bool operator==(const Size&) const = default;
};

struct Rect {
    Real x = 0;
    Real y = 0;
    Real width = 0;
    Real height = 0;

    bool operator==(const Rect&) const = default;

    [[nodiscard]] Real minX() const { return x; }
    [[nodiscard]] Real minY() const { return y; }
    [[nodiscard]] Real maxX() const { return x + width; }
    [[nodiscard]] Real maxY() const { return y + height; }
    [[nodiscard]] Real midX() const { return x + width / 2; }
    [[nodiscard]] Real midY() const { return y + height / 2; }
    [[nodiscard]] Point origin() const { return {x, y}; }
    [[nodiscard]] Size size() const { return {width, height}; }

    [[nodiscard]] Rect insetBy(Real dx, Real dy) const {
        return {x + dx, y + dy, width - dx * 2, height - dy * 2};
    }

    [[nodiscard]] bool contains(Point p) const {
        return p.x >= minX() && p.x <= maxX() && p.y >= minY() && p.y <= maxY();
    }

    [[nodiscard]] Rect united(const Rect& other) const {
        const Real nx = std::min(minX(), other.minX());
        const Real ny = std::min(minY(), other.minY());
        const Real nx2 = std::max(maxX(), other.maxX());
        const Real ny2 = std::max(maxY(), other.maxY());
        return {nx, ny, nx2 - nx, ny2 - ny};
    }
};

inline Point operator+(Point a, Point b) { return {a.x + b.x, a.y + b.y}; }
inline Point operator-(Point a, Point b) { return {a.x - b.x, a.y - b.y}; }
inline Point operator*(Point a, Real s) { return {a.x * s, a.y * s}; }
inline Point operator/(Point a, Real s) { return {a.x / s, a.y / s}; }

inline Real hypot(Point a, Point b) { return std::hypot(a.x - b.x, a.y - b.y); }
inline Real hypot(Vector v) { return std::hypot(v.dx, v.dy); }
inline Real hypot(Real x, Real y) { return std::hypot(x, y); }

inline Real clamp(Real v, Real lo, Real hi) { return std::min(std::max(v, lo), hi); }

}  // namespace lz
