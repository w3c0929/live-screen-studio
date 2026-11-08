#include "magnification_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <iterator>
#include <string>

#ifndef WC_MAGNIFIER
#define WC_MAGNIFIER L"Magnifier"
#endif
#ifndef MS_SHOWMAGNIFIEDCURSOR
#define MS_SHOWMAGNIFIEDCURSOR 0x0001
#endif

namespace {

constexpr wchar_t kHostClass[] = L"LiveZoomMagnifierHost";

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

}  // namespace

MagnificationEngine::~MagnificationEngine() {
    shutdown();
}

bool MagnificationEngine::initialize(std::string& error) {
    api_.module = LoadLibraryW(L"magnification.dll");
    if (!api_.module) {
        error = "could not load magnification.dll";
        return false;
    }
    auto get = [this](const char* name) { return GetProcAddress(api_.module, name); };
    api_.initialize = reinterpret_cast<decltype(api_.initialize)>(get("MagInitialize"));
    api_.uninitialize = reinterpret_cast<decltype(api_.uninitialize)>(get("MagUninitialize"));
    api_.setWindowTransform =
        reinterpret_cast<decltype(api_.setWindowTransform)>(get("MagSetWindowTransform"));
    api_.setWindowFilterList =
        reinterpret_cast<decltype(api_.setWindowFilterList)>(get("MagSetWindowFilterList"));
    api_.showSystemCursor =
        reinterpret_cast<decltype(api_.showSystemCursor)>(get("MagShowSystemCursor"));
    api_.setLensUseBitmapSmoothing =
        reinterpret_cast<decltype(api_.setLensUseBitmapSmoothing)>(
            get("MagSetLensUseBitmapSmoothing"));

    if (!api_.initialize || !api_.uninitialize || !api_.setWindowTransform) {
        error = "magnification.dll is missing required exports";
        return false;
    }
    if (!api_.initialize()) {
        error = "MagInitialize failed (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    initialized_ = true;
    registerHostClass();
    return true;
}

void MagnificationEngine::shutdown() {
    if (!api_.module && !initialized_) return;
    if (initialized_) {
        hide();
        hideHardwareCursor(false);
        destroyHost();
    }
    if (initialized_ && api_.uninitialize) {
        api_.uninitialize();
    }
    if (api_.module) {
        FreeLibrary(api_.module);
        api_.module = nullptr;
    }
    api_.initialize = nullptr;
    api_.uninitialize = nullptr;
    api_.setWindowTransform = nullptr;
    api_.setWindowFilterList = nullptr;
    api_.showSystemCursor = nullptr;
    api_.setLensUseBitmapSmoothing = nullptr;
    initialized_ = false;
}

void MagnificationEngine::registerHostClass() {
    if (classRegistered_) return;
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = hostProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = kHostClass;
    classRegistered_ = RegisterClassExW(&windowClass) != 0 ||
                       GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool MagnificationEngine::createHost(HMONITOR monitor) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return false;

    registerHostClass();
    const int width = info.rcMonitor.right - info.rcMonitor.left;
    const int height = info.rcMonitor.bottom - info.rcMonitor.top;

    host_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
        kHostClass, L"LiveZoom", WS_POPUP,
        info.rcMonitor.left, info.rcMonitor.top, width, height,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!host_) {
        std::printf("[livezoom] CreateWindowEx(host) failed: %lu\n", GetLastError());
        return false;
    }

    // No MS_SHOWMAGNIFIEDCURSOR: the overlay draws a custom cursor above the magnified
    // content, at the same mapped position as the ink and click effects.
    mag_ = CreateWindowExW(
        0, WC_MAGNIFIER, L"MagnifierWindow",
        WS_CHILD | WS_VISIBLE,
        0, 0, width, height, host_, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!mag_) {
        std::printf("[livezoom] CreateWindowEx(WC_MAGNIFIER) failed: %lu\n", GetLastError());
        DestroyWindow(host_);
        host_ = nullptr;
        return false;
    }

    monitor_ = monitor;
    monitorRect_ = info.rcMonitor;
    if (api_.setLensUseBitmapSmoothing) {
        api_.setLensUseBitmapSmoothing(mag_, smoothing_ ? TRUE : FALSE);
    }
    refreshExcludedWindows();
    return true;
}

void MagnificationEngine::destroyHost() {
    if (mag_) {
        DestroyWindow(mag_);
        mag_ = nullptr;
    }
    if (host_) {
        DestroyWindow(host_);
        host_ = nullptr;
    }
    monitor_ = nullptr;
    visible_ = false;
}

bool MagnificationEngine::showForMonitor(HMONITOR monitor) {
    if (!api_.setWindowTransform) return false;
    if (host_ && monitor_ == monitor) {
        showHost();
        return true;
    }
    if (visible_) hide();
    destroyHost();
    if (!createHost(monitor)) return false;
    showHost();
    return true;
}

