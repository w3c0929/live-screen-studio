#include "cursor_motion.h"

#include <algorithm>
#include <limits>

namespace lz {

CursorMotion::CursorMotion(Point position, double now)
    : position(position), lastInput_(position), lastActivity_(now) {}

void CursorMotion::update(Point input, bool buttonsDown, double now, double dt) {
    const bool moved = !(input == lastInput_);
    if (moved || buttonsDown) {
        lastActivity_ = now;
    }
    if (dt > 0) {
        const Real instant =
            moved ? hypot(input, lastInput_) / static_cast<Real>(dt) : 0;
        // Smoothed speed keeps the time constant from flickering between samples.
        const Real blend = static_cast<Real>(std::min(1.0, dt / 0.05));
        speed_ += (instant - speed_) * blend;
    }
    lastInput_ = input;

    if (smoothTime > 0) {
        // Fast motion must stay under the hand; smoothing is mostly for slow, jittery moves.
        // Fixed smoothing made the drawn cursor trail the pointer, which reads as unnatural.
        const Real fast = clamp(speed_ / static_cast<Real>(900), 0.0, 1.0);
        const double effective = smoothTime * (0.2 + 0.8 * (1.0 - fast));
        position = SmoothDamp::step(position, input, velocity_, effective,
                                    std::numeric_limits<Real>::max(), dt);
    } else {
        position = input;
        velocity_ = {};
    }
    const bool hidden = idleDelay && (now - lastActivity_ >= *idleDelay);
    if (hidden) {
        opacity = std::max<Real>(0, opacity - static_cast<Real>(dt / fadeOutDuration));
    } else {
        opacity = std::min<Real>(1, opacity + static_cast<Real>(dt / fadeInDuration));
    }
}

void CursorMotion::jump(Point to, double now) {
    position = to;
    lastInput_ = to;
    lastActivity_ = now;
    velocity_ = {};
    speed_ = 0;
    opacity = 1;
}

}  // namespace lz
