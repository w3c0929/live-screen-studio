#include "tray.h"

#include <shellapi.h>

#include <cwchar>

#include "input_hook.h"
#include "resource.h"

Tray::Tray(HWND owner) : owner_(owner) {
    icon_ = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON));
    if (!icon_) icon_ = LoadIconW(nullptr, IDI_APPLICATION);
}

Tray::~Tray() {
    remove();
}

bool Tray::add(const std::wstring& tooltip) {
    tooltip_ = tooltip;
    if (added_) {
        update();
        return true;
    }
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = owner_;
    data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = kLiveZoomTray;
    data.hIcon = icon_;
    constexpr size_t kTipLength = sizeof(data.szTip) / sizeof(wchar_t);
    wcsncpy(data.szTip, tooltip_.c_str(), kTipLength - 1);
    data.szTip[kTipLength - 1] = L'\0';
    if (!Shell_NotifyIconW(NIM_ADD, &data)) return false;
    added_ = true;
    return true;
}

void Tray::remove() {
    if (!added_) return;
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = owner_;
    data.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &data);
    added_ = false;
}

void Tray::setEnabled(bool enabled) {
    enabled_ = enabled;
    update();
}

void Tray::update() {
    if (!added_) return;
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = owner_;
    data.uID = 1;
    data.uFlags = NIF_TIP;
    const std::wstring tip = tooltip_ + (enabled_ ? L"（已启用）" : L"（已暂停）");
    constexpr size_t kTipLength = sizeof(data.szTip) / sizeof(wchar_t);
    wcsncpy(data.szTip, tip.c_str(), kTipLength - 1);
    data.szTip[kTipLength - 1] = L'\0';
    Shell_NotifyIconW(NIM_MODIFY, &data);
}
