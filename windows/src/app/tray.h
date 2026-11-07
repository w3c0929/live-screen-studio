#pragma once

#include <windows.h>

#include <string>

/// System tray icon. Its callback messages arrive at the owner window.
class Tray {
public:
    explicit Tray(HWND owner);
    ~Tray();
    Tray(const Tray&) = delete;
    Tray& operator=(const Tray&) = delete;

    bool add(const std::wstring& tooltip);
    void remove();
    void setEnabled(bool enabled);

private:
    void update();

    HWND owner_ = nullptr;
    HICON icon_ = nullptr;
    bool added_ = false;
    bool enabled_ = true;
    std::wstring tooltip_ = L"LiveZoom";
};
