#include "input_hook.h"

#include <cmath>
#include <cstdio>
#include <new>

namespace {
constexpr ULONG_PTR kSyntheticMarker = 0x4C5A0001;
constexpr int kDragThreshold = 4;

BOOL CALLBACK collectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM param) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
        auto* rect = reinterpret_cast<RECT*>(param);
        if (IsRectEmpty(rect)) {
            *rect = info.rcMonitor;
        } else {
            UnionRect(rect, rect, &info.rcMonitor);
        }
    }
    return TRUE;
}

RECT physicalVirtualScreen() {
    RECT rect{};
    EnumDisplayMonitors(nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>(&rect));
    return rect;
}
}  // namespace

InputHook* InputHook::current_ = nullptr;

InputHook::~InputHook() {
    stop();
}

bool InputHook::start(HWND target) {
    if (thread_.joinable()) return installed_;
    current_ = this;
    target_ = target;
    running_ = true;
    installed_ = false;
    ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ready_) return false;
    thread_ = std::thread(&InputHook::threadMain, this);
    WaitForSingleObject(ready_, 3000);
    CloseHandle(ready_);
    ready_ = nullptr;
    return installed_;
}

void InputHook::stop() {
    running_ = false;
    if (threadId_ != 0) {
        PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
    }
    if (thread_.joinable()) thread_.join();
    if (ready_) {
        CloseHandle(ready_);
        ready_ = nullptr;
    }
    ClipCursor(nullptr);
    current_ = nullptr;
}

void InputHook::setZoomActive(bool active) {
    zoomActive_ = active;
}

void InputHook::setDrawingArmed(bool armed) {
    drawingArmed_ = armed;
}

void InputHook::threadMain() {
    threadId_ = GetCurrentThreadId();
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, mouseProc, instance, 0);
    keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, instance, 0);
    installed_ = mouseHook_ && keyboardHook_;
    if (!installed_) {
        std::printf("[livezoom] SetWindowsHookEx failed: %lu\n", GetLastError());
        if (mouseHook_) UnhookWindowsHookEx(mouseHook_);
        if (keyboardHook_) UnhookWindowsHookEx(keyboardHook_);
        mouseHook_ = nullptr;
        keyboardHook_ = nullptr;
        if (ready_) SetEvent(ready_);
        threadId_ = 0;
        return;
    }
    if (ready_) SetEvent(ready_);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == kApplyCursorBounds) {
            applyCursorBounds();
        }
    }

    if (mouseHook_) UnhookWindowsHookEx(mouseHook_);
    if (keyboardHook_) UnhookWindowsHookEx(keyboardHook_);
    mouseHook_ = nullptr;
    keyboardHook_ = nullptr;
    ClipCursor(nullptr);
    threadId_ = 0;
}

void InputHook::setCursorBounds(std::optional<lz::Rect> bounds) {
    {
        std::lock_guard lock(boundsMutex_);
        pendingBounds_ = bounds;
    }
    if (threadId_ != 0) {
        PostThreadMessageW(threadId_, kApplyCursorBounds, 0, 0);
    } else {
        applyCursorBounds();
    }
}

void InputHook::applyCursorBounds() {
    std::optional<lz::Rect> bounds;
    {
        std::lock_guard lock(boundsMutex_);
        bounds = pendingBounds_;
    }
    if (!bounds) {
        ClipCursor(nullptr);
        return;
    }
    RECT rect{};
    rect.left = static_cast<LONG>(std::lround(bounds->minX()));
    rect.top = static_cast<LONG>(std::lround(bounds->minY()));
    rect.right = static_cast<LONG>(std::lround(bounds->maxX()));
    rect.bottom = static_cast<LONG>(std::lround(bounds->maxY()));
    ClipCursor(&rect);
}

void InputHook::postStroke(UINT message, POINT point, bool straight) {
    auto* event = new (std::nothrow) StrokeEvent{point.x, point.y, straight};
    if (!event) return;
    if (!PostMessageW(target_, message, 0, reinterpret_cast<LPARAM>(event))) {
        delete event;
    }
}

void InputHook::postPoint(UINT message, POINT point) {
    auto* data = new (std::nothrow) POINT(point);
    if (!data) return;
    if (!PostMessageW(target_, message, 0, reinterpret_cast<LPARAM>(data))) {
        delete data;
    }
}

void InputHook::replayClick(POINT point) {
    // GetSystemMetrics is DPI-virtualized; monitor rects from GetMonitorInfo are physical,
    // which is the space SendInput absolute coordinates use with MOUSEEVENTF_VIRTUALDESK.
    const RECT screen = physicalVirtualScreen();
    const int vw = screen.right - screen.left;
    const int vh = screen.bottom - screen.top;
    if (vw <= 1 || vh <= 1) return;
    const LONG dx = (point.x - screen.left) * 65535 / (vw - 1);
    const LONG dy = (point.y - screen.top) * 65535 / (vh - 1);

    INPUT inputs[2]{};
    for (INPUT& input : inputs) {
        input.type = INPUT_MOUSE;
        input.mi.dx = dx;
        input.mi.dy = dy;
        input.mi.dwExtraInfo = kSyntheticMarker;
    }
    inputs[0].mi.dwFlags =
        MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_LEFTDOWN;
    inputs[1].mi.dwFlags =
        MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_LEFTUP;
    SendInput(2, inputs, sizeof(INPUT));
}

