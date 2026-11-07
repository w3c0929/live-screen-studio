// Minimal proof of concept for the Windows zoom engine.
//
// A full-screen topmost host window contains a WC_MAGNIFIER child. The magnifier reads the live
// desktop behind it (excluding itself), so LiveZoom never captures its own output. OBS display
// capture sees the magnified result, just like the macOS ScreenCaptureKit implementation.
//
// Controls:
//   middle click       toggle zoom
//   middle + wheel     adjust zoom (1.25x .. 4x)
//   Esc                quit
//   --zoom N --seconds N --smooth --no-obs-filter --keep-hardware-cursor --no-auto-activate

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <magnification.h>

// MinGW's magnification.h defines the structs but not the window class constants.
#ifndef WC_MAGNIFIER
#define WC_MAGNIFIER L"Magnifier"
#endif
#ifndef MS_SHOWMAGNIFIEDCURSOR
#define MS_SHOWMAGNIFIEDCURSOR 0x0001
#endif

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kHostClass[] = L"LiveZoomMagnifierPoCHost";
constexpr UINT_PTR kTimerId = 1;

struct MagnifierApi {
    HMODULE module = nullptr;
    BOOL(WINAPI* initialize)() = nullptr;
    BOOL(WINAPI* uninitialize)() = nullptr;
    BOOL(WINAPI* setWindowTransform)(HWND, PMAGTRANSFORM) = nullptr;
    BOOL(WINAPI* setWindowFilterList)(HWND, DWORD, int, HWND*) = nullptr;
    BOOL(WINAPI* showSystemCursor)(BOOL) = nullptr;
    BOOL(WINAPI* setLensUseBitmapSmoothing)(HWND, BOOL) = nullptr;
};

struct Options {
    float zoom = 2.0f;
    double seconds = 0;
    bool smooth = false;
    bool excludeObs = true;
    bool keepHardwareCursor = false;
    bool autoActivate = true;
};

struct App {
    HINSTANCE instance = nullptr;
    HWND host = nullptr;
    HWND mag = nullptr;
    HMONITOR monitor = nullptr;
    RECT monitorRect{};
    bool active = false;
    bool middleDown = false;
    float zoom = 2.0f;
    POINT lastSource{-100000, -100000};
    uint64_t quitAt = 0;
    uint64_t lastFpsTick = 0;
    int frames = 0;
    bool cursorHidden = false;
    std::vector<HWND> excluded;
};

MagnifierApi g_api;
Options g_options;
App g_app;
HHOOK g_mouseHook = nullptr;
bool g_cleanedUp = false;

void logLine(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    std::printf("\n");
    std::fflush(stdout);
    va_end(args);
}

bool loadMagnifierApi() {
    g_api.module = LoadLibraryW(L"magnification.dll");
    if (!g_api.module) {
        logLine("[mag_poc] could not load magnification.dll");
        return false;
    }
    auto get = [](const char* name) { return GetProcAddress(g_api.module, name); };
    g_api.initialize = reinterpret_cast<decltype(g_api.initialize)>(get("MagInitialize"));
    g_api.uninitialize = reinterpret_cast<decltype(g_api.uninitialize)>(get("MagUninitialize"));
    g_api.setWindowTransform =
        reinterpret_cast<decltype(g_api.setWindowTransform)>(get("MagSetWindowTransform"));
    g_api.setWindowFilterList =
        reinterpret_cast<decltype(g_api.setWindowFilterList)>(get("MagSetWindowFilterList"));
    g_api.showSystemCursor =
        reinterpret_cast<decltype(g_api.showSystemCursor)>(get("MagShowSystemCursor"));
    g_api.setLensUseBitmapSmoothing =
        reinterpret_cast<decltype(g_api.setLensUseBitmapSmoothing)>(
            get("MagSetLensUseBitmapSmoothing"));

    if (!g_api.initialize || !g_api.uninitialize || !g_api.setWindowTransform) {
        logLine("[mag_poc] magnification.dll is missing required exports");
        return false;
    }
    return true;
}

