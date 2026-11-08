#include "settings_ui.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>

namespace {

constexpr wchar_t kSettingsClass[] = L"LiveZoomSettingsWindow";

constexpr int kIdZoom = 1001;
constexpr int kIdFollow = 1002;
constexpr int kIdDraw = 1003;
constexpr int kIdSmooth = 1004;
constexpr int kIdObs = 1005;
constexpr int kIdInkStyle = 1006;
constexpr int kIdInkColor = 1007;
constexpr int kIdInkWidth = 1008;
constexpr int kIdClickEffect = 1009;
constexpr int kIdClickColor = 1010;
constexpr int kIdCursorSize = 1011;
constexpr int kIdCursorStyle = 1012;
constexpr int kIdSmoothCursor = 1013;
constexpr int kIdHideIdle = 1014;
constexpr int kIdMarginCalm = 1015;
constexpr int kIdMarginStandard = 1016;
constexpr int kIdMarginSnappy = 1017;
constexpr int kIdHotkeyOpen = 1018;
constexpr int kIdCustomAlways = 1019;
constexpr int kIdAutoStart = 1020;
constexpr int kIdOk = 2001;
constexpr int kIdCancel = 2002;
constexpr int kIdRecordBase = 3001;
constexpr int kIdHotkeyClose = 3005;

constexpr wchar_t kHotkeyClass[] = L"LiveZoomHotkeyWindow";

constexpr double kZoomValues[] = {1.25, 1.5, 1.75, 2.0, 2.5, 3.0, 4.0};
constexpr int kZoomCount = static_cast<int>(sizeof(kZoomValues) / sizeof(kZoomValues[0]));
constexpr double kStrokeWidths[] = {2.0, 3.0, 5.0, 8.0, 12.0, 16.0};
constexpr int kStrokeWidthCount =
    static_cast<int>(sizeof(kStrokeWidths) / sizeof(kStrokeWidths[0]));
constexpr double kCursorSizes[] = {0.8, 1.0, 1.2, 1.5, 2.0};
constexpr int kCursorSizeCount = static_cast<int>(sizeof(kCursorSizes) / sizeof(kCursorSizes[0]));

HWND g_dialog = nullptr;
HWND g_zoomCombo = nullptr;
HWND g_followCombo = nullptr;
HWND g_drawCheck = nullptr;
HWND g_smoothCheck = nullptr;
HWND g_obsCheck = nullptr;
HWND g_inkStyleCombo = nullptr;
HWND g_inkColorCombo = nullptr;
HWND g_inkWidthCombo = nullptr;
HWND g_clickEffectCombo = nullptr;
HWND g_clickColorCombo = nullptr;
HWND g_cursorSizeCombo = nullptr;
HWND g_cursorStyleCombo = nullptr;
HWND g_smoothCursorCheck = nullptr;
HWND g_hideIdleCheck = nullptr;
HWND g_customAlwaysCheck = nullptr;
HWND g_autoStartCheck = nullptr;
HWND g_marginCalmCombo = nullptr;
HWND g_marginStandardCombo = nullptr;
HWND g_marginSnappyCombo = nullptr;
lz::Settings* g_settings = nullptr;
bool g_done = false;
bool g_accepted = false;

lz::Hotkey g_editHotkeys[4];
HWND g_hotkeyDialog = nullptr;
HWND g_hotkeyLabels[4] = {nullptr, nullptr, nullptr, nullptr};
int g_recordingKind = -1;
bool g_hotkeyDone = false;

void setFont(HWND control) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                 TRUE);
}

HWND addStatic(HWND parent, const wchar_t* text, int x, int y, int width, int height) {
    HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, width, height,
                                   parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    setFont(control);
    return control;
}

HWND addCombo(HWND parent, int id, int x, int y, int width, int height) {
    HWND control = CreateWindowExW(0, L"COMBOBOX", L"",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
                                       WS_VSCROLL,
                                   x, y, width, height, parent,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   GetModuleHandleW(nullptr), nullptr);
    setFont(control);
    return control;
}