LRESULT CALLBACK InputHook::mouseProc(int code, WPARAM message, LPARAM lParam) {
    InputHook* self = current_;
    if (code == HC_ACTION && self && self->running_ && self->enabled_) {
        const auto* mouse = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        if (mouse->dwExtraInfo == kSyntheticMarker) {
            return CallNextHookEx(nullptr, code, message, lParam);
        }
        if (!(mouse->flags & LLMHF_INJECTED) || self->acceptInjected_) {
            if (!self->drawingArmed_ && self->leftState_ != LeftState::Idle) {
                self->leftState_ = LeftState::Idle;
                self->swallowRightUp_ = false;
            }
            const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            switch (message) {
                case WM_MBUTTONDOWN:
                    if (!(GetAsyncKeyState(VK_MENU) & 0x8000)) {
                        self->middleDown_ = true;
                        PostMessageW(self->target_, kLiveZoomToggle, 0, 0);
                        return 1;
                    }
                    break;
                case WM_MBUTTONUP:
                    if (self->middleDown_) {
                        self->middleDown_ = false;
                        return 1;
                    }
                    break;
                case WM_MOUSEWHEEL:
                    if (self->middleDown_) {
                        const int delta = static_cast<short>(HIWORD(mouse->mouseData));
                        const double steps = static_cast<double>(delta) / 120.0;
                        PostMessageW(self->target_, kLiveZoomAdjust,
                                     static_cast<WPARAM>(std::lround(steps * 1000.0)), 0);
                        return 1;
                    }
                    break;
                case WM_LBUTTONDOWN:
                    if (self->drawingArmed_) {
                        self->ctrlAtDown_ =
                            (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                        if (self->ctrlAtDown_) {
                            self->leftState_ = LeftState::Passthrough;
                        } else {
                            self->leftState_ = LeftState::Drawing;
                            // GetCursorPos is the authoritative physical position; the hook
                            // point can be DPI-virtualized in some driver setups.
                            POINT down = mouse->pt;
                            GetCursorPos(&down);
                            self->downPoint_ = down;
                            self->moveCount_ = 0;
                            std::printf("[hook] down (%ld,%ld)\n", down.x, down.y);
                            self->postStroke(kLiveZoomStrokeBegin, down, shift);
                            return 1;
                        }
                    } else {
                        self->postPoint(kLiveZoomClick, mouse->pt);
                    }
                    break;
                case WM_MOUSEMOVE:
                    if (self->leftState_ == LeftState::Drawing) {
                        ++self->moveCount_;
                        // Points are sampled from GetCursorPos in the UI thread every frame.
                        // Let the cursor move; swallowing moves freezes the pointer and makes
                        // every later sample identical.
                    }
                    break;
                case WM_LBUTTONUP:
                    if (self->leftState_ == LeftState::Drawing) {
                        POINT up = mouse->pt;
                        GetCursorPos(&up);
                        const int dx = up.x - self->downPoint_.x;
                        const int dy = up.y - self->downPoint_.y;
                        self->leftState_ = LeftState::Idle;
                        std::printf("[hook] up dist=%d moves=%d\n",
                                    static_cast<int>(std::lround(std::hypot(dx, dy))),
                                    self->moveCount_);
                        if (dx * dx + dy * dy <
                            kDragThreshold * kDragThreshold) {
                            PostMessageW(self->target_, kLiveZoomStrokeCancel, 0, 0);
                            self->replayClick(self->downPoint_);
                            self->postPoint(kLiveZoomClick, self->downPoint_);
                        } else {
                            self->postStroke(kLiveZoomStrokeMove, up, shift);
                            PostMessageW(self->target_, kLiveZoomStrokeEnd, 0, 0);
                        }
                        return 1;
                    }
                    if (self->leftState_ == LeftState::Passthrough) {
                        self->leftState_ = LeftState::Idle;
                    }
                    break;
                case WM_RBUTTONDOWN:
                    if (self->drawingArmed_ && self->hasStrokes_) {
                        self->swallowRightUp_ = true;
                        PostMessageW(self->target_, kLiveZoomUndo, 0, 0);
                        return 1;
                    }
                    self->postPoint(kLiveZoomClick, mouse->pt);
                    break;
                case WM_RBUTTONUP:
                    if (self->swallowRightUp_) {
                        self->swallowRightUp_ = false;
                        return 1;
                    }
                    break;
                default:
                    break;
            }
        }
    }
    return CallNextHookEx(nullptr, code, message, lParam);
}

LRESULT CALLBACK InputHook::keyboardProc(int code, WPARAM message, LPARAM lParam) {
    InputHook* self = current_;
    if (code == HC_ACTION && self && self->running_ && self->enabled_) {
        const auto* keyboard = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if ((!(keyboard->flags & LLKHF_INJECTED) || self->acceptInjected_) &&
            keyboard->dwExtraInfo != kSyntheticMarker &&
            (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)) {
            if (keyboard->vkCode == VK_ESCAPE && self->zoomActive_ &&
                self->hasStrokes_) {
                PostMessageW(self->target_, kLiveZoomClear, 0, 0);
                return 1;
            }
        }
    }
    return CallNextHookEx(nullptr, code, message, lParam);
}
