#pragma once

#include <optional>

#include "geometry.h"

namespace lz {

/// Where the drawn cursor is and how visible it is: optional smoothing and fade-out when idle.
struct CursorMotion {
    /// 0 follows the mouse exactly.
    double smoothTime = 0;
    /// Largest distance the drawn cursor may trail the pointer, in content points. Small values
    /// keep the cursor visually locked to the hand even while smoothing is enabled.
    Real maxLag = 1e9;
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
    Point lastInput_;
    double lastActivity_;
    /// Low-passed pointer speed, used to shorten the smoothing when the mouse moves fast.
    Real speed_ = 0;
};

}  // namespace lz