HWND addCheck(HWND parent, int id, const wchar_t* text, int x, int y, int width, int height) {
    HWND control = CreateWindowExW(0, L"BUTTON", text,
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, x, y,
                                   width, height, parent,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   GetModuleHandleW(nullptr), nullptr);
    setFont(control);
    return control;
}

HWND addButton(HWND parent, int id, const wchar_t* text, int x, int y, int width, int height) {
    HWND control = CreateWindowExW(0, L"BUTTON", text,
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, x, y,
                                   width, height, parent,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   GetModuleHandleW(nullptr), nullptr);
    setFont(control);
    return control;
}

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

std::wstring executablePath() {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    return length ? std::wstring(path, length) : std::wstring();
}

bool autoStartEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) {
        return false;
    }
    wchar_t value[2048]{};
    DWORD size = sizeof(value);
    DWORD type = 0;
    const LSTATUS status =
        RegQueryValueExW(key, L"LiveZoom", nullptr, &type,
                         reinterpret_cast<BYTE*>(value), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ) return false;
    const std::wstring expected = L"\"" + executablePath() + L"\"";
    return _wcsicmp(value, expected.c_str()) == 0;
}

bool setAutoStart(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    LSTATUS status = ERROR_SUCCESS;
    if (enabled) {
        const std::wstring value = L"\"" + executablePath() + L"\"";
        status = RegSetValueExW(key, L"LiveZoom", 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(value.c_str()),
                                static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, L"LiveZoom");
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

int nearestZoomIndex(double zoom) {
    int best = 0;
    double distance = std::abs(kZoomValues[0] - zoom);
    for (int i = 1; i < kZoomCount; ++i) {
        const double current = std::abs(kZoomValues[i] - zoom);
        if (current < distance) {
            distance = current;
            best = i;
        }
    }
    return best;
}

int nearestWidthIndex(double width) {
    int best = 0;
    double distance = std::abs(kStrokeWidths[0] - width);
    for (int i = 1; i < kStrokeWidthCount; ++i) {
        const double current = std::abs(kStrokeWidths[i] - width);
        if (current < distance) {
            distance = current;
            best = i;
        }
    }
    return best;
}

int nearestCursorSizeIndex(double size) {
    int best = 0;
    double distance = std::abs(kCursorSizes[0] - size);
    for (int i = 1; i < kCursorSizeCount; ++i) {
        const double current = std::abs(kCursorSizes[i] - size);
        if (current < distance) {
            distance = current;
            best = i;
        }
    }
    return best;
}

int marginPercentIndex(double margin) {
    const int percent = static_cast<int>(std::lround(margin * 100.0));
    return std::clamp(percent, 1, 20) - 1;
}

void fillMarginCombo(HWND combo, double margin) {
    for (int percent = 1; percent <= 20; ++percent) {
        wchar_t text[16];
        std::swprintf(text, 16, L"%d%%", percent);
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    }
    SendMessageW(combo, CB_SETCURSEL, marginPercentIndex(margin), 0);
}

void createControls(HWND dialog) {
    addStatic(dialog, L"默认倍数", 24, 24, 100, 22);
    g_zoomCombo = addCombo(dialog, kIdZoom, 140, 20, 120, 220);
    for (double value : kZoomValues) {
        wchar_t text[32];
        std::swprintf(text, 32, L"%.2g×", value);
        SendMessageW(g_zoomCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    }
    SendMessageW(g_zoomCombo, CB_SETCURSEL, nearestZoomIndex(g_settings->defaultZoom), 0);

    addStatic(dialog, L"跟随速度", 24, 64, 100, 22);
    g_followCombo = addCombo(dialog, kIdFollow, 140, 60, 120, 220);
    SendMessageW(g_followCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"平稳"));
    SendMessageW(g_followCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"标准"));
    SendMessageW(g_followCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"灵敏"));
    int followIndex = 0;
    if (g_settings->follow == lz::FollowStyle::Standard) followIndex = 1;
    if (g_settings->follow == lz::FollowStyle::Snappy) followIndex = 2;
    SendMessageW(g_followCombo, CB_SETCURSEL, followIndex, 0);

    g_drawCheck = addCheck(dialog, kIdDraw, L"放大时左键拖动画线", 24, 108, 300, 24);
    SendMessageW(g_drawCheck, BM_SETCHECK, g_settings->drawWhileZoomed ? BST_CHECKED : BST_UNCHECKED,
                 0);

    g_smoothCheck = addCheck(dialog, kIdSmooth, L"放大画面平滑", 24, 140, 300, 24);
    SendMessageW(g_smoothCheck, BM_SETCHECK, g_settings->smoothImage ? BST_CHECKED : BST_UNCHECKED,
                 0);

    g_obsCheck = addCheck(dialog, kIdObs, L"排除 OBS 窗口", 24, 172, 300, 24);
    SendMessageW(g_obsCheck, BM_SETCHECK, g_settings->excludeObs ? BST_CHECKED : BST_UNCHECKED, 0);

    g_smoothCursorCheck = addCheck(dialog, kIdSmoothCursor, L"光标平滑", 24, 204, 300, 24);
    SendMessageW(g_smoothCursorCheck, BM_SETCHECK,
                 g_settings->smoothCursor ? BST_CHECKED : BST_UNCHECKED, 0);
    g_hideIdleCheck = addCheck(dialog, kIdHideIdle, L"静止后隐藏光标（3 秒）", 24, 236, 300, 24);
    SendMessageW(g_hideIdleCheck, BM_SETCHECK,
                 g_settings->hideIdleCursor ? BST_CHECKED : BST_UNCHECKED, 0);
    g_customAlwaysCheck =
        addCheck(dialog, kIdCustomAlways, L"始终使用自定义光标（非放大时）", 24, 268, 360, 24);
    SendMessageW(g_customAlwaysCheck, BM_SETCHECK,
                 g_settings->customCursorAlways ? BST_CHECKED : BST_UNCHECKED, 0);
    g_autoStartCheck = addCheck(dialog, kIdAutoStart, L"开机启动 LiveZoom", 800, 208, 200, 24);
    SendMessageW(g_autoStartCheck, BM_SETCHECK,
                 autoStartEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);

    addStatic(dialog, L"标注与点击", 420, 20, 200, 22);

    addStatic(dialog, L"笔迹样式", 420, 56, 110, 22);
    g_inkStyleCombo = addCombo(dialog, kIdInkStyle, 540, 52, 150, 220);
    SendMessageW(g_inkStyleCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"马克笔"));
    SendMessageW(g_inkStyleCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"霓虹"));
    SendMessageW(g_inkStyleCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"荧光笔"));
    SendMessageW(g_inkStyleCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"箭头"));
    SendMessageW(g_inkStyleCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"流动虚线"));
    int styleIndex = 0;
    switch (g_settings->inkStyle) {
        case lz::InkStyle::Marker: styleIndex = 0; break;
        case lz::InkStyle::Neon: styleIndex = 1; break;
        case lz::InkStyle::Highlighter: styleIndex = 2; break;
        case lz::InkStyle::Arrow: styleIndex = 3; break;
        case lz::InkStyle::Dashed: styleIndex = 4; break;
    }
    SendMessageW(g_inkStyleCombo, CB_SETCURSEL, styleIndex, 0);

    addStatic(dialog, L"笔迹颜色", 420, 92, 110, 22);
    g_inkColorCombo = addCombo(dialog, kIdInkColor, 540, 88, 150, 220);
    SendMessageW(g_inkColorCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"红"));
    SendMessageW(g_inkColorCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"黄"));
    SendMessageW(g_inkColorCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"绿"));
    SendMessageW(g_inkColorCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"蓝"));
    SendMessageW(g_inkColorCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"粉"));
    SendMessageW(g_inkColorCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"白"));
    SendMessageW(g_inkColorCombo, CB_SETCURSEL, static_cast<WPARAM>(g_settings->inkColor), 0);

    addStatic(dialog, L"笔迹粗细", 420, 128, 110, 22);
    g_inkWidthCombo = addCombo(dialog, kIdInkWidth, 540, 124, 150, 220);
    for (double width : kStrokeWidths) {
        wchar_t text[32];
        std::swprintf(text, 32, L"%.0f px", width);
        SendMessageW(g_inkWidthCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    }
    SendMessageW(g_inkWidthCombo, CB_SETCURSEL, nearestWidthIndex(g_settings->strokeWidth), 0);

    addStatic(dialog, L"点击效果", 420, 164, 110, 22);
    g_clickEffectCombo = addCombo(dialog, kIdClickEffect, 540, 160, 150, 220);
    SendMessageW(g_clickEffectCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"无"));
    SendMessageW(g_clickEffectCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"波纹"));
    SendMessageW(g_clickEffectCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"光圈"));
    int effectIndex = 1;
    if (g_settings->clickEffect == lz::ClickEffect::None) effectIndex = 0;
    if (g_settings->clickEffect == lz::ClickEffect::Halo) effectIndex = 2;
    SendMessageW(g_clickEffectCombo, CB_SETCURSEL, effectIndex, 0);

    addStatic(dialog, L"点击颜色", 420, 200, 110, 22);
    g_clickColorCombo = addCombo(dialog, kIdClickColor, 540, 196, 150, 220);
    SendMessageW(g_clickColorCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"蓝"));
    SendMessageW(g_clickColorCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"黄"));
    SendMessageW(g_clickColorCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"红"));
    SendMessageW(g_clickColorCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"绿"));
    SendMessageW(g_clickColorCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"白"));
    SendMessageW(g_clickColorCombo, CB_SETCURSEL, static_cast<WPARAM>(g_settings->clickColor), 0);

    addStatic(dialog, L"光标大小", 420, 236, 110, 22);
    g_cursorSizeCombo = addCombo(dialog, kIdCursorSize, 540, 232, 150, 220);
    for (double size : kCursorSizes) {
        wchar_t text[32];
        std::swprintf(text, 32, L"%.1fx", size);
        SendMessageW(g_cursorSizeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    }
    SendMessageW(g_cursorSizeCombo, CB_SETCURSEL,
                 nearestCursorSizeIndex(g_settings->cursorSize), 0);

    addStatic(dialog, L"光标样式", 420, 272, 110, 22);
    g_cursorStyleCombo = addCombo(dialog, kIdCursorStyle, 540, 268, 150, 220);
    SendMessageW(g_cursorStyleCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"经典黑"));
    SendMessageW(g_cursorStyleCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"经典白"));
    SendMessageW(g_cursorStyleCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"海蓝"));
    SendMessageW(g_cursorStyleCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"日落"));
    SendMessageW(g_cursorStyleCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"薄荷"));
    SendMessageW(g_cursorStyleCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"圆点"));
    SendMessageW(g_cursorStyleCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"聚光"));
    int cursorStyleIndex = static_cast<int>(g_settings->cursorStyle) - 1;
    if (cursorStyleIndex < 0 || cursorStyleIndex > 6) cursorStyleIndex = 0;
    SendMessageW(g_cursorStyleCombo, CB_SETCURSEL, cursorStyleIndex, 0);

    addStatic(dialog, L"跟随边缘", 800, 20, 200, 22);
    addStatic(dialog, L"平稳", 800, 56, 90, 22);
    g_marginCalmCombo = addCombo(dialog, kIdMarginCalm, 900, 52, 100, 220);
    fillMarginCombo(g_marginCalmCombo, g_settings->followMarginCalm);
    addStatic(dialog, L"标准", 800, 92, 90, 22);
    g_marginStandardCombo = addCombo(dialog, kIdMarginStandard, 900, 88, 100, 220);
    fillMarginCombo(g_marginStandardCombo, g_settings->followMarginStandard);
    addStatic(dialog, L"灵敏", 800, 128, 90, 22);
    g_marginSnappyCombo = addCombo(dialog, kIdMarginSnappy, 900, 124, 100, 220);
    fillMarginCombo(g_marginSnappyCombo, g_settings->followMarginSnappy);
    addButton(dialog, kIdHotkeyOpen, L"高亮快捷键…", 800, 168, 180, 30);

    // Explanatory text lives in a dedicated bottom block, never between controls.
    CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ, 24, 320,
                    1000, 2, dialog, nullptr, GetModuleHandleW(nullptr), nullptr);
    addStatic(dialog,
              L"中键：放大/缩回    按住中键滚动：调倍\n"
              L"放大后左键拖动：画线    Shift：直线    Ctrl+左键拖动：正常拖拽\n"
              L"右键：撤销    Esc：清除笔迹\n"
              L"高亮：默认 Ctrl+Shift+1 框住 / 2 划线 / 3 荧光\n"
              L"      Ctrl+Shift+4 光标聚光灯（可自定义快捷键）\n"
              L"跟随边缘：百分比越小，越靠近屏幕边缘才移动",
              24, 336, 1000, 120);

    addButton(dialog, kIdOk, L"确定", 860, 480, 86, 30);
    addButton(dialog, kIdCancel, L"取消", 956, 480, 86, 30);
}

