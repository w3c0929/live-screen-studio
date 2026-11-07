#include "input_hook.h"

#include <cmath>
#include <cstdio>
#include <new>

namespace {
constexpr ULONG_PTR kSyntheticMarker = 0x4C5A0001;
constexpr int kDragThreshold = 4;
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
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 1 || vh <= 1) return;
    const LONG dx = (point.x - vx) * 65535 / (vw - 1);
    const LONG dy = (point.y - vy) * 65535 / (vh - 1);

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
                            self->leftState_ = LeftState::Pending;
                            self->downPoint_ = mouse->pt;
                            return 1;
                        }
                    } else {
                        self->postPoint(kLiveZoomClick, mouse->pt);
                    }
                    break;
                case WM_MOUSEMOVE:
                    if (self->leftState_ == LeftState::Pending) {
                        const int dx = mouse->pt.x - self->downPoint_.x;
                        const int dy = mouse->pt.y - self->downPoint_.y;
                        if (dx * dx + dy * dy >= kDragThreshold * kDragThreshold) {
                            self->leftState_ = LeftState::Drawing;
                            self->postStroke(kLiveZoomStrokeBegin, self->downPoint_, shift);
                            self->postStroke(kLiveZoomStrokeMove, mouse->pt, shift);
                            return 1;
                        }
                    } else if (self->leftState_ == LeftState::Drawing) {
                        self->postStroke(kLiveZoomStrokeMove, mouse->pt, shift);
                        return 1;
                    }
                    break;
                case WM_LBUTTONUP:
                    if (self->leftState_ == LeftState::Pending) {
                        const POINT point = self->downPoint_;
                        self->leftState_ = LeftState::Idle;
                        self->replayClick(point);
                        self->postPoint(kLiveZoomClick, point);
                        return 1;
                    }
                    if (self->leftState_ == LeftState::Drawing) {
                        self->leftState_ = LeftState::Idle;
                        PostMessageW(self->target_, kLiveZoomStrokeEnd, 0, 0);
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
