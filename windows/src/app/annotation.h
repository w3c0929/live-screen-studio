#pragma once

#include <vector>

#include "geometry.h"
#include "style.h"

namespace lz {

/// One stroke or highlight in display-local content coordinates (origin top-left, y down).
struct Annotation {
    bool spotlight = false;
    std::vector<Point> points;
    Rect line;
    HighlightKind kind = HighlightKind::Box;
    InkLook look;
    /// Content units per screen point when the annotation was made.
    double unit = 1;
    bool complete = false;

    static Annotation stroke(Point start, InkLook look, double zoom);
    static Annotation highlight(HighlightKind kind, Rect line, InkLook look, double zoom);

    void extend(Point point, bool straight);
};

}  // namespace lz