void MagnificationEngine::showHost() {
    if (!host_) return;
    SetWindowPos(host_, HWND_TOPMOST, monitorRect_.left, monitorRect_.top,
                 monitorRect_.right - monitorRect_.left,
                 monitorRect_.bottom - monitorRect_.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(host_, SW_SHOWNOACTIVATE);
    UpdateWindow(host_);
    refreshExcludedWindows();
    hideHardwareCursor(true);
    visible_ = true;
}

void MagnificationEngine::hide() {
    // Hide the magnifier first. Some drivers keep the magnifier's cursor hide active until
    // the magnified window is gone, so restoring before ShowWindow(SW_HIDE) leaves no cursor.
    if (host_) ShowWindow(host_, SW_HIDE);
    visible_ = false;
    ensureCursorVisible();
}

void MagnificationEngine::setViewport(lz::Point sourceTopLeft, double zoom) {
    if (!mag_ || !api_.setWindowTransform) return;
    const float scale = static_cast<float>(zoom);
    const float x = static_cast<float>(sourceTopLeft.x);
    const float y = static_cast<float>(sourceTopLeft.y);

    MAGTRANSFORM matrix{};
    matrix.v[0][0] = scale;
    matrix.v[0][1] = 0;
    matrix.v[0][2] = -x * scale;
    matrix.v[1][0] = 0;
    matrix.v[1][1] = scale;
    matrix.v[1][2] = -y * scale;
    matrix.v[2][0] = 0;
    matrix.v[2][1] = 0;
    matrix.v[2][2] = 1.0f;
    api_.setWindowTransform(mag_, &matrix);
    // The magnifier repaints only its invalid region; erase is skipped to avoid a flash.
    InvalidateRect(mag_, nullptr, FALSE);
}

void MagnificationEngine::setSmoothing(bool smooth) {
    smoothing_ = smooth;
    if (mag_ && api_.setLensUseBitmapSmoothing) {
        api_.setLensUseBitmapSmoothing(mag_, smooth ? TRUE : FALSE);
    }
}

void MagnificationEngine::setExcludeObs(bool exclude) {
    excludeObs_ = exclude;
    refreshExcludedWindows();
}

void MagnificationEngine::setOverlayWindow(HWND overlay) {
    overlay_ = overlay;
    refreshExcludedWindows();
}

void MagnificationEngine::refreshExcludedWindows() {
    if (!mag_ || !api_.setWindowFilterList) return;
    excluded_.clear();
    if (excludeObs_) {
        EnumWindows(enumObsWindows, reinterpret_cast<LPARAM>(&excluded_));
    }
    if (overlay_ && IsWindow(overlay_)) {
        excluded_.push_back(overlay_);
    }
    if (excluded_.empty()) {
        api_.setWindowFilterList(mag_, MW_FILTERMODE_EXCLUDE, 0, nullptr);
    } else {
        api_.setWindowFilterList(mag_, MW_FILTERMODE_EXCLUDE,
                                 static_cast<int>(excluded_.size()), excluded_.data());
    }
}

void MagnificationEngine::blankSystemCursors() {
    // A transparent 32x32 cursor. Each id needs a fresh cursor because SetSystemCursor
    // destroys the handle it is given.
    constexpr DWORD ids[] = {
        32512, 32513, 32514, 32515, 32516, 32642, 32643,
        32644, 32645, 32646, 32648, 32649, 32650, 32651,
    };
    bool any = false;
    BYTE andMask[32 * 32 / 8];
    BYTE xorMask[32 * 32 / 8];
    std::memset(andMask, 0xFF, sizeof(andMask));
    std::memset(xorMask, 0, sizeof(xorMask));
    for (const DWORD id : ids) {
        HCURSOR cursor = CreateCursor(GetModuleHandleW(nullptr), 0, 0, 32, 32, andMask,
                                      xorMask);
        if (!cursor) continue;
        if (SetSystemCursor(cursor, id)) {
            any = true;
        } else {
            DestroyCursor(cursor);
        }
    }
    std::printf("[livezoom] system cursors blanked=%d\n", any ? 1 : 0);
}

void MagnificationEngine::hideHardwareCursor(bool hide) {
    if (!hide) {
        ensureCursorVisible();
        return;
    }
    std::lock_guard lock(cursorMutex_);
    if (hardwareCursorHidden_) return;
    if (api_.showSystemCursor) api_.showSystemCursor(FALSE);
    blankSystemCursors();
    hardwareCursorHidden_ = true;
    std::printf("[livezoom] hardware cursor hidden\n");
}

void MagnificationEngine::ensureCursorVisible() {
    std::lock_guard lock(cursorMutex_);
    const bool wasHidden = hardwareCursorHidden_;
    if (api_.showSystemCursor) api_.showSystemCursor(TRUE);
    hardwareCursorHidden_ = false;

    // Reloading the current cursor scheme restores the real cursor bitmaps after
    // SetSystemCursor replaced them with blank ones, and recovers drivers that keep the
    // magnifier's hide state.
    if (wasHidden) {
        SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, SPIF_SENDCHANGE);
        if (api_.showSystemCursor) api_.showSystemCursor(TRUE);
    }

    CURSORINFO info{};
    info.cbSize = sizeof(info);
    const bool showing = GetCursorInfo(&info) && (info.flags & CURSOR_SHOWING) != 0;
    if (!showing) {
        for (int i = 0; i < 8; ++i) ShowCursor(TRUE);
        POINT cursor{};
        if (GetCursorPos(&cursor)) SetCursorPos(cursor.x, cursor.y);
    }
    std::printf("[livezoom] cursor restore: wasHidden=%d showing=%d\n",
                wasHidden ? 1 : 0, showing ? 1 : 0);
}

LRESULT CALLBACK MagnificationEngine::hostProc(HWND hwnd, UINT message, WPARAM wParam,
                                               LPARAM lParam) {
    switch (message) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_ERASEBKGND:
            return 1;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
