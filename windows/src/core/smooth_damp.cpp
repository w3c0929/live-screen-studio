#include "smooth_damp.h"

namespace lz {

Point SmoothDamp::step(Point current, Point target, Vector& velocity, double smoothTime,
                       Real maxSpeed, double dt) {
    if (dt <= 0) return current;
    smoothTime = std::max(0.0001, smoothTime);
    const double omega = 2 / smoothTime;
    const double x = omega * dt;
    const double decay = 1 / (1 + x + 0.48 * x * x + 0.235 * x * x * x);
    const Real omegaF = static_cast<Real>(omega);
    const Real dtF = static_cast<Real>(dt);

    Vector change{current.x - target.x, current.y - target.y};
    const Real maxChange = maxSpeed * static_cast<Real>(smoothTime);
    const Real changeLength = hypot(change);
    if (changeLength > maxChange) {
        change.dx *= maxChange / changeLength;
        change.dy *= maxChange / changeLength;
    }
    const Point clampedTarget{current.x - change.dx, current.y - change.dy};

    const Vector temp{(velocity.dx + omegaF * change.dx) * dtF,
                      (velocity.dy + omegaF * change.dy) * dtF};
    Vector newVelocity{(velocity.dx - omegaF * temp.dx) * static_cast<Real>(decay),
                       (velocity.dy - omegaF * temp.dy) * static_cast<Real>(decay)};
    Point output{clampedTarget.x + (change.dx + temp.dx) * static_cast<Real>(decay),
                 clampedTarget.y + (change.dy + temp.dy) * static_cast<Real>(decay)};

    // Stop exactly on the target instead of passing it.
    const Vector toTarget{target.x - current.x, target.y - current.y};
    const Vector past{output.x - target.x, output.y - target.y};
    if (toTarget.dx * past.dx + toTarget.dy * past.dy > 0) {
        output = target;
        newVelocity = {};
    }

    // Hard speed cap so a sudden far target never produces a fast pan.
    const Vector stepVector{output.x - current.x, output.y - current.y};
    const Real stepLength = hypot(stepVector);
    const Real maxStep = maxSpeed * dtF;
    if (stepLength > maxStep) {
        const Real scale = maxStep / stepLength;
        output = {current.x + stepVector.dx * scale, current.y + stepVector.dy * scale};
        newVelocity = {stepVector.dx * scale / dtF, stepVector.dy * scale / dtF};
    }

    velocity = newVelocity;
    return output;
}

}  // namespace lz
