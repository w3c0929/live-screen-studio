#include "cursor_motion.h"

#include <algorithm>
#include <cmath>

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
        const Real fast = clamp(speed_ / static_cast<Real>(600), 0.0, 1.0);
        const double tau = std::max(smoothTime * (0.15 + 0.85 * (1.0 - fast)), 1e-4);
        const Real alpha = static_cast<Real>(1 - std::exp(-dt / tau));
        position.x += (input.x - position.x) * alpha;
        position.y += (input.y - position.y) * alpha;
        // Hard cap: a trailing cursor reads as "the mouse is not following", especially in a
        // recording, so the drawn pointer never gets further than maxLag from the real one.
        const Vector lag{input.x - position.x, input.y - position.y};
        const Real length = hypot(lag);
        if (length > maxLag) {
            const Real pull = (length - maxLag) / length;
            position.x += lag.dx * pull;
            position.y += lag.dy * pull;
        }
    } else {
        position = input;
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
    speed_ = 0;
    opacity = 1;
}

}  // namespace lz
