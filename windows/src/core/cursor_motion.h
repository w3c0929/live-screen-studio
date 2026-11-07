#pragma once

#include <optional>

#include "geometry.h"
#include "smooth_damp.h"

namespace lz {

/// Where the drawn cursor is and how visible it is: optional smoothing and fade-out when idle.
struct CursorMotion {
    /// 0 follows the mouse exactly.
    double smoothTime = 0;
    /// nullopt never hides the cursor.
    std::optional<double> idleDelay;
    double fadeOutDuration = 0.35;
    double fadeInDuration = 0.08;

    Point position;
    Real opacity = 1;

    CursorMotion(Point position, double now);

    void update(Point input, bool buttonsDown, double now, double dt);

    /// Moves without smoothing, e.g. when the drawing starts.
    void jump(Point to, double now);

private:
    Vector velocity_;
    Point lastInput_;
    double lastActivity_;
};

}  // namespace lz
