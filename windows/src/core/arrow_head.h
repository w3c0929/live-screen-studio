#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "geometry.h"

namespace lz {

struct ArrowHead {
    /// The two barb ends of an arrowhead at the last point of a stroke, pointing along its last `length`
    /// or so, so a little jitter at the end of the stroke does not swing the head around. nullopt for a
    /// stroke that is still too short to have a direction.
    static std::optional<std::pair<Point, Point>> barbs(const std::vector<Point>& points, Real length,
                                                        Real angle = 3.14159265358979323846 / 6.5);
};

}  // namespace lz
