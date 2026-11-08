#include "zoom_controller.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "input_hook.h"
#include "magnification_engine.h"
#include "ocr_service.h"
#include "overlay_renderer.h"
#include "style.h"

ZoomController::ZoomController(MagnificationEngine& engine, InputHook& input, OcrService& ocr,
                               lz::Settings& settings)
    : engine_(engine),
      input_(input),
      ocr_(ocr),
      settings_(settings),
      overlay_(std::make_unique<OverlayRenderer>()),
      idleOverlay_(std::make_unique<OverlayRenderer>()) {
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
    // Display-local physical coordinates. The overlay maps these with the same viewport
    // transform the magnifier uses, so ink lands under the magnified cursor even after the
    // camera has followed the pointer into its safe-zone margin.
    return {static_cast<lz::Real>(x) - monitorRect_.left,
            static_cast<lz::Real>(y) - monitorRect_.top};
}

void ZoomController::startZoom() {
    ++session_;
    hideIdleCursor(false);
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

    overlay_->setInk(settings_.inkLook());
    overlay_->setClickEffect(settings_.clickEffect, settings_.clickRgba());
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
    const double now = nowSeconds();
    camera_->zoomIn(settings_.defaultZoom, local, now);
    cursorMotion_.emplace(local, now);
    cursorMotion_->smoothTime = settings_.smoothCursor ? 0.06 : 0.0;
    cursorMotion_->idleDelay = settings_.hideIdleCursor
                                   ? std::optional<double>(settings_.idleDelay)
                                   : std::nullopt;
    cursorMotion_->jump(local, now);
    overlay_->setCursorStyle(settings_.cursorStyle);

    active_ = true;
    lastTick_ = nowSeconds();
    cursorRestoreAt_ = 0;
    cursorRestoreTries_ = 0;
    input_.setZoomActive(true);
    input_.setDrawingArmed(settings_.drawWhileZoomed && overlay_->visible());
    input_.setHasStrokes(false);
    input_.setCursorBounds(lz::Rect{static_cast<lz::Real>(monitorRect_.left),
                                    static_cast<lz::Real>(monitorRect_.top),
                                    display_.width, display_.height});
    std::printf("[livezoom] zoom on: %.2fx display=%.0fx%.0f monitor=(%ld,%ld)-(%ld,%ld) "
                "(drawing=%d overlay=%d)\n",
                settings_.defaultZoom, display_.width, display_.height, monitorRect_.left,
                monitorRect_.top, monitorRect_.right, monitorRect_.bottom,
                settings_.drawWhileZoomed ? 1 : 0, overlay_->visible() ? 1 : 0);
    lastPhase_ = lz::Phase::Idle;
    followStartLogged_ = false;
}

void ZoomController::finish() {
    if (!active_) return;
    ++session_;
    active_ = false;
    if (overlay_) {
        overlay_->clear();
        overlay_->hide();
    }
    engine_.setOverlayWindow(nullptr);
    engine_.hide();
    idleHardwareHidden_ = false;
    // Some drivers apply the magnifier's cursor hide asynchronously. Retry after the zoom-out
    // animation so the pointer is back without requiring Ctrl+Alt+Del.
    cursorRestoreAt_ = nowSeconds() + 0.15;
    cursorRestoreTries_ = 3;
    strokeDrawing_ = false;
    input_.setDrawingArmed(false);
    input_.setHasStrokes(false);
    input_.setZoomActive(false);
    input_.setCursorBounds(std::nullopt);
    camera_.reset();
    std::printf("[livezoom] zoom off\n");
    if (settings_.enabled && settings_.customCursorAlways) {
        updateIdleCursor(nowSeconds());
    }
}

void ZoomController::reset() {
    finish();
    hideIdleCursor(true);
}

void ZoomController::hideIdleCursor(bool restoreHardware) {
    if (idleOverlay_ && idleOverlay_->visible()) idleOverlay_->hide();
    if (idleCursorActive_) {
        std::printf("[livezoom] idle custom cursor off\n");
    }
    idleMonitor_ = nullptr;
    idleMotion_.reset();
    idleCursorActive_ = false;
    lastIdleRender_ = 0;
    if (restoreHardware && idleHardwareHidden_) {
        engine_.restoreSystemCursor();
        idleHardwareHidden_ = false;
    }
}

