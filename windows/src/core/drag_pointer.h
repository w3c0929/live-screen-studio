#pragma once

#include "geometry.h"

namespace lz {

/// Tracks a swallowed drag without moving the system cursor. Coordinates have a top-left origin.
struct DragPointer {
    Point location;
    Point previousSample;

    DragPointer(Point at, std::optional<Rect> bounds);

    void move(Point sample, std::optional<Rect> bounds);

private:
    static Point confined(Point point, std::optional<Rect> bounds);
};

}  // namespace lz
