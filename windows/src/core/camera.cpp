#include "camera.h"

#include <algorithm>
#include <cmath>

#include "easing.h"
#include "smooth_damp.h"

namespace lz {

namespace ViewportMath {

Size size(Size display, Real zoom) {
    return {display.width / zoom, display.height / zoom};
}

Point clampCenter(Point center, Real zoom, Size display) {
    const Size half = size(display, zoom);
    const Real minX = half.width / 2, maxX = display.width - half.width / 2;
    const Real minY = half.height / 2, maxY = display.height - half.height / 2;
    return {clamp(center.x, minX, maxX), clamp(center.y, minY, maxY)};
}

Rect rect(Point center, Real zoom, Size display) {
    const Size s = size(display, zoom);
    return {center.x - s.width / 2, center.y - s.height / 2, s.width, s.height};
}

Point screenPoint(Point p, Point center, Real zoom, Size display) {
    const Rect r = rect(center, zoom, display);
    return {(p.x - r.x) * zoom, (p.y - r.y) * zoom};
}

Point anchoredCenter(Point anchor, Point screenPoint, Real zoom, Size display) {
    return {anchor.x + (display.width / 2 - screenPoint.x) / zoom,
            anchor.y + (display.height / 2 - screenPoint.y) / zoom};
}

Rect pixelAligned(Rect viewport, Real zoom, Real pixelsPerPoint, Size display) {
    const Real step = 1 / (zoom * pixelsPerPoint);
    auto align = [&](Real value, Real limit) {
        return std::min(std::round(value / step) * step, std::floor(limit / step) * step);
    };
    Rect aligned = viewport;
    aligned.x = std::max(align(viewport.minX(), std::max(display.width - viewport.width, 0.0)), 0.0);
    aligned.y = std::max(align(viewport.minY(), std::max(display.height - viewport.height, 0.0)), 0.0);
    return aligned;
}

Point applySafeZone(Point center, Point cursor, Real zoom, Size display, Real margin,
                    std::optional<Real> settleMargin) {
    const Size s = size(display, zoom);
    const Real settle = std::max(settleMargin.value_or(margin), margin);
    const Real hx = s.width * (0.5 - margin), hy = s.height * (0.5 - margin);
    const Real sx = s.width * (0.5 - settle), sy = s.height * (0.5 - settle);
    Point c = center;
    if (cursor.x > c.x + hx) {
        c.x = cursor.x - sx;
    } else if (cursor.x < c.x - hx) {
        c.x = cursor.x + sx;
    }
    if (cursor.y > c.y + hy) {
        c.y = cursor.y - sy;
    } else if (cursor.y < c.y - hy) {
        c.y = cursor.y + sy;
    }
    return c;
}

}  // namespace ViewportMath

ZoomCamera::ZoomCamera(Size display) : ZoomCamera(display, Config{}) {}

ZoomCamera::ZoomCamera(Size display, Config config)
    : config(config), display(display),
      center_{display.width / 2, display.height / 2},
      targetCenter_{display.width / 2, display.height / 2} {}

void ZoomCamera::setFollowPaused(bool paused) {
    followPaused_ = paused;
    if (paused) velocity_ = {};
}

Rect ZoomCamera::viewport() const {
    return ViewportMath::rect(center_, zoom_, display);
}

Point ZoomCamera::screenPoint(Point p) const {
    return ViewportMath::screenPoint(p, center_, zoom_, display);
}

Real ZoomCamera::clampZoom(Real z) const {
    return clamp(z, config.minZoom, config.maxZoom);
}

void ZoomCamera::zoomIn(Real level, Point cursor, double now) {
    cursor = clampToDisplay(cursor);
    const Real z = clampZoom(level);
    const Point anchorScreen = screenPoint(cursor);
    const Point fixed = ViewportMath::anchoredCenter(cursor, anchorScreen, z, display);
    const Point to = settledCenter(fixed, cursor, z);
    begin(Phase::ZoomingIn, z, to, cursor, anchorScreen, config.zoomInDuration, now);
}

void ZoomCamera::zoomOut(Point cursor, double now) {
    if (phase_ == Phase::Idle) return;
    cursor = clampToDisplay(cursor);
    const Point to{display.width / 2, display.height / 2};
    begin(Phase::ZoomingOut, 1, to, cursor, screenPoint(cursor), config.zoomOutDuration, now);
}

void ZoomCamera::adjustZoom(Real factor) {
    switch (phase_) {
        case Phase::Idle:
        case Phase::ZoomingOut:
            return;
        case Phase::ZoomingIn:
            if (!transition_) return;
            pendingZoomFactor_ *= factor;
            targetZoom_ = clampZoom(transition_->toZoom * pendingZoomFactor_);
            return;
        case Phase::Zoomed:
            targetZoom_ = clampZoom(targetZoom_ * factor);
            return;
    }
}

void ZoomCamera::update(Point cursor, double now, double dt) {
    cursor = clampToDisplay(cursor);
    switch (phase_) {
        case Phase::Idle:
            return;
        case Phase::ZoomingIn:
        case Phase::ZoomingOut:
            advanceTransition(now);
            return;
        case Phase::Zoomed:
            follow(cursor, dt);
            return;
    }
}

void ZoomCamera::begin(Phase phase, Real toZoom, Point toCenter, Point anchor,
                       Point anchorScreen, double duration, double now) {
    transition_ = Transition{now, duration, zoom_, toZoom, anchor, anchorScreen, toCenter};
    phase_ = phase;
    targetZoom_ = toZoom;
    pendingZoomFactor_ = 1;
    velocity_ = {};
    boost_ = 1;
}

void ZoomCamera::advanceTransition(double now) {
    if (!transition_) return;
    const Transition t = *transition_;
    const double progress = clamp((now - t.start) / t.duration, 0.0, 1.0);
    const Real e = static_cast<Real>(Easing::easeInOutCubic(progress));
    // Interpolate in log space so the zoom speed looks constant.
    zoom_ = std::exp(std::log(t.fromZoom) + (std::log(t.toZoom) - std::log(t.fromZoom)) * e);
    const Point fixedNow =
        ViewportMath::anchoredCenter(t.anchor, t.anchorScreen, zoom_, display);
    const Point fixedEnd =
        ViewportMath::anchoredCenter(t.anchor, t.anchorScreen, t.toZoom, display);
    const Point blended{fixedNow.x + (t.toCenter.x - fixedEnd.x) * e,
                        fixedNow.y + (t.toCenter.y - fixedEnd.y) * e};
    center_ = ViewportMath::clampCenter(blended, zoom_, display);

    if (progress < 1) return;
    transition_.reset();
    zoom_ = t.toZoom;
    center_ = ViewportMath::clampCenter(t.toCenter, zoom_, display);
    targetCenter_ = center_;
    velocity_ = {};
    boost_ = 1;
    const bool out = phase_ == Phase::ZoomingOut;
    targetZoom_ = out ? 1 : clampZoom(t.toZoom * pendingZoomFactor_);
    pendingZoomFactor_ = 1;
    phase_ = out ? Phase::Idle : Phase::Zoomed;
}

void ZoomCamera::follow(Point cursor, double dt) {
    if (zoom_ != targetZoom_) {
        const Real k = 1 - static_cast<Real>(std::exp(-dt / config.zoomAdjustTime));
        const Real remaining = static_cast<Real>(std::log(targetZoom_) - std::log(zoom_));
        // Keep the far edge moving at least about a point per frame and land exactly.
        const Real minStep = 1 / display.width;
        const Real step = std::abs(remaining * k) >= minStep
                              ? remaining * k
                              : (remaining < 0 ? -1 : 1) * std::min(std::abs(remaining), minStep);
        const Real newZoom = std::abs(remaining - step) < 1e-9
                                 ? targetZoom_
                                 : static_cast<Real>(std::exp(std::log(zoom_) + step));
        const Real ratio = zoom_ / newZoom;
        center_ = {cursor.x + (center_.x - cursor.x) * ratio,
                   cursor.y + (center_.y - cursor.y) * ratio};
        targetCenter_ = {cursor.x + (targetCenter_.x - cursor.x) * ratio,
                         cursor.y + (targetCenter_.y - cursor.y) * ratio};
        zoom_ = newZoom;
        center_ = ViewportMath::clampCenter(center_, zoom_, display);
    }
    if (followPaused_) {
        targetCenter_ = center_;
        return;
    }
    targetCenter_ = settledCenter(targetCenter_, cursor, zoom_);
    const Real wantedBoost = catchUpBoost(cursor);
    const double boostTime = wantedBoost > boost_ ? 0.1 : 0.35;
    boost_ += (wantedBoost - boost_) * (1 - static_cast<Real>(std::exp(-dt / boostTime)));
    const Point previous = center_;
    center_ = SmoothDamp::step(center_, targetCenter_, velocity_, config.follow.smoothTime,
                               config.follow.maxVisualSpeed * boost_ / zoom_, dt);
    // The spring's tail creeps by fractions of a pixel for a long time, which makes text shimmer.
    const Real remaining = hypot(targetCenter_, previous);
    const Real minStep = minVisualSpeed * static_cast<Real>(dt) / zoom_;
    if (remaining > 0 && hypot(center_, previous) < minStep) {
        if (remaining <= minStep) {
            center_ = targetCenter_;
            velocity_ = {};
        } else {
            const Real k = minStep / remaining;
            center_ = {previous.x + (targetCenter_.x - previous.x) * k,
                       previous.y + (targetCenter_.y - previous.y) * k};
        }
    }
    center_ = ViewportMath::clampCenter(center_, zoom_, display);
}

Real ZoomCamera::catchUpBoost(Point cursor) const {
    const Rect vp = viewport();
    const Real dx = std::max({vp.minX() - cursor.x, 0.0, cursor.x - vp.maxX()});
    const Real dy = std::max({vp.minY() - cursor.y, 0.0, cursor.y - vp.maxY()});
    const Real outside = std::hypot(dx, dy) / (std::min(vp.width, vp.height) / 2);
    return std::min(1 + 2 * outside, config.catchUpLimit);
}

Point ZoomCamera::settledCenter(Point center, Point cursor, Real zoom) const {
    const Point safe = ViewportMath::applySafeZone(center, cursor, zoom, display,
                                                   config.follow.margin,
                                                   config.follow.settleMargin);
    return ViewportMath::clampCenter(safe, zoom, display);
}

Point ZoomCamera::clampToDisplay(Point p) const {
    return {clamp(p.x, 0, display.width), clamp(p.y, 0, display.height)};
}

}  // namespace lz