void ZoomController::updateIdleCursor(double now) {
    if (!settings_.enabled || !settings_.customCursorAlways) {
        hideIdleCursor(true);
        return;
    }
    if (!idleOverlay_) return;

    // The idle cursor owns the pointer; cancel the zoom-out cursor restore retries.
    cursorRestoreAt_ = 0;
    cursorRestoreTries_ = 0;

    POINT cursor{};
    GetCursorPos(&cursor);
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        hideIdleCursor(true);
        return;
    }

    if (idleMonitor_ != monitor || !idleOverlay_->visible()) {
        idleOverlay_->hide();
        idleMonitor_ = monitor;
        idleMonitorRect_ = info.rcMonitor;
        idleDisplay_ = {static_cast<lz::Real>(idleMonitorRect_.right - idleMonitorRect_.left),
                        static_cast<lz::Real>(idleMonitorRect_.bottom - idleMonitorRect_.top)};
        if (!idleOverlay_->showForMonitor(monitor)) {
            idleMonitor_ = nullptr;
            hideIdleCursor(true);
            return;
        }
        idleOverlay_->setMapping({0, 0}, 1.0, idleDisplay_);
        idleOverlay_->setCursorStyle(settings_.cursorStyle);
        idleOverlay_->setInk(settings_.inkLook());
        idleOverlay_->setClickEffect(settings_.clickEffect, settings_.clickRgba());
        const lz::Point local{cursor.x - idleMonitorRect_.left,
                              cursor.y - idleMonitorRect_.top};
        idleMotion_.emplace(local, now);
        idleMotion_->smoothTime = settings_.smoothCursor ? 0.06 : 0.0;
        idleMotion_->idleDelay = settings_.hideIdleCursor
                                     ? std::optional<double>(settings_.idleDelay)
                                     : std::nullopt;
        idleMotion_->jump(local, now);
        lastIdleRender_ = 0;
        idleCursorActive_ = true;
        std::printf("[livezoom] idle custom cursor on\n");
        if (!idleHardwareHidden_) {
            engine_.hideSystemCursor();
            idleHardwareHidden_ = true;
        }
    }

    const lz::Point local{cursor.x - idleMonitorRect_.left,
                          cursor.y - idleMonitorRect_.top};
    double dt = lastIdleRender_ > 0 ? now - lastIdleRender_ : 1.0 / 60.0;
    dt = std::clamp(dt, 1.0 / 240.0, 1.0 / 20.0);
    const bool buttonsDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
                             (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0 ||
                             (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
    if (idleMotion_) idleMotion_->update(local, buttonsDown, now, dt);
    idleOverlay_->setCursorStyle(settings_.cursorStyle);
    idleOverlay_->setCursor(local, settings_.cursorSize,
                            idleMotion_ ? idleMotion_->opacity : 1.0);
    idleOverlay_->render(now);
    lastIdleRender_ = now;

    engine_.touchCursorWatchdog();
    if (engine_.consumeWatchdogRestore()) {
        engine_.hideSystemCursor();
        idleHardwareHidden_ = true;
    }
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
    const double now = nowSeconds();
    if (cursorRestoreAt_ > 0 && now >= cursorRestoreAt_) {
        engine_.ensureCursorVisible();
        if (--cursorRestoreTries_ > 0) {
            cursorRestoreAt_ = now + 0.25;
        } else {
            cursorRestoreAt_ = 0;
        }
    }

    if (!active_ || !camera_) {
        updateIdleCursor(now);
        return;
    }

    const double dt = std::clamp(now - lastTick_, 1.0 / 240.0, 1.0 / 20.0);
    lastTick_ = now;

    POINT cursor{};
    GetCursorPos(&cursor);
    const lz::Point local{
        std::clamp(static_cast<lz::Real>(cursor.x - monitorRect_.left), 0.0, display_.width),
        std::clamp(static_cast<lz::Real>(cursor.y - monitorRect_.top), 0.0, display_.height)};
    camera_->update(local, now, dt);

    if (camera_->phase() == lz::Phase::Zoomed) {
        if (lastPhase_ != lz::Phase::Zoomed) {
            lastCameraCenter_ = camera_->center();
            followStartLogged_ = false;
        } else if (!followStartLogged_ && !(camera_->center() == lastCameraCenter_)) {
            const lz::Rect vp = camera_->viewport();
            const double fraction = (local.x - vp.x) / vp.width;
            std::printf("[livezoom] follow starts: cursor=(%.0f,%.0f) viewport=(%.0f,%.0f,"
                        "%.0f,%.0f) xFraction=%.3f\n",
                        local.x, local.y, vp.x, vp.y, vp.width, vp.height, fraction);
            followStartLogged_ = true;
        }
        lastCameraCenter_ = camera_->center();
    } else {
        lastCameraCenter_ = camera_->center();
    }
    lastPhase_ = camera_->phase();

    const lz::Rect viewport = shownViewport();
    if (overlay_) {
        overlay_->setMapping(viewport.origin(), camera_->zoom(), display_);
        // The hook also sends moves, but sampling here keeps the line continuous even when a
        // busy main thread or a mouse driver drops some of them.
        if (strokeDrawing_) {
            const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            overlay_->extendStroke(contentPoint(cursor.x, cursor.y), shift);
        }
        const bool buttonsDown =
            strokeDrawing_ || (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
        if (strokeDrawing_) {
            // Keep the pen tip exactly under the ink while drawing.
            cursorMotion_->jump(local, now);
        } else {
            cursorMotion_->update(local, buttonsDown, now, dt);
        }
        const lz::Point drawn = strokeDrawing_ ? local : cursorMotion_->position;
        const double zoom = camera_->zoom();
        const lz::Point shownCursor{(drawn.x - viewport.x) * zoom,
                                    (drawn.y - viewport.y) * zoom};
        overlay_->setCursor(shownCursor, zoom * settings_.cursorSize,
                            cursorMotion_->opacity);
        overlay_->setSpotlightCenter(shownCursor, 90.0 * zoom);
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
    // The physical click is delivered to the app at the real pointer position; the visible
    // feedback belongs at the magnified cursor, i.e. the same source point mapped by the
    // current viewport transform.
    const lz::Point content = contentPoint(x, y);
    const lz::Rect viewport = shownViewport();
    const double zoom = camera_ ? camera_->zoom() : 1.0;
    overlay_->addClick({(content.x - viewport.x) * zoom, (content.y - viewport.y) * zoom},
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

void ZoomController::highlight(int kindIndex) {
    if (!active_ || !overlay_ || !overlay_->visible()) return;
    if (kindIndex == 3) {
        overlay_->toggleSpotlight();
        input_.setHasStrokes(overlay_->hasContent());
        return;
    }
    lz::HighlightKind kind = lz::HighlightKind::Box;
    switch (kindIndex) {
        case 0: kind = lz::HighlightKind::Box; break;
        case 1: kind = lz::HighlightKind::Underline; break;
        case 2: kind = lz::HighlightKind::Fill; break;
        default: return;
    }
    if (!ocr_.available()) {
        std::printf("[livezoom] OCR helper not found\n");
        return;
    }

    POINT cursor{};
    GetCursorPos(&cursor);
    const lz::Rect viewport = shownViewport();
    const double zoom = camera_ ? camera_->zoom() : 1.0;
    const lz::Point cursorContent = contentPoint(cursor.x, cursor.y);
    const lz::Point mapped{(cursorContent.x - viewport.x) * zoom,
                           (cursorContent.y - viewport.y) * zoom};
    const uint64_t session = session_;
    std::printf("[livezoom] OCR highlight kind=%d at (%.0f,%.0f)\n", kindIndex, mapped.x,
                mapped.y);

    const RECT capture = monitorRect_;
    ocr_.recognize(capture, [this, kind, viewport, zoom, mapped, session](
                                std::vector<OcrLine> lines) {
        if (session != session_ || !active_ || !overlay_ || !overlay_->visible()) return;
        if (lines.empty()) {
            std::printf("[livezoom] OCR returned no lines\n");
            return;
        }
        const OcrLine* best = nullptr;
        double bestScore = 0;
        for (const OcrLine& line : lines) {
            const lz::Rect& box = line.box;
            if (mapped.y < box.minY() - box.height * 0.8 ||
                mapped.y > box.maxY() + box.height * 0.8) {
                continue;
            }
            const double vertical =
                std::max({box.minY() - mapped.y, mapped.y - box.maxY(), 0.0});
            const double gap =
                std::max({box.minX() - mapped.x, mapped.x - box.maxX(), 0.0});
            if (gap > box.height * 4) continue;
            const double score = vertical * 1000 + gap;
            if (!best || score < bestScore) {
                best = &line;
                bestScore = score;
            }
        }
        if (!best) {
            std::printf("[livezoom] OCR found no line at the cursor\n");
            return;
        }
        const lz::Rect content{
            viewport.x + best->box.x / zoom, viewport.y + best->box.y / zoom,
            best->box.width / zoom, best->box.height / zoom};
        overlay_->addHighlight(kind, content, zoom);
        input_.setHasStrokes(overlay_->hasContent());
        std::printf("[livezoom] highlighted line (%.0f,%.0f,%.0f,%.0f)\n", content.x,
                    content.y, content.width, content.height);
    });
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
    if (overlay_) {
        overlay_->setInk(settings_.inkLook());
        overlay_->setClickEffect(settings_.clickEffect, settings_.clickRgba());
        overlay_->setCursorStyle(settings_.cursorStyle);
    }
    if (cursorMotion_) {
        cursorMotion_->smoothTime = settings_.smoothCursor ? 0.06 : 0.0;
        cursorMotion_->idleDelay = settings_.hideIdleCursor
                                       ? std::optional<double>(settings_.idleDelay)
                                       : std::nullopt;
    }
    if (idleOverlay_ && idleOverlay_->visible()) {
        idleOverlay_->setCursorStyle(settings_.cursorStyle);
        idleOverlay_->setInk(settings_.inkLook());
        idleOverlay_->setClickEffect(settings_.clickEffect, settings_.clickRgba());
    }
    if (!active_) {
        if (!settings_.enabled || !settings_.customCursorAlways) {
            hideIdleCursor(true);
        } else {
            updateIdleCursor(nowSeconds());
        }
    }
    input_.setDrawingArmed(settings_.drawWhileZoomed && overlay_ && overlay_->visible());
}
