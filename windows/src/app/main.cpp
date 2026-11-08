#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shellapi.h>
#include <timeapi.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "input_hook.h"
#include "magnification_engine.h"
#include "settings.h"
#include "settings_ui.h"
#include "tray.h"
#include "zoom_controller.h"

namespace {

constexpr wchar_t kMainClass[] = L"LiveZoomMainWindow";
constexpr UINT_PTR kFrameTimer = 1;
constexpr UINT_PTR kExitTimer = 2;

HWND g_window = nullptr;
std::unique_ptr<MagnificationEngine> g_engine;
std::unique_ptr<InputHook> g_input;
std::unique_ptr<ZoomController> g_controller;
std::unique_ptr<Tray> g_tray;
lz::Settings g_settings;

void applyHotkeys() {
    if (!g_input) return;
    g_input->setToggleHotkey({g_settings.zoomHotkey.vk, g_settings.zoomHotkey.mods});
    g_input->setSpotlightHotkey(
        {g_settings.highlightSpotlight.vk, g_settings.highlightSpotlight.mods});
}

bool runSettingsDialog(HWND window) {
    g_input->setHotkeysSuppressed(true);
    const bool accepted = showSettingsDialog(window, g_settings);
    g_input->setHotkeysSuppressed(false);
    if (accepted) {
        g_controller->reloadSettings();
        applyHotkeys();
    }
    g_input->setEnabled(g_settings.enabled);
    g_input->setMiddleClickEnabled(g_settings.middleClickZoom);
    g_tray->setEnabled(g_settings.enabled);
    return accepted;
}

void showTrayMenu(HWND window) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, 1, L"放大 / 缩回");
    AppendMenuW(menu, MF_STRING | (g_settings.enabled ? MF_CHECKED : 0), 2, L"启用");
    AppendMenuW(menu, MF_STRING, 3, L"设置…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 4, L"退出");

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(window);
    const int command =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, window, nullptr);
    DestroyMenu(menu);
    PostMessageW(window, WM_NULL, 0, 0);

    switch (command) {
        case 1:
            g_controller->toggle();
            break;
        case 2:
            g_settings.enabled = !g_settings.enabled;
            g_settings.save();
            g_input->setEnabled(g_settings.enabled);
            g_tray->setEnabled(g_settings.enabled);
            if (!g_settings.enabled) g_controller->reset();
            break;
        case 3:
            runSettingsDialog(window);
            break;
        case 4:
            PostMessageW(window, WM_CLOSE, 0, 0);
            break;
        default:
            break;
    }
}

LRESULT CALLBACK mainProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case kLiveZoomToggle:
            g_controller->toggle();
            return 0;
        case kLiveZoomAdjust:
            g_controller->adjustZoom(static_cast<int>(wParam) / 1000.0);
            return 0;
        case kLiveZoomEscape:
            if (g_controller->active()) g_controller->toggle();
            return 0;
        case kLiveZoomStrokeBegin: {
            auto* event = reinterpret_cast<StrokeEvent*>(lParam);
            if (event) {
                g_controller->beginStroke(event->x, event->y);
                delete event;
            }
            return 0;
        }
        case kLiveZoomStrokeMove: {
            auto* event = reinterpret_cast<StrokeEvent*>(lParam);
            if (event) {
                g_controller->moveStroke(event->x, event->y, event->straight);
                delete event;
            }
            return 0;
        }
        case kLiveZoomStrokeEnd:
            g_controller->endStroke();
            return 0;
        case kLiveZoomStrokeCancel:
            g_controller->cancelStroke();
            return 0;
        case kLiveZoomSpotlight:
            g_controller->toggleSpotlight();
            return 0;
        case kLiveZoomUndo:
            g_controller->undoStroke();
            return 0;
        case kLiveZoomClear:
            g_controller->clearStrokes();
            return 0;
        case kLiveZoomClick: {
            auto* point = reinterpret_cast<POINT*>(lParam);
            if (point) {
                g_controller->handleClick(point->x, point->y);
                delete point;
            }
            return 0;
        }
        case kLiveZoomTray:
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                showTrayMenu(window);
            } else if (lParam == WM_LBUTTONUP) {
                g_controller->toggle();
            }
            return 0;
        case WM_TIMER:
            if (wParam == kFrameTimer) {
                g_controller->tick();
                return 0;
            }
            if (wParam == kExitTimer) {
                PostMessageW(window, WM_CLOSE, 0, 0);
                return 0;
            }
            break;
        case WM_DISPLAYCHANGE:
            g_controller->reset();
            return 0;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void redirectLogsToFile() {
    wchar_t appData[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    std::wstring directory = length ? std::wstring(appData, length) : std::wstring(L".");
    directory += L"\\LiveZoom";
    CreateDirectoryW(directory.c_str(), nullptr);
    const std::wstring path = directory + L"\\livezoom.log";
    if (_wfreopen(path.c_str(), L"w", stdout)) {
        setvbuf(stdout, nullptr, _IONBF, 0);
        std::printf("[livezoom] log started\n");
    }
    if (_wfreopen(path.c_str(), L"a", stderr)) {
        setvbuf(stderr, nullptr, _IONBF, 0);
    }
}

std::string utf8FromWide(const wchar_t* text) {
    if (!text) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    result.resize(static_cast<size_t>(size - 1));
    return result;
}

std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
    result.resize(static_cast<size_t>(size - 1));
    return result;
}

}  // namespace

