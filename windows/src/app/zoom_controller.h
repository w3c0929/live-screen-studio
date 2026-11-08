#pragma once

#include <windows.h>

#include <cstdint>
#include <memory>
#include <optional>

#include "camera.h"
#include "cursor_motion.h"
#include "settings.h"

class MagnificationEngine;
class InputHook;
class OcrService;
class OverlayRenderer;

/// Ties the camera, the magnifier engine and the input hook together.
class ZoomController {
public:
    ZoomController(MagnificationEngine& engine, InputHook& input, OcrService& ocr,
                   lz::Settings& settings);
    ~ZoomController();

    void toggle();
    void adjustZoom(double steps);
    void tick();
    void reset();

    void handleClick(int x, int y);
    void beginStroke(int x, int y);
    void moveStroke(int x, int y, bool straight);
    void endStroke();
    void cancelStroke();
    void undoStroke();
    void clearStrokes();
    void highlight(int kind);
    void reloadSettings();

    [[nodiscard]] bool active() const { return active_; }

private:
    void startZoom();
    void finish();
    void updateIdleCursor(double now);
    void hideIdleCursor(bool restoreHardware);
    [[nodiscard]] double nowSeconds() const;
    [[nodiscard]] lz::Rect shownViewport() const;
    [[nodiscard]] lz::Point contentPoint(int x, int y) const;

    MagnificationEngine& engine_;
    InputHook& input_;
    OcrService& ocr_;
    lz::Settings& settings_;
    std::unique_ptr<OverlayRenderer> overlay_;
    std::unique_ptr<OverlayRenderer> idleOverlay_;
    std::optional<lz::CursorMotion> idleMotion_;
    HMONITOR idleMonitor_ = nullptr;
    RECT idleMonitorRect_{};
    lz::Size idleDisplay_{};
    bool idleCursorActive_ = false;
    bool idleHardwareHidden_ = false;
    double lastIdleRender_ = 0;

    std::optional<lz::ZoomCamera> camera_;
    std::optional<lz::CursorMotion> cursorMotion_;
    HMONITOR monitor_ = nullptr;
    RECT monitorRect_{};
    lz::Size display_{};
    bool active_ = false;
    bool strokeDrawing_ = false;
    uint64_t session_ = 0;
    double lastTick_ = 0;
    double cursorRestoreAt_ = 0;
    int cursorRestoreTries_ = 0;
    lz::Point lastCameraCenter_{};
    lz::Phase lastPhase_ = lz::Phase::Idle;
    bool followStartLogged_ = false;
    LARGE_INTEGER frequency_{};
    LARGE_INTEGER start_{};
};
