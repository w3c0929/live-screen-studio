#include "drag_pointer.h"

namespace lz {

DragPointer::DragPointer(Point at, std::optional<Rect> bounds)
    : location(confined(at, bounds)), previousSample(at) {}

void DragPointer::move(Point sample, std::optional<Rect> bounds) {
    // Keep the raw sample even at an edge, so reversing direction moves the pen back immediately.
    const Point next{location.x + (sample.x - previousSample.x),
                     location.y + (sample.y - previousSample.y)};
    previousSample = sample;
    location = confined(next, bounds);
}

Point DragPointer::confined(Point point, std::optional<Rect> bounds) {
    if (!bounds) return point;
    return {clamp(point.x, bounds->minX(), bounds->maxX()),
            clamp(point.y, bounds->minY(), bounds->maxY())};
}

}  // namespace lz
