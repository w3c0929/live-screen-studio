// Minimal proof of concept for global input handling on Windows.
//
// Verifies the pieces the macOS CGEventTap implementation relies on:
//   - swallowing the middle button and wheel,
//   - adjusting zoom while the middle button is held,
//   - confining the cursor with ClipCursor,
//   - replacing the system cursor with a transparent one, with a watchdog restore,
//   - injecting clicks later (SendInput is wired in the full app; this PoC logs the gesture).
//
// Controls:
//   middle click       toggle "zoomed" state
//   middle + wheel     adjust zoom
//   F2                 toggle
//   Esc                leave the zoomed state
//
// Usage:
//   input_poc [--seconds N] [--dry-run] [--blank]

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define OEMRESOURCE

#include <windows.h>

#ifndef OCR_HELP
#define OCR_HELP 32651
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace {

constexpr UINT_PTR kTimerId = 1;

std::atomic<bool> g_active{false};
std::atomic<bool> g_stop{false};
std::atomic<uint64_t> g_heartbeat{0};
std::atomic<bool> g_cleanedUp{false};
std::mutex g_cursorMutex;

HHOOK g_mouseHook = nullptr;
HHOOK g_keyboardHook = nullptr;
bool g_middleDown = false;
bool g_dryRun = false;
bool g_blankCursor = false;
bool g_acceptInjected = false;
bool g_blanked = false;
float g_zoom = 2.0f;
uint64_t g_quitAt = 0;
RECT g_monitorRect{};

void logLine(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    std::printf("\n");
    std::fflush(stdout);
    va_end(args);
}

bool setTransparentCursor(DWORD cursorId) {
    constexpr int kSize = 32;
    BYTE andMask[kSize * kSize / 8];
    BYTE xorMask[kSize * kSize / 8];
    std::memset(andMask, 0xFF, sizeof(andMask));
    std::memset(xorMask, 0, sizeof(xorMask));
    HCURSOR cursor = CreateCursor(GetModuleHandleW(nullptr), 0, 0, kSize, kSize, andMask, xorMask);
    if (!cursor) return false;
    // SetSystemCursor destroys the cursor it is given, so every id needs its own copy.
    if (!SetSystemCursor(cursor, cursorId)) {
        DestroyCursor(cursor);
        return false;
    }
    return true;
}

void blankSystemCursors() {
    std::lock_guard lock(g_cursorMutex);
    if (g_blanked) return;
    const DWORD ids[] = {
        OCR_NORMAL, OCR_IBEAM, OCR_WAIT, OCR_CROSS, OCR_UP,
        OCR_SIZENWSE, OCR_SIZENESW, OCR_SIZEWE, OCR_SIZENS, OCR_SIZEALL,
        OCR_NO, OCR_HAND, OCR_APPSTARTING, OCR_HELP,
    };
    bool any = false;
    for (const DWORD id : ids) any = setTransparentCursor(id) || any;
    g_blanked = any;
    if (!any) logLine("[input_poc] could not replace the system cursor");
}

void restoreSystemCursors() {
    std::lock_guard lock(g_cursorMutex);
    if (!g_blanked) return;
    SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, SPIF_SENDCHANGE);
    g_blanked = false;
}

void toggleActive() {
    if (!g_active.load()) {
        POINT cursor{};
        GetCursorPos(&cursor);
        const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info)) g_monitorRect = info.rcMonitor;
        ClipCursor(&g_monitorRect);
        if (g_blankCursor) blankSystemCursors();
        g_heartbeat = GetTickCount64();
        g_active = true;
        logLine("[input_poc] active: cursor confined to (%ld,%ld)-(%ld,%ld)",
                g_monitorRect.left, g_monitorRect.top, g_monitorRect.right, g_monitorRect.bottom);
    } else {
        g_active = false;
        ClipCursor(nullptr);
        if (g_blankCursor) restoreSystemCursors();
        logLine("[input_poc] inactive: cursor released");
    }
}

void adjustZoom(float factor) {
    g_zoom = std::clamp(g_zoom * factor, 1.25f, 4.0f);
    logLine("[input_poc] zoom=%.2fx", g_zoom);
}

void watchdogLoop() {
    while (!g_stop.load()) {
        Sleep(250);
        if (!g_active.load()) continue;
        const uint64_t heartbeat = g_heartbeat.load();
        if (GetTickCount64() - heartbeat <= 2000) continue;
        g_active = false;
        ClipCursor(nullptr);
        if (g_blankCursor) restoreSystemCursors();
        logLine("[input_poc] watchdog: main thread stalled; cursor restored");
    }
}

