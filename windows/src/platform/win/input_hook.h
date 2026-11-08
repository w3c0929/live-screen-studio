#pragma once

#include <windows.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>

#include "geometry.h"

inline constexpr UINT kLiveZoomToggle = WM_APP + 1;
inline constexpr UINT kLiveZoomAdjust = WM_APP + 2;
inline constexpr UINT kLiveZoomEscape = WM_APP + 3;
inline constexpr UINT kLiveZoomStrokeBegin = WM_APP + 5;
inline constexpr UINT kLiveZoomStrokeMove = WM_APP + 6;
inline constexpr UINT kLiveZoomStrokeEnd = WM_APP + 7;
inline constexpr UINT kLiveZoomUndo = WM_APP + 8;
inline constexpr UINT kLiveZoomClear = WM_APP + 9;
inline constexpr UINT kLiveZoomClick = WM_APP + 10;
inline constexpr UINT kLiveZoomStrokeCancel = WM_APP + 11;
inline constexpr UINT kLiveZoomHighlight = WM_APP + 13;
inline constexpr UINT kLiveZoomTray = WM_APP + 100;

/// Heap-allocated stroke point delivered from the hook thread; the UI thread deletes it.
struct StrokeEvent {
    int x = 0;
    int y = 0;
    bool straight = false;
};

/// Global low-level mouse/keyboard hooks on their own thread.
///
/// The hook thread only classifies input and posts small messages to the UI thread, so a busy
/// main thread never delays anyone's clicks. The wheel value is sent in thousandths of a notch.
class InputHook {
public:
    InputHook() = default;
    ~InputHook();
    InputHook(const InputHook&) = delete;
    InputHook& operator=(const InputHook&) = delete;

    bool start(HWND target);
    void stop();

    void setEnabled(bool enabled) { enabled_ = enabled; }
    void setZoomActive(bool active);
    /// Whether a left drag should draw instead of reaching the app.
    void setDrawingArmed(bool armed);
    /// Whether there are strokes or highlights to undo/clear.
    void setHasStrokes(bool has) { hasStrokes_ = has; }
    /// Test/demo hook: accept SendInput/mouse_event events too.
    void setAcceptInjected(bool accept) { acceptInjected_ = accept; }
    void setCursorBounds(std::optional<lz::Rect> bounds);

    [[nodiscard]] bool running() const { return installed_; }

private:
    enum : UINT { kApplyCursorBounds = WM_APP + 20 };

    static LRESULT CALLBACK mouseProc(int code, WPARAM message, LPARAM lParam);
    static LRESULT CALLBACK keyboardProc(int code, WPARAM message, LPARAM lParam);

    void threadMain();
    void applyCursorBounds();
    void postStroke(UINT message, POINT point, bool straight);
    void postPoint(UINT message, POINT point);
    void replayClick(POINT point);

    static InputHook* current_;

    HWND target_ = nullptr;
    std::thread thread_;
    DWORD threadId_ = 0;
    HANDLE ready_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<bool> installed_{false};
    std::atomic<bool> enabled_{true};
    std::atomic<bool> zoomActive_{false};
    std::atomic<bool> drawingArmed_{false};
    std::atomic<bool> hasStrokes_{false};
    std::atomic<bool> acceptInjected_{false};
    bool middleDown_ = false;

    enum class LeftState {
        Idle,
        Drawing,
        Passthrough,
    };
    LeftState leftState_ = LeftState::Idle;
    POINT downPoint_{};
    int moveCount_ = 0;
    bool ctrlAtDown_ = false;
    bool swallowRightUp_ = false;
    bool highlightHeld_[4] = {false, false, false, false};
    HHOOK mouseHook_ = nullptr;
    HHOOK keyboardHook_ = nullptr;

    std::mutex boundsMutex_;
    std::optional<lz::Rect> pendingBounds_;
};
