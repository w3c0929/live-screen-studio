#include "annotation.h"

#include <cmath>

namespace lz {

Annotation Annotation::stroke(Point start, InkLook look, double zoom) {
    Annotation annotation;
    annotation.look = look;
    annotation.unit = 1.0 / zoom;
    annotation.points.push_back(start);
    return annotation;
}

Annotation Annotation::highlight(HighlightKind kind, Rect line, InkLook look, double zoom) {
    Annotation annotation;
    annotation.look = look;
    annotation.unit = 1.0 / zoom;
    annotation.kind = kind;
    annotation.line = line;
    annotation.spotlight = kind == HighlightKind::Spotlight;
    return annotation;
}

void Annotation::extend(Point point, bool straight) {
    if (straight) {
        if (points.empty()) {
            points.push_back(point);
        } else {
            points.resize(1);
            points.push_back(point);
        }
        return;
    }
    if (!points.empty()) {
        const Point& last = points.back();
        if (std::hypot(point.x - last.x, point.y - last.y) < 0.75 * unit) return;
    }
    points.push_back(point);
}

}  // namespace lz
