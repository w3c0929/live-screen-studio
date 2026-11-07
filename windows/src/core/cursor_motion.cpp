#include "cursor_motion.h"

#include <limits>

namespace lz {

CursorMotion::CursorMotion(Point position, double now)
    : position(position), lastInput_(position), lastActivity_(now) {}

void CursorMotion::update(Point input, bool buttonsDown, double now, double dt) {
    if (!(input == lastInput_) || buttonsDown) {
        lastInput_ = input;
        lastActivity_ = now;
    }
    if (smoothTime > 0) {
        position = SmoothDamp::step(position, input, velocity_, smoothTime,
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
    opacity = 1;
}

}  // namespace lz