void readControls() {
    const int zoomIndex = static_cast<int>(SendMessageW(g_zoomCombo, CB_GETCURSEL, 0, 0));
    if (zoomIndex >= 0 && zoomIndex < kZoomCount) {
        g_settings->defaultZoom = kZoomValues[zoomIndex];
    }
    const int followIndex = static_cast<int>(SendMessageW(g_followCombo, CB_GETCURSEL, 0, 0));
    g_settings->follow = followIndex == 1 ? lz::FollowStyle::Standard
                         : followIndex == 2 ? lz::FollowStyle::Snappy
                                            : lz::FollowStyle::Calm;
    g_settings->drawWhileZoomed =
        SendMessageW(g_drawCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_settings->smoothImage =
        SendMessageW(g_smoothCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_settings->excludeObs =
        SendMessageW(g_obsCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_settings->smoothCursor =
        SendMessageW(g_smoothCursorCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_settings->hideIdleCursor =
        SendMessageW(g_hideIdleCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_settings->customCursorAlways =
        SendMessageW(g_customAlwaysCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;

    const bool autoStart = SendMessageW(g_autoStartCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (!setAutoStart(autoStart)) {
        MessageBoxW(g_dialog, L"无法修改开机启动项，请检查系统权限。", L"LiveZoom",
                    MB_OK | MB_ICONWARNING);
    }

    const int styleIndex = static_cast<int>(SendMessageW(g_inkStyleCombo, CB_GETCURSEL, 0, 0));
    switch (styleIndex) {
        case 1: g_settings->inkStyle = lz::InkStyle::Neon; break;
        case 2: g_settings->inkStyle = lz::InkStyle::Highlighter; break;
        case 3: g_settings->inkStyle = lz::InkStyle::Arrow; break;
        case 4: g_settings->inkStyle = lz::InkStyle::Dashed; break;
        default: g_settings->inkStyle = lz::InkStyle::Marker; break;
    }

    const int colorIndex = static_cast<int>(SendMessageW(g_inkColorCombo, CB_GETCURSEL, 0, 0));
    g_settings->inkColor = colorIndex >= 0 && colorIndex <= 5
                               ? static_cast<lz::InkPalette>(colorIndex)
                               : lz::InkPalette::Red;

    const int widthIndex = static_cast<int>(SendMessageW(g_inkWidthCombo, CB_GETCURSEL, 0, 0));
    if (widthIndex >= 0 && widthIndex < kStrokeWidthCount) {
        g_settings->strokeWidth = kStrokeWidths[widthIndex];
    }

    const int effectIndex =
        static_cast<int>(SendMessageW(g_clickEffectCombo, CB_GETCURSEL, 0, 0));
    if (effectIndex == 0) {
        g_settings->clickEffect = lz::ClickEffect::None;
    } else if (effectIndex == 2) {
        g_settings->clickEffect = lz::ClickEffect::Halo;
    } else {
        g_settings->clickEffect = lz::ClickEffect::Ripple;
    }

    const int clickColorIndex =
        static_cast<int>(SendMessageW(g_clickColorCombo, CB_GETCURSEL, 0, 0));
    g_settings->clickColor = clickColorIndex >= 0 && clickColorIndex <= 4
                                 ? static_cast<lz::ClickPalette>(clickColorIndex)
                                 : lz::ClickPalette::Blue;

    const int cursorSizeIndex =
        static_cast<int>(SendMessageW(g_cursorSizeCombo, CB_GETCURSEL, 0, 0));
    if (cursorSizeIndex >= 0 && cursorSizeIndex < kCursorSizeCount) {
        g_settings->cursorSize = kCursorSizes[cursorSizeIndex];
    }

    const int cursorStyleIndex =
        static_cast<int>(SendMessageW(g_cursorStyleCombo, CB_GETCURSEL, 0, 0));
    if (cursorStyleIndex >= 0 && cursorStyleIndex <= 6) {
        g_settings->cursorStyle =
            static_cast<lz::CursorStyle>(cursorStyleIndex + 1);
    }

    auto readMargin = [](HWND combo, double fallback) {
        const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
        return index >= 0 && index < 20 ? (index + 1) / 100.0 : fallback;
    };
    g_settings->followMarginCalm = readMargin(g_marginCalmCombo, g_settings->followMarginCalm);
    g_settings->followMarginStandard =
        readMargin(g_marginStandardCombo, g_settings->followMarginStandard);
    g_settings->followMarginSnappy =
        readMargin(g_marginSnappyCombo, g_settings->followMarginSnappy);

    g_settings->highlightBox = g_editHotkeys[0];
    g_settings->highlightUnderline = g_editHotkeys[1];
    g_settings->highlightFill = g_editHotkeys[2];
    g_settings->highlightSpotlight = g_editHotkeys[3];
}

void updateHotkeyLabel(int kind) {
    if (kind < 0 || kind > 3 || !g_hotkeyLabels[kind]) return;
    SetWindowTextW(g_hotkeyLabels[kind], lz::HotkeyText(g_editHotkeys[kind]).c_str());
}

LRESULT CALLBACK hotkeyProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id >= kIdRecordBase && id < kIdRecordBase + 4) {
                g_recordingKind = id - kIdRecordBase;
                SetFocus(g_hotkeyDialog);
                SetWindowTextW(g_hotkeyLabels[g_recordingKind], L"请按组合键…（Esc 取消）");
                return 0;
            }
            if (id == kIdHotkeyClose) {
                DestroyWindow(window);
                return 0;
            }
            break;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            if (g_recordingKind < 0) break;
            const UINT vk = static_cast<UINT>(wParam);
            if (vk == VK_ESCAPE) {
                updateHotkeyLabel(g_recordingKind);
                g_recordingKind = -1;
                return 0;
            }
            if (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT || vk == VK_LWIN ||
                vk == VK_RWIN) {
                return 0;
            }
            const UINT mods =
                ((GetKeyState(VK_CONTROL) & 0x8000) ? 1u : 0u) |
                ((GetKeyState(VK_MENU) & 0x8000) ? 2u : 0u) |
                ((GetKeyState(VK_SHIFT) & 0x8000) ? 4u : 0u) |
                ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000) ? 8u
                                                                                    : 0u);
            if (mods == 0) {
                SetWindowTextW(g_hotkeyLabels[g_recordingKind],
                               L"需要至少一个 Ctrl/Alt/Shift/Win");
                return 0;
            }
            g_editHotkeys[g_recordingKind] = lz::Hotkey{vk, mods};
            updateHotkeyLabel(g_recordingKind);
            g_recordingKind = -1;
            return 0;
        }
        case WM_SYSCHAR:
            if (g_recordingKind >= 0) return 0;
            break;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            g_hotkeyDone = true;
            g_hotkeyDialog = nullptr;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void showHotkeyDialog(HWND owner) {
    g_hotkeyDone = false;
    g_recordingKind = -1;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = hotkeyProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = kHotkeyClass;
    RegisterClassExW(&windowClass);

    const int width = 560;
    const int height = 330;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    g_hotkeyDialog = CreateWindowExW(WS_EX_DLGMODALFRAME, kHotkeyClass, L"高亮快捷键",
                                     WS_CAPTION | WS_SYSMENU | WS_POPUP, x, y, width, height,
                                     owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_hotkeyDialog) return;

    const wchar_t* names[4] = {L"框住", L"划线", L"荧光笔", L"聚光灯"};
    for (int i = 0; i < 4; ++i) {
        const int row = 20 + i * 40;
        addStatic(g_hotkeyDialog, names[i], 20, row, 80, 24);
        g_hotkeyLabels[i] = addStatic(g_hotkeyDialog, L"", 100, row, 270, 24);
        updateHotkeyLabel(i);
        addButton(g_hotkeyDialog, kIdRecordBase + i, L"录制", 390, row - 4, 80, 28);
    }
    addStatic(g_hotkeyDialog,
              L"点击“录制”后按下组合键；至少包含一个 Ctrl/Alt/Shift/Win。\n"
              L"按 Esc 取消录制。修改会随主设置窗口的“确定”一起保存。",
              20, 190, 520, 70);
    addButton(g_hotkeyDialog, kIdHotkeyClose, L"关闭", 450, 255, 86, 30);

    EnableWindow(owner, FALSE);
    ShowWindow(g_hotkeyDialog, SW_SHOW);
    SetForegroundWindow(g_hotkeyDialog);
    UpdateWindow(g_hotkeyDialog);

    MSG message{};
    while (!g_hotkeyDone && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (g_recordingKind >= 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        } else if (!IsDialogMessageW(g_hotkeyDialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

LRESULT CALLBACK settingsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kIdOk:
                    readControls();
                    g_settings->save();
                    g_accepted = true;
                    DestroyWindow(window);
                    return 0;
                case kIdCancel:
                    DestroyWindow(window);
                    return 0;
                case kIdHotkeyOpen:
                    showHotkeyDialog(window);
                    return 0;
                default:
                    break;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            g_done = true;
            g_dialog = nullptr;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

bool showSettingsDialog(HWND owner, lz::Settings& settings) {
    g_settings = &settings;
    g_done = false;
    g_accepted = false;
    g_editHotkeys[0] = settings.highlightBox;
    g_editHotkeys[1] = settings.highlightUnderline;
    g_editHotkeys[2] = settings.highlightFill;
    g_editHotkeys[3] = settings.highlightSpotlight;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = settingsProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = kSettingsClass;
    RegisterClassExW(&windowClass);

    const int width = 1050;
    const int height = 560;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

    g_dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, kSettingsClass, L"LiveZoom 设置",
                               WS_CAPTION | WS_SYSMENU | WS_POPUP,
                               x, y, width, height, owner, nullptr, GetModuleHandleW(nullptr),
                               nullptr);
    if (!g_dialog) return false;

    createControls(g_dialog);
    EnableWindow(owner, FALSE);
    ShowWindow(g_dialog, SW_SHOW);
    SetForegroundWindow(g_dialog);
    UpdateWindow(g_dialog);

    MSG message{};
    while (!g_done && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g_dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    return g_accepted;
}
