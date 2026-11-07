#include "zoom_controller.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "input_hook.h"
#include "magnification_engine.h"
#include "overlay_renderer.h"
#include "style.h"

ZoomController::ZoomController(MagnificationEngine& engine, InputHook& input,
                               lz::Settings& settings)
    : engine_(engine),
      input_(input),
      settings_(settings),
      overlay_(std::make_unique<OverlayRenderer>()) {
    QueryPerformanceFrequency(&frequency_);
    QueryPerformanceCounter(&start_);
}

ZoomController::~ZoomController() = default;

double ZoomController::nowSeconds() const {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<double>(value.QuadPart - start_.QuadPart) /
           static_cast<double>(frequency_.QuadPart);
}

lz::Rect ZoomController::shownViewport() const {
    if (!camera_) return {0, 0, display_.width, display_.height};
    lz::Rect viewport = camera_->viewport();
    if (camera_->phase() == lz::Phase::Zoomed &&
        camera_->zoom() == camera_->targetZoom()) {
        // Whole output pixels keep the magnified image from resampling every frame.
        viewport = lz::ViewportMath::pixelAligned(viewport, camera_->zoom(), 1.0, display_);
    }
    return viewport;
}

lz::Point ZoomController::contentPoint(int x, int y) const {
    const lz::Rect viewport = shownViewport();
    const double zoom = camera_ ? camera_->zoom() : 1.0;
    return {viewport.x + (static_cast<double>(x) - monitorRect_.left) / zoom,
            viewport.y + (static_cast<double>(y) - monitorRect_.top) / zoom};
}

void ZoomController::startZoom() {
    POINT cursor{};
    GetCursorPos(&cursor);
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return;

    engine_.setSmoothing(settings_.smoothImage);
    engine_.setExcludeObs(settings_.excludeObs);
    if (!engine_.showForMonitor(monitor)) return;

    monitor_ = monitor;
    monitorRect_ = info.rcMonitor;
    display_ = {static_cast<lz::Real>(monitorRect_.right - monitorRect_.left),
                static_cast<lz::Real>(monitorRect_.bottom - monitorRect_.top)};

    lz::InkLook ink;
    ink.style = lz::InkStyle::Marker;
    ink.color = {1.0f, 0.23f, 0.19f, 1.0f};
    ink.width = 5.0;
    overlay_->setInk(ink);
    overlay_->setClickEffect(lz::ClickEffect::Ripple, {0.20f, 0.55f, 1.0f, 1.0f});
    const bool overlayShown = overlay_->showForMonitor(monitor);
    engine_.setOverlayWindow(overlayShown ? overlay_->hwnd() : nullptr);
    overlay_->setMapping({0, 0}, 1.0, display_);

    lz::ZoomCamera::Config config;
    config.follow = settings_.followPreset();
    config.minZoom = settings_.minZoom;
    config.maxZoom = settings_.maxZoom;
    camera_.emplace(display_, config);

    const lz::Point local{
        std::clamp(static_cast<lz::Real>(cursor.x - monitorRect_.left), 0.0, display_.width),
        std::clamp(static_cast<lz::Real>(cursor.y - monitorRect_.top), 0.0, display_.height)};
    camera_->zoomIn(settings_.defaultZoom, local, nowSeconds());

    active_ = true;
    lastTick_ = nowSeconds();
    input_.setZoomActive(true);
    input_.setDrawingArmed(settings_.drawWhileZoomed && overlay_->visible());
    input_.setHasStrokes(false);
    input_.setCursorBounds(lz::Rect{static_cast<lz::Real>(monitorRect_.left),
                                    static_cast<lz::Real>(monitorRect_.top),
                                    display_.width, display_.height});
    std::printf("[livezoom] zoom on: %.2fx (drawing=%d overlay=%d)\n",
                settings_.defaultZoom, settings_.drawWhileZoomed ? 1 : 0,
                overlay_->visible() ? 1 : 0);
}

void ZoomController::finish() {
    if (!active_) return;
    active_ = false;
    if (overlay_) {
        overlay_->clear();
        overlay_->hide();
    }
    engine_.setOverlayWindow(nullptr);
    engine_.hide();
    strokeDrawing_ = false;
    input_.setDrawingArmed(false);
    input_.setHasStrokes(false);
    input_.setZoomActive(false);
    input_.setCursorBounds(std::nullopt);
    camera_.reset();
    std::printf("[livezoom] zoom off\n");
}

void ZoomController::reset() {
    finish();
}

