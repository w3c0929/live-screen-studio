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
constexpr int kIdOk = 2001;
constexpr int kIdCancel = 2002;

constexpr double kZoomValues[] = {1.25, 1.5, 1.75, 2.0, 2.5, 3.0, 4.0};
constexpr int kZoomCount = static_cast<int>(sizeof(kZoomValues) / sizeof(kZoomValues[0]));

HWND g_dialog = nullptr;
HWND g_zoomCombo = nullptr;
HWND g_followCombo = nullptr;
HWND g_drawCheck = nullptr;
HWND g_smoothCheck = nullptr;
HWND g_obsCheck = nullptr;
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

    addStatic(dialog,
              L"中键：放大/缩回    按住中键滚动：调倍\n"
              L"放大后左键拖动：画线    Shift：直线\n"
              L"Ctrl+左键拖动：正常拖拽    右键：撤销\n"
              L"Esc：清除笔迹\n"
              L"跟随速度越灵敏，画布越晚开始移动",
              24, 208, 400, 84);

    addButton(dialog, kIdOk, L"确定", 250, 292, 86, 30);
    addButton(dialog, kIdCancel, L"取消", 346, 292, 86, 30);
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

    const int width = 460;
    const int height = 380;
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