LRESULT CALLBACK mouseHookProc(int code, WPARAM message, LPARAM lParam) {
    if (code == HC_ACTION) {
        const auto* mouse = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        if (!(mouse->flags & LLMHF_INJECTED) || g_acceptInjected) {
            switch (message) {
                case WM_MBUTTONDOWN:
                    if (!(GetAsyncKeyState(VK_MENU) & 0x8000)) {
                        g_middleDown = true;
                        toggleActive();
                        if (!g_dryRun) return 1;
                    }
                    break;
                case WM_MBUTTONUP:
                    if (g_middleDown) {
                        g_middleDown = false;
                        if (!g_dryRun) return 1;
                    }
                    break;
                case WM_MOUSEWHEEL:
                    if (g_middleDown && g_active.load()) {
                        const int delta = static_cast<short>(HIWORD(mouse->mouseData));
                        adjustZoom(std::exp(static_cast<float>(delta) / 120.0f * 0.1f));
                        if (!g_dryRun) return 1;
                    }
                    break;
                case WM_LBUTTONDOWN:
                    if (g_active.load()) {
                        logLine("[input_poc] left down at (%ld,%ld)",
                                mouse->pt.x, mouse->pt.y);
                    }
                    break;
                case WM_XBUTTONDOWN:
                    if (g_active.load()) {
                        logLine("[input_poc] side button %u",
                                static_cast<unsigned>(HIWORD(mouse->mouseData)));
                    }
                    break;
                default:
                    break;
            }
        }
    }
    return CallNextHookEx(nullptr, code, message, lParam);
}

LRESULT CALLBACK keyboardHookProc(int code, WPARAM message, LPARAM lParam) {
    if (code == HC_ACTION) {
        const auto* keyboard = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if ((!(keyboard->flags & LLKHF_INJECTED) || g_acceptInjected) &&
            (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)) {
            if (keyboard->vkCode == VK_ESCAPE && g_active.load()) {
                toggleActive();
                if (!g_dryRun) return 1;
            } else if (keyboard->vkCode == VK_F2) {
                toggleActive();
                if (!g_dryRun) return 1;
            }
        }
    }
    return CallNextHookEx(nullptr, code, message, lParam);
}

void cleanup() {
    if (g_cleanedUp.exchange(true)) return;
    g_stop = true;
    if (g_mouseHook) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }
    if (g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
    KillTimer(nullptr, kTimerId);
    g_active = false;
    ClipCursor(nullptr);
    if (g_blankCursor) restoreSystemCursors();
}

BOOL WINAPI consoleCtrlHandler(DWORD) {
    cleanup();
    return FALSE;
}

bool parseOptions(int argc, char** argv, double& seconds) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--seconds" && i + 1 < argc) {
            seconds = std::stod(argv[++i]);
        } else if (arg == "--dry-run") {
            g_dryRun = true;
        } else if (arg == "--blank") {
            g_blankCursor = true;
        } else if (arg == "--accept-injected") {
            g_acceptInjected = true;
        } else if (arg == "--help" || arg == "-h") {
            std::printf("usage: input_poc [--seconds N] [--dry-run] [--blank] "
                        "[--accept-injected]\n");
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    double seconds = 0;
    if (!parseOptions(argc, argv, seconds)) return 1;

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, mouseHookProc, instance, 0);
    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHookProc, instance, 0);
    if (!g_mouseHook || !g_keyboardHook) {
        logLine("[input_poc] SetWindowsHookEx failed: %lu", GetLastError());
        cleanup();
        return 1;
    }

    std::thread watchdog(watchdogLoop);
    if (seconds > 0) {
        g_quitAt = GetTickCount64() + static_cast<uint64_t>(seconds * 1000);
    }
    SetTimer(nullptr, kTimerId, 16, nullptr);
    logLine("[input_poc] ready (dry-run=%d, blank=%d); middle click toggles",
            g_dryRun ? 1 : 0, g_blankCursor ? 1 : 0);

    MSG message{};
    bool running = true;
    while (running && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_TIMER && message.hwnd == nullptr) {
            g_heartbeat = GetTickCount64();
            if (g_quitAt && g_heartbeat.load() >= g_quitAt) {
                running = false;
                break;
            }
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    cleanup();
    g_stop = true;
    if (watchdog.joinable()) watchdog.join();
    logLine("[input_poc] done");
    return 0;
}
