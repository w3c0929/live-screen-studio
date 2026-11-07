#pragma once

#include <optional>
#include <vector>

#include "geometry.h"

namespace lz {

struct TextLines {
    /// The line of text at `point`, built from text segments found on screen (same coordinates, y down).
    static std::optional<Rect> line(Point point, const std::vector<Rect>& segments);

private:
    static Real score(Point p, const Rect& r);
    static Real horizontalGap(Point p, const Rect& r);
    static Real verticalOverlap(const Rect& a, const Rect& b);
};

}  // namespace lz