void ZoomController::toggle() {
    if (!active_) {
        startZoom();
        return;
    }
    POINT cursor{};
    GetCursorPos(&cursor);
    const lz::Point local{
        std::clamp(static_cast<lz::Real>(cursor.x - monitorRect_.left), 0.0, display_.width),
        std::clamp(static_cast<lz::Real>(cursor.y - monitorRect_.top), 0.0, display_.height)};
    if (camera_->phase() == lz::Phase::ZoomingOut) {
        camera_->zoomIn(settings_.defaultZoom, local, nowSeconds());
    } else {
        camera_->zoomOut(local, nowSeconds());
    }
}

void ZoomController::adjustZoom(double steps) {
    if (!active_) {
        if (steps <= 0) return;
        startZoom();
        if (!active_ || !camera_) return;
    }
    camera_->adjustZoom(std::exp(0.1 * steps));
    std::printf("[livezoom] zoom target: %.2fx\n", camera_->targetZoom());
}

void ZoomController::tick() {
    if (!active_ || !camera_) return;

    const double now = nowSeconds();
    const double dt = std::clamp(now - lastTick_, 1.0 / 240.0, 1.0 / 20.0);
    lastTick_ = now;

    POINT cursor{};
    GetCursorPos(&cursor);
    const lz::Point local{
        std::clamp(static_cast<lz::Real>(cursor.x - monitorRect_.left), 0.0, display_.width),
        std::clamp(static_cast<lz::Real>(cursor.y - monitorRect_.top), 0.0, display_.height)};
    camera_->update(local, now, dt);

    const lz::Rect viewport = shownViewport();
    if (overlay_) {
        overlay_->setMapping(viewport.origin(), camera_->zoom(), display_);
        // The hook also sends moves, but sampling here keeps the line continuous even when a
        // busy main thread or a mouse driver drops some of them.
        if (strokeDrawing_) {
            const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            overlay_->extendStroke(contentPoint(cursor.x, cursor.y), shift);
        }
        overlay_->render(now);
    }
    engine_.setViewport({viewport.x + static_cast<lz::Real>(monitorRect_.left),
                         viewport.y + static_cast<lz::Real>(monitorRect_.top)},
                        camera_->zoom());

    if (camera_->phase() == lz::Phase::Idle) {
        finish();
    }
}

void ZoomController::handleClick(int x, int y) {
    if (!active_ || !overlay_) return;
    overlay_->addClick({static_cast<lz::Real>(x - monitorRect_.left),
                         static_cast<lz::Real>(y - monitorRect_.top)},
                        nowSeconds());
}

void ZoomController::beginStroke(int x, int y) {
    if (!active_ || !overlay_ || !overlay_->visible()) return;
    overlay_->setMapping(shownViewport().origin(), camera_->zoom(), display_);
    overlay_->beginStroke(contentPoint(x, y));
    camera_->setFollowPaused(true);
    strokeDrawing_ = true;
}

void ZoomController::moveStroke(int x, int y, bool straight) {
    if (!active_ || !overlay_) return;
    overlay_->extendStroke(contentPoint(x, y), straight);
}

void ZoomController::endStroke() {
    if (!active_ || !overlay_) return;
    strokeDrawing_ = false;
    overlay_->endStroke();
    camera_->setFollowPaused(false);
    input_.setHasStrokes(overlay_->hasContent());
    std::printf("[livezoom] stroke end, content=%d\n", overlay_->hasContent() ? 1 : 0);
}

void ZoomController::cancelStroke() {
    if (!overlay_) return;
    strokeDrawing_ = false;
    overlay_->cancelStroke();
    camera_->setFollowPaused(false);
    input_.setHasStrokes(overlay_->hasContent());
}

void ZoomController::undoStroke() {
    if (!overlay_) return;
    overlay_->undo();
    input_.setHasStrokes(overlay_->hasContent());
}

void ZoomController::clearStrokes() {
    if (!overlay_) return;
    overlay_->clear();
    input_.setHasStrokes(false);
}

void ZoomController::reloadSettings() {
    if (!active_) return;
    if (camera_) {
        camera_->config.follow = settings_.followPreset();
        camera_->config.minZoom = settings_.minZoom;
        camera_->config.maxZoom = settings_.maxZoom;
    }
    engine_.setSmoothing(settings_.smoothImage);
    engine_.setExcludeObs(settings_.excludeObs);
    input_.setDrawingArmed(settings_.drawWhileZoomed && overlay_ && overlay_->visible());
}