std::wstring processBaseNameLower(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    wchar_t buffer[2048];
    DWORD length = static_cast<DWORD>(std::size(buffer));
    std::wstring result;
    if (QueryFullProcessImageNameW(process, 0, buffer, &length)) {
        result.assign(buffer, length);
        const size_t slash = result.find_last_of(L"\\/");
        if (slash != std::wstring::npos) result = result.substr(slash + 1);
        std::transform(result.begin(), result.end(), result.begin(),
                       [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    }
    CloseHandle(process);
    return result;
}

BOOL CALLBACK enumObsWindows(HWND hwnd, LPARAM param) {
    auto* out = reinterpret_cast<std::vector<HWND>*>(param);
    if (!IsWindowVisible(hwnd)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || pid == GetCurrentProcessId()) return TRUE;
    const std::wstring name = processBaseNameLower(pid);
    if (name == L"obs64.exe" || name == L"obs32.exe" || name == L"obs.exe") {
        out->push_back(hwnd);
    }
    return TRUE;
}

void applyWindowFilter() {
    if (!g_api.setWindowFilterList || !g_app.mag) return;
    g_app.excluded.clear();
    if (g_options.excludeObs) {
        EnumWindows(enumObsWindows, reinterpret_cast<LPARAM>(&g_app.excluded));
    }
    if (g_app.excluded.empty()) {
        g_api.setWindowFilterList(g_app.mag, MW_FILTERMODE_EXCLUDE, 0, nullptr);
    } else {
        g_api.setWindowFilterList(g_app.mag, MW_FILTERMODE_EXCLUDE,
                                  static_cast<int>(g_app.excluded.size()),
                                  g_app.excluded.data());
    }
    if (!g_app.excluded.empty()) {
        logLine("[mag_poc] excluding %zu OBS window(s) from the magnified source",
                g_app.excluded.size());
    }
}

void updateTransform() {
    if (!g_app.active || !g_app.mag) return;
    POINT cursor{};
    GetCursorPos(&cursor);
    const RECT& r = g_app.monitorRect;
    const float width = static_cast<float>(r.right - r.left);
    const float height = static_cast<float>(r.bottom - r.top);
    const float zoom = g_app.zoom;
    const float visibleWidth = width / zoom;
    const float visibleHeight = height / zoom;

    float sourceX = static_cast<float>(cursor.x) - visibleWidth / 2;
    float sourceY = static_cast<float>(cursor.y) - visibleHeight / 2;
    sourceX = std::clamp(sourceX, static_cast<float>(r.left),
                         static_cast<float>(r.right) - visibleWidth);
    sourceY = std::clamp(sourceY, static_cast<float>(r.top),
                         static_cast<float>(r.bottom) - visibleHeight);
    // Whole physical pixels keep the magnified image from resampling every frame.
    sourceX = std::round(sourceX);
    sourceY = std::round(sourceY);
    sourceX = std::min(sourceX, static_cast<float>(r.right) - visibleWidth);
    sourceY = std::min(sourceY, static_cast<float>(r.bottom) - visibleHeight);

    MAGTRANSFORM matrix{};
    matrix.v[0][0] = zoom;
    matrix.v[0][1] = 0;
    matrix.v[0][2] = -sourceX * zoom;
    matrix.v[1][0] = 0;
    matrix.v[1][1] = zoom;
    matrix.v[1][2] = -sourceY * zoom;
    matrix.v[2][0] = 0;
    matrix.v[2][1] = 0;
    matrix.v[2][2] = 1.0f;
    g_api.setWindowTransform(g_app.mag, &matrix);
    InvalidateRect(g_app.mag, nullptr, FALSE);

    if (static_cast<int>(sourceX) != g_app.lastSource.x ||
        static_cast<int>(sourceY) != g_app.lastSource.y) {
        g_app.lastSource = {static_cast<LONG>(sourceX), static_cast<LONG>(sourceY)};
        logLine("[mag_poc] zoom=%.2fx source=(%.0f, %.0f) size=(%.0f, %.0f)", zoom,
                sourceX, sourceY, visibleWidth, visibleHeight);
    }
}

void destroyHost() {
    if (g_app.mag) {
        DestroyWindow(g_app.mag);
        g_app.mag = nullptr;
    }
    if (g_app.host) {
        DestroyWindow(g_app.host);
        g_app.host = nullptr;
    }
    g_app.monitor = nullptr;
    g_app.lastSource = {-100000, -100000};
}

bool createHost() {
    POINT cursor{};
    GetCursorPos(&cursor);
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return false;

    const int width = info.rcMonitor.right - info.rcMonitor.left;
    const int height = info.rcMonitor.bottom - info.rcMonitor.top;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = g_app.instance;
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = kHostClass;
    RegisterClassExW(&windowClass);

    g_app.host = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
        kHostClass, L"LiveZoom Magnifier PoC", WS_POPUP,
        info.rcMonitor.left, info.rcMonitor.top, width, height,
        nullptr, nullptr, g_app.instance, nullptr);
    if (!g_app.host) {
        logLine("[mag_poc] CreateWindowEx(host) failed: %lu", GetLastError());
        return false;
    }

    g_app.mag = CreateWindowExW(
        0, WC_MAGNIFIER, L"MagnifierWindow",
        WS_CHILD | WS_VISIBLE | MS_SHOWMAGNIFIEDCURSOR,
        0, 0, width, height, g_app.host, nullptr, g_app.instance, nullptr);
    if (!g_app.mag) {
        logLine("[mag_poc] CreateWindowEx(WC_MAGNIFIER) failed: %lu", GetLastError());
        DestroyWindow(g_app.host);
        g_app.host = nullptr;
        return false;
    }

    g_app.monitor = monitor;
    g_app.monitorRect = info.rcMonitor;
    if (g_api.setLensUseBitmapSmoothing) {
        g_api.setLensUseBitmapSmoothing(g_app.mag, g_options.smooth ? TRUE : FALSE);
    }
    SetWindowPos(g_app.host, HWND_TOPMOST, info.rcMonitor.left, info.rcMonitor.top, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(g_app.host, SW_SHOWNOACTIVATE);
    UpdateWindow(g_app.host);
    applyWindowFilter();
    return true;
}

void activate() {
    if (g_app.active) return;
    if (!g_app.host && !createHost()) return;
    if (g_app.monitor && !IsWindowVisible(g_app.host)) {
        ShowWindow(g_app.host, SW_SHOWNOACTIVATE);
    }
    SetWindowPos(g_app.host, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (!g_options.keepHardwareCursor && g_api.showSystemCursor) {
        g_api.showSystemCursor(FALSE);
        g_app.cursorHidden = true;
    }
    RECT clip = g_app.monitorRect;
    ClipCursor(&clip);
    g_app.active = true;
    g_app.lastSource = {-100000, -100000};
    logLine("[mag_poc] zoom on: %.2fx", g_app.zoom);
    updateTransform();
}

void deactivate() {
    if (!g_app.active) return;
    g_app.active = false;
    ClipCursor(nullptr);
    if (g_app.cursorHidden && g_api.showSystemCursor) {
        g_api.showSystemCursor(TRUE);
        g_app.cursorHidden = false;
    }
    if (g_app.host) ShowWindow(g_app.host, SW_HIDE);
    logLine("[mag_poc] zoom off");
}

void cleanup() {
    if (g_cleanedUp) return;
    g_cleanedUp = true;
    if (g_mouseHook) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }
    KillTimer(nullptr, kTimerId);
    deactivate();
    destroyHost();
    ClipCursor(nullptr);
    if (g_api.showSystemCursor) g_api.showSystemCursor(TRUE);
    if (g_api.uninitialize) g_api.uninitialize();
    if (g_api.module) {
        FreeLibrary(g_api.module);
        g_api.module = nullptr;
    }
}

LRESULT CALLBACK mouseHookProc(int code, WPARAM message, LPARAM lParam) {
    if (code == HC_ACTION) {
        const auto* mouse = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        if (!(mouse->flags & LLMHF_INJECTED)) {
            switch (message) {
                case WM_MBUTTONDOWN:
                    if (!(GetAsyncKeyState(VK_MENU) & 0x8000)) {
                        g_app.middleDown = true;
                        if (g_app.active) {
                            deactivate();
                        } else {
                            activate();
                        }
                        return 1;
                    }
                    break;
                case WM_MBUTTONUP:
                    if (g_app.middleDown) {
                        g_app.middleDown = false;
                        return 1;
                    }
                    break;
                case WM_MOUSEWHEEL:
                    if (g_app.middleDown && g_app.active) {
                        const int delta = static_cast<short>(HIWORD(mouse->mouseData));
                        const float factor = std::exp(static_cast<float>(delta) / 120.0f * 0.1f);
                        g_app.zoom = std::clamp(g_app.zoom * factor, 1.25f, 4.0f);
                        updateTransform();
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

void onTimer() {
    if (g_app.quitAt && GetTickCount64() >= g_app.quitAt) {
        PostQuitMessage(0);
        return;
    }
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
        PostQuitMessage(0);
        return;
    }
    if (g_app.active) updateTransform();

    const uint64_t now = GetTickCount64();
    if (now - g_app.lastFpsTick >= 1000) {
        if (g_app.lastFpsTick) {
            logLine("[mag_poc] %d updates/s active=%d", g_app.frames, g_app.active ? 1 : 0);
        }
        g_app.lastFpsTick = now;
        g_app.frames = 0;
    }
    ++g_app.frames;
}

BOOL WINAPI consoleCtrlHandler(DWORD) {
    cleanup();
    return FALSE;
}

bool parseOptions(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--zoom" && i + 1 < argc) {
            g_options.zoom = std::clamp(std::stof(argv[++i]), 1.25f, 4.0f);
        } else if (arg == "--seconds" && i + 1 < argc) {
            g_options.seconds = std::stod(argv[++i]);
        } else if (arg == "--smooth") {
            g_options.smooth = true;
        } else if (arg == "--no-obs-filter") {
            g_options.excludeObs = false;
        } else if (arg == "--keep-hardware-cursor") {
            g_options.keepHardwareCursor = true;
        } else if (arg == "--no-auto-activate") {
            g_options.autoActivate = false;
        } else if (arg == "--help" || arg == "-h") {
            std::printf("usage: mag_poc [--zoom N] [--seconds N] [--smooth] "
                        "[--no-obs-filter] [--keep-hardware-cursor] [--no-auto-activate]\n");
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

    if (!parseOptions(argc, argv)) return 1;
    g_app.instance = GetModuleHandleW(nullptr);
    g_app.zoom = g_options.zoom;

    if (!loadMagnifierApi()) return 1;
    if (!g_api.initialize()) {
        logLine("[mag_poc] MagInitialize failed: %lu", GetLastError());
        return 1;
    }

    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, mouseHookProc, g_app.instance, 0);
    if (!g_mouseHook) {
        logLine("[mag_poc] SetWindowsHookEx failed: %lu", GetLastError());
        cleanup();
        return 1;
    }

    if (g_options.autoActivate) activate();
    if (g_options.seconds > 0) {
        g_app.quitAt = GetTickCount64() + static_cast<uint64_t>(g_options.seconds * 1000);
    }
    SetTimer(nullptr, kTimerId, 8, nullptr);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_TIMER && message.hwnd == nullptr) {
            onTimer();
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    cleanup();
    logLine("[mag_poc] done");
    return 0;
}
