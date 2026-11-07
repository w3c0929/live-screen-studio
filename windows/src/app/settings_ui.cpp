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
constexpr int kIdOk = 2001;
constexpr int kIdCancel = 2002;

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
lz::Settings* g_settings = nullptr;
bool g_done = false;

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

    addStatic(dialog,
              L"中键：放大/缩回    按住中键滚动：调倍\n"
              L"放大后左键拖动：画线    Shift：直线\n"
              L"Ctrl+左键拖动：正常拖拽    右键：撤销\n"
              L"Esc：清除笔迹\n"
              L"跟随速度越灵敏，画布越晚开始移动",
              24, 220, 370, 100);

    addButton(dialog, kIdOk, L"确定", 590, 330, 86, 30);
    addButton(dialog, kIdCancel, L"取消", 686, 330, 86, 30);
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
}

LRESULT CALLBACK settingsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kIdOk:
                    readControls();
                    g_settings->save();
                    DestroyWindow(window);
                    return 0;
                case kIdCancel:
                    DestroyWindow(window);
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

void showSettingsDialog(HWND owner, lz::Settings& settings) {
    g_settings = &settings;
    g_done = false;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = settingsProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = kSettingsClass;
    RegisterClassExW(&windowClass);

    const int width = 780;
    const int height = 400;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

    g_dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, kSettingsClass, L"LiveZoom 设置",
                               WS_CAPTION | WS_SYSMENU | WS_POPUP,
                               x, y, width, height, owner, nullptr, GetModuleHandleW(nullptr),
                               nullptr);
    if (!g_dialog) return;

    createControls(g_dialog);
    EnableWindow(owner, FALSE);
    ShowWindow(g_dialog, SW_SHOW);
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
}
