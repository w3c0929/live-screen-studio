#pragma once

#include <optional>

#include "follow_preset.h"
#include "geometry.h"

namespace lz {

/// Viewport geometry. Points are display-local, in points, origin at the top-left.
namespace ViewportMath {

Size size(Size display, Real zoom);
Point clampCenter(Point center, Real zoom, Size display);
Rect rect(Point center, Real zoom, Size display);
Point screenPoint(Point p, Point center, Real zoom, Size display);
Point anchoredCenter(Point anchor, Point screenPoint, Real zoom, Size display);

/// `viewport` moved by less than one output pixel so that its origin falls on whole output pixels (at
/// `pixelsPerPoint`). Never moves past the display's far edge.
Rect pixelAligned(Rect viewport, Real zoom, Real pixelsPerPoint, Size display);

/// Leaves `center` alone while `cursor` is inside the safe zone (`margin` from each edge). Once the cursor
/// crosses it, moves just far enough that the cursor sits `settleMargin` from that edge.
Point applySafeZone(Point center, Point cursor, Real zoom, Size display, Real margin,
                    std::optional<Real> settleMargin = std::nullopt);

}  // namespace ViewportMath

enum class Phase {
    Idle,
    ZoomingIn,
    Zoomed,
    ZoomingOut,
};

/// Zoom camera for one display: eased zoom transitions plus speed-limited cursor following.
class ZoomCamera {
public:
    struct Config {
        FollowPreset follow = FollowPreset::calm;
        double zoomInDuration = 0.55;
        double zoomOutDuration = 0.45;
        Real minZoom = 1.25;
        Real maxZoom = 4;
        /// Time constant of wheel zoom adjustments.
        double zoomAdjustTime = 0.15;
        /// Speed multiplier ceiling while the cursor is outside the visible viewport.
        Real catchUpLimit = 3;
    };

    Config config;
    const Size display;

    explicit ZoomCamera(Size display);
    ZoomCamera(Size display, Config config);

    /// Holds the camera still (e.g. while drawing) so the content under the pen does not slide away.
    [[nodiscard]] bool followPaused() const { return followPaused_; }
    void setFollowPaused(bool paused);

    [[nodiscard]] Phase phase() const { return phase_; }
    [[nodiscard]] Real zoom() const { return zoom_; }
    [[nodiscard]] Point center() const { return center_; }
    /// Zoom level the camera is heading to.
    [[nodiscard]] Real targetZoom() const { return targetZoom_; }
    [[nodiscard]] bool isActive() const { return phase_ != Phase::Idle; }
    [[nodiscard]] Rect viewport() const;
    [[nodiscard]] Point screenPoint(Point p) const;
    [[nodiscard]] Real clampZoom(Real z) const;

    void zoomIn(Real level, Point cursor, double now);
    void zoomOut(Point cursor, double now);

    /// Multiplies the zoom level. The change is applied gradually around the live cursor position.
    void adjustZoom(Real factor);

    void update(Point cursor, double now, double dt);

private:
    struct Transition {
        double start = 0;
        double duration = 0;
        Real fromZoom = 1;
        Real toZoom = 1;
        Point anchor;
        Point anchorScreen;
        Point toCenter;
    };

    void begin(Phase phase, Real toZoom, Point toCenter, Point anchor, Point anchorScreen,
               double duration, double now);
    void advanceTransition(double now);
    void follow(Point cursor, double dt);
    [[nodiscard]] Point settledCenter(Point center, Point cursor, Real zoom) const;
    [[nodiscard]] Point clampToDisplay(Point p) const;
    [[nodiscard]] Real catchUpBoost(Point cursor) const;

    Phase phase_ = Phase::Idle;
    Real zoom_ = 1;
    Point center_;
    Point targetCenter_;
    Real targetZoom_ = 1;
    Vector velocity_;
    std::optional<Transition> transition_;
    Real pendingZoomFactor_ = 1;
    Real boost_ = 1;
    bool followPaused_ = false;

    /// Screen points per second: one pixel per frame at 60 Hz.
    static constexpr Real minVisualSpeed = 60;
};

}  // namespace lz
