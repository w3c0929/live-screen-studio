#pragma once

#include "geometry.h"

namespace lz {

struct SmoothDamp {
    /// Critically damped spring toward `target` (Game Programming Gems 4, 1.10).
    /// Never overshoots, and moves at most `maxSpeed` units per second.
    static Point step(Point current, Point target, Vector& velocity, double smoothTime,
                      Real maxSpeed, double dt);
};

}  // namespace lz
