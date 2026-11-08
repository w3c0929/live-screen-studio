#pragma once

#include <vector>

#include "geometry.h"
#include "style.h"

namespace lz {

/// One stroke in display-local content coordinates (origin top-left, y down).
struct Annotation {
    std::vector<Point> points;
    InkLook look;
    /// Content units per screen point when the annotation was made.
    double unit = 1;
    bool complete = false;

    static Annotation stroke(Point start, InkLook look, double zoom);

    void extend(Point point, bool straight);
};

}  // namespace lz
