#pragma once

#include <windows.h>

#include <memory>
#include <optional>

#include "camera.h"
#include "settings.h"

class MagnificationEngine;
class InputHook;
class OverlayRenderer;

/// Ties the camera, the magnifier engine and the input hook together.
class ZoomController {
public:
    ZoomController(MagnificationEngine& engine, InputHook& input, lz::Settings& settings);
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
    void reloadSettings();

    [[nodiscard]] bool active() const { return active_; }

private:
    void startZoom();
    void finish();
    [[nodiscard]] double nowSeconds() const;
    [[nodiscard]] lz::Rect shownViewport() const;
    [[nodiscard]] lz::Point contentPoint(int x, int y) const;

    MagnificationEngine& engine_;
    InputHook& input_;
    lz::Settings& settings_;
    std::unique_ptr<OverlayRenderer> overlay_;

    std::optional<lz::ZoomCamera> camera_;
    HMONITOR monitor_ = nullptr;
    RECT monitorRect_{};
    lz::Size display_{};
    bool active_ = false;
    bool strokeDrawing_ = false;
    double lastTick_ = 0;
    LARGE_INTEGER frequency_{};
    LARGE_INTEGER start_{};
};