int run(int argc, char** argv) {
    bool acceptInjected = false;
    bool openSettings = false;
    double runSeconds = 0;
    double zoomOverride = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--accept-injected") acceptInjected = true;
        else if (argument == "--settings") openSettings = true;
        else if (argument == "--seconds" && i + 1 < argc) runSeconds = std::stod(argv[++i]);
        else if (argument == "--zoom" && i + 1 < argc) {
            zoomOverride = std::clamp(std::stod(argv[++i]), 1.25, 4.0);
        }
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HANDLE singleInstance = CreateMutexW(nullptr, TRUE, L"Local\\LiveZoomWin_SingleInstance");
    if (singleInstance && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"LiveZoom 已经在运行。", L"LiveZoom", MB_OK | MB_ICONINFORMATION);
        CloseHandle(singleInstance);
        return 0;
    }

    g_settings.load();
    if (zoomOverride > 0) g_settings.defaultZoom = zoomOverride;

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = mainProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kMainClass;
    RegisterClassExW(&windowClass);

    g_window = CreateWindowExW(0, kMainClass, L"LiveZoom", WS_OVERLAPPED, CW_USEDEFAULT,
                               CW_USEDEFAULT, 1, 1, nullptr, nullptr, instance, nullptr);
    if (!g_window) {
        MessageBoxW(nullptr, L"无法创建 LiveZoom 主窗口。", L"LiveZoom", MB_OK | MB_ICONERROR);
        if (singleInstance) CloseHandle(singleInstance);
        return 1;
    }

    g_engine = std::make_unique<MagnificationEngine>();
    std::string error;
    if (!g_engine->initialize(error)) {
        const std::wstring message = L"无法初始化放大引擎：\n" + widen(error);
        MessageBoxW(nullptr, message.c_str(), L"LiveZoom", MB_OK | MB_ICONERROR);
        if (singleInstance) CloseHandle(singleInstance);
        return 1;
    }

    g_input = std::make_unique<InputHook>();
    g_controller = std::make_unique<ZoomController>(*g_engine, *g_input, g_settings);
    if (!g_input->start(g_window)) {
        MessageBoxW(nullptr, L"无法安装全局输入钩子。\n请检查安全软件是否拦截了 LiveZoom。",
                    L"LiveZoom", MB_OK | MB_ICONERROR);
        g_engine->shutdown();
        if (singleInstance) CloseHandle(singleInstance);
        return 1;
    }
    g_input->setEnabled(g_settings.enabled);
    g_input->setMiddleClickEnabled(g_settings.middleClickZoom);
    g_input->setAcceptInjected(acceptInjected);
    applyHotkeys();

    g_tray = std::make_unique<Tray>(g_window);
    g_tray->add(L"LiveZoom");
    g_tray->setEnabled(g_settings.enabled);

    // A finer system timer keeps the overlay animation near display refresh instead of the
    // default ~15.6 ms tick, which makes click ripple and cursor easing look steppy.
    timeBeginPeriod(1);
    SetTimer(g_window, kFrameTimer, 8, nullptr);
    if (runSeconds > 0) {
        SetTimer(g_window, kExitTimer, static_cast<UINT>(runSeconds * 1000), nullptr);
    }
    if (openSettings) {
        runSettingsDialog(g_window);
    }
    std::printf("[livezoom] running; enabled=%d middleClick=%d\n",
                g_settings.enabled ? 1 : 0, g_settings.middleClickZoom ? 1 : 0);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    KillTimer(g_window, kFrameTimer);
    KillTimer(g_window, kExitTimer);
    timeEndPeriod(1);
    if (g_controller) g_controller->reset();
    if (g_input) g_input->stop();
    if (g_tray) g_tray->remove();
    if (g_engine) g_engine->shutdown();
    ClipCursor(nullptr);
    if (g_window) DestroyWindow(g_window);
    if (singleInstance) CloseHandle(singleInstance);
    std::printf("[livezoom] done\n");
    return 0;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    redirectLogsToFile();

    int argc = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    std::vector<char*> argv;
    if (wide && argc > 0) {
        args.reserve(static_cast<size_t>(argc));
        for (int i = 0; i < argc; ++i) args.push_back(utf8FromWide(wide[i]));
    } else {
        args.push_back("livezoom_win.exe");
        argc = 1;
    }
    argv.reserve(args.size() + 1);
    for (std::string& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);

    const int result = run(argc, argv.data());
    if (wide) LocalFree(wide);
    std::printf("[livezoom] exit %d\n", result);
    return result;
}
