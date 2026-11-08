#pragma once

#include <windows.h>
#include <magnification.h>

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "geometry.h"

/// Owns the full-screen WC_MAGNIFIER host used as the zoom engine.
///
/// The magnifier reads the live desktop behind it (excluding itself), so LiveZoom never captures
/// its own output and OBS display capture sees exactly what the presenter sees.
class MagnificationEngine {
public:
    MagnificationEngine() = default;
    ~MagnificationEngine();
    MagnificationEngine(const MagnificationEngine&) = delete;
    MagnificationEngine& operator=(const MagnificationEngine&) = delete;

    bool initialize(std::string& error);
    void shutdown();

    /// Shows the magnifier over `monitor`, recreating the host if it was on another display.
    bool showForMonitor(HMONITOR monitor);
    void hide();

    /// `sourceTopLeft` is the top-left of the visible display area, in virtual-screen pixels.
    void setViewport(lz::Point sourceTopLeft, double zoom);
    void setSmoothing(bool smooth);
    void setExcludeObs(bool exclude);
    /// The ink/cursor overlay must never be magnified, or it would be drawn twice.
    void setOverlayWindow(HWND overlay);
    /// Asks the system to show the cursor again; safe to call repeatedly.
    void ensureCursorVisible();
    /// Re-enumerates OBS windows (it can start or move while LiveZoom is running).
    void refreshExcludedWindows();

    [[nodiscard]] bool visible() const { return visible_; }
    [[nodiscard]] HWND host() const { return host_; }
    [[nodiscard]] const RECT& monitorRect() const { return monitorRect_; }

private:
    struct Api {
        HMODULE module = nullptr;
        BOOL(WINAPI* initialize)() = nullptr;
        BOOL(WINAPI* uninitialize)() = nullptr;
        BOOL(WINAPI* setWindowTransform)(HWND, PMAGTRANSFORM) = nullptr;
        BOOL(WINAPI* setWindowFilterList)(HWND, DWORD, int, HWND*) = nullptr;
        BOOL(WINAPI* showSystemCursor)(BOOL) = nullptr;
        BOOL(WINAPI* setLensUseBitmapSmoothing)(HWND, BOOL) = nullptr;
    };

    static LRESULT CALLBACK hostProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    bool createHost(HMONITOR monitor);
    void destroyHost();
    void showHost();
    void hideHardwareCursor(bool hide);
    void blankSystemCursors();
    void registerHostClass();

    Api api_;
    bool initialized_ = false;
    HWND host_ = nullptr;
    HWND mag_ = nullptr;
    HMONITOR monitor_ = nullptr;
    RECT monitorRect_{};
    bool visible_ = false;
    bool smoothing_ = false;
    bool excludeObs_ = true;
    HWND overlay_ = nullptr;
    bool hardwareCursorHidden_ = false;
    lz::Point viewportSource_{};
    double viewportZoom_ = 0;
    bool viewportValid_ = false;
    std::mutex cursorMutex_;
    std::vector<HWND> excluded_;
    bool classRegistered_ = false;
};
