#include "settings.h"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <iterator>
#include <string>

namespace lz {

namespace {

constexpr wchar_t kSection[] = L"LiveZoom";

std::wstring readString(const std::wstring& path, const wchar_t* key, const wchar_t* fallback) {
    wchar_t buffer[256];
    GetPrivateProfileStringW(kSection, key, fallback, buffer,
                             static_cast<DWORD>(std::size(buffer)), path.c_str());
    return buffer;
}

double readDouble(const std::wstring& path, const wchar_t* key, double fallback) {
    const std::wstring value = readString(path, key, L"");
    if (value.empty()) return fallback;
    wchar_t* end = nullptr;
    const double parsed = std::wcstod(value.c_str(), &end);
    return end && end != value.c_str() ? parsed : fallback;
}

int readInt(const std::wstring& path, const wchar_t* key, int fallback) {
    return static_cast<int>(readDouble(path, key, fallback));
}

bool readBool(const std::wstring& path, const wchar_t* key, bool fallback) {
    return readInt(path, key, fallback ? 1 : 0) != 0;
}

void writeString(const std::wstring& path, const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(kSection, key, value.c_str(), path.c_str());
}

const wchar_t* cursorStyleName(CursorStyle style) {
    switch (style) {
        case CursorStyle::Classic: return L"classic";
        case CursorStyle::Light: return L"light";
        case CursorStyle::Ocean: return L"ocean";
        case CursorStyle::Sunset: return L"sunset";
        case CursorStyle::Mint: return L"mint";
        case CursorStyle::Dot: return L"dot";
        case CursorStyle::Spotlight: return L"spotlight";
        case CursorStyle::System: return L"system";
    }
    return L"classic";
}

CursorStyle cursorStyleFromName(const std::wstring& name) {
    if (name == L"light") return CursorStyle::Light;
    if (name == L"ocean") return CursorStyle::Ocean;
    if (name == L"sunset") return CursorStyle::Sunset;
    if (name == L"mint") return CursorStyle::Mint;
    if (name == L"dot") return CursorStyle::Dot;
    if (name == L"spotlight") return CursorStyle::Spotlight;
    if (name == L"system") return CursorStyle::System;
    return CursorStyle::Classic;
}

const wchar_t* inkStyleName(InkStyle style) {
    switch (style) {
        case InkStyle::Marker: return L"marker";
        case InkStyle::Neon: return L"neon";
        case InkStyle::Highlighter: return L"highlighter";
        case InkStyle::Arrow: return L"arrow";
        case InkStyle::Dashed: return L"dashed";
    }
    return L"marker";
}

InkStyle inkStyleFromName(const std::wstring& name) {
    if (name == L"neon") return InkStyle::Neon;
    if (name == L"highlighter") return InkStyle::Highlighter;
    if (name == L"arrow") return InkStyle::Arrow;
    if (name == L"dashed") return InkStyle::Dashed;
    return InkStyle::Marker;
}

const wchar_t* inkColorName(InkPalette color) {
    switch (color) {
        case InkPalette::Red: return L"red";
        case InkPalette::Yellow: return L"yellow";
        case InkPalette::Green: return L"green";
        case InkPalette::Blue: return L"blue";
        case InkPalette::Pink: return L"pink";
        case InkPalette::White: return L"white";
    }
    return L"red";
}

InkPalette inkColorFromName(const std::wstring& name) {
    if (name == L"yellow") return InkPalette::Yellow;
    if (name == L"green") return InkPalette::Green;
    if (name == L"blue") return InkPalette::Blue;
    if (name == L"pink") return InkPalette::Pink;
    if (name == L"white") return InkPalette::White;
    return InkPalette::Red;
}

const wchar_t* clickEffectName(ClickEffect effect) {
    switch (effect) {
        case ClickEffect::None: return L"none";
        case ClickEffect::Ripple: return L"ripple";
        case ClickEffect::Halo: return L"halo";
    }
    return L"ripple";
}

ClickEffect clickEffectFromName(const std::wstring& name) {
    if (name == L"none") return ClickEffect::None;
    if (name == L"halo") return ClickEffect::Halo;
    return ClickEffect::Ripple;
}

const wchar_t* clickColorName(ClickPalette color) {
    switch (color) {
        case ClickPalette::Blue: return L"blue";
        case ClickPalette::Yellow: return L"yellow";
        case ClickPalette::Red: return L"red";
        case ClickPalette::Green: return L"green";
        case ClickPalette::White: return L"white";
    }
    return L"blue";
}

ClickPalette clickColorFromName(const std::wstring& name) {
    if (name == L"yellow") return ClickPalette::Yellow;
    if (name == L"red") return ClickPalette::Red;
    if (name == L"green") return ClickPalette::Green;
    if (name == L"white") return ClickPalette::White;
    return ClickPalette::Blue;
}

}  // namespace

std::wstring HotkeyText(const Hotkey& hotkey) {
    if (!hotkey.valid()) return L"未设置";
    std::wstring text;
    if (hotkey.mods & 1) text += L"Ctrl+";
    if (hotkey.mods & 2) text += L"Alt+";
    if (hotkey.mods & 4) text += L"Shift+";
    if (hotkey.mods & 8) text += L"Win+";

    const unsigned vk = hotkey.vk;
    wchar_t key[32]{};
    if (vk >= '0' && vk <= '9') {
        key[0] = static_cast<wchar_t>(vk);
    } else if (vk >= 'A' && vk <= 'Z') {
        key[0] = static_cast<wchar_t>(vk);
    } else if (vk >= 0x70 && vk <= 0x87) {
        std::swprintf(key, 32, L"F%u", vk - 0x6F);
    } else {
        switch (vk) {
            case VK_LEFT: std::wcscpy(key, L"←"); break;
            case VK_UP: std::wcscpy(key, L"↑"); break;
            case VK_RIGHT: std::wcscpy(key, L"→"); break;
            case VK_DOWN: std::wcscpy(key, L"↓"); break;
            case VK_SPACE: std::wcscpy(key, L"Space"); break;
            case VK_TAB: std::wcscpy(key, L"Tab"); break;
            case VK_RETURN: std::wcscpy(key, L"Enter"); break;
            case VK_ESCAPE: std::wcscpy(key, L"Esc"); break;
            case VK_BACK: std::wcscpy(key, L"Backspace"); break;
            case VK_DELETE: std::wcscpy(key, L"Delete"); break;
            case VK_INSERT: std::wcscpy(key, L"Insert"); break;
            case VK_HOME: std::wcscpy(key, L"Home"); break;
            case VK_END: std::wcscpy(key, L"End"); break;
            case VK_PRIOR: std::wcscpy(key, L"PageUp"); break;
            case VK_NEXT: std::wcscpy(key, L"PageDown"); break;
            default: std::swprintf(key, 32, L"VK 0x%02X", vk); break;
        }
    }
    text += key;
    return text;
}

std::wstring Settings::path() const {
    wchar_t appData[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    std::wstring directory = length ? std::wstring(appData, length) : std::wstring(L".");
    directory += L"\\LiveZoom";
    CreateDirectoryW(directory.c_str(), nullptr);
    return directory + L"\\settings.ini";
}

bool Settings::load() {
    const std::wstring file = path();
    enabled = readBool(file, L"enabled", enabled);
    defaultZoom = std::clamp(readDouble(file, L"defaultZoom", defaultZoom), 1.25, 4.0);
    minZoom = std::clamp(readDouble(file, L"minZoom", minZoom), 1.0, 4.0);
    maxZoom = std::clamp(readDouble(file, L"maxZoom", maxZoom), minZoom, 8.0);
    smoothImage = readBool(file, L"smoothImage", smoothImage);
    excludeObs = readBool(file, L"excludeObs", excludeObs);
    drawWhileZoomed = readBool(file, L"drawWhileZoomed", drawWhileZoomed);
    middleClickZoom = readBool(file, L"middleClickZoom", middleClickZoom);
    cursorSize = std::clamp(readDouble(file, L"cursorSize", cursorSize), 0.5, 3.0);
    cursorStyle = cursorStyleFromName(readString(file, L"cursorStyle", L"classic"));
    smoothCursor = readBool(file, L"smoothCursor", smoothCursor);
    hideIdleCursor = readBool(file, L"hideIdleCursor", hideIdleCursor);
    idleDelay = std::clamp(readDouble(file, L"idleDelay", idleDelay), 1.0, 10.0);

    const std::wstring followName = readString(file, L"follow", L"calm");
    if (followName == L"standard") {
        follow = FollowStyle::Standard;
    } else if (followName == L"snappy") {
        follow = FollowStyle::Snappy;
    } else {
        follow = FollowStyle::Calm;
    }
    followMarginCalm =
        std::clamp(readDouble(file, L"followMarginCalm", followMarginCalm), 0.01, 0.20);
    followMarginStandard = std::clamp(
        readDouble(file, L"followMarginStandard", followMarginStandard), 0.01, 0.20);
    followMarginSnappy =
        std::clamp(readDouble(file, L"followMarginSnappy", followMarginSnappy), 0.01, 0.20);

    const auto readHotkey = [&](const wchar_t* key, Hotkey fallback) {
        const std::wstring value = readString(file, key, L"");
        unsigned vk = 0;
        unsigned mods = 0;
        if (std::swscanf(value.c_str(), L"%u:%u", &vk, &mods) == 2 && vk && mods) {
            return Hotkey{vk, mods};
        }
        return fallback;
    };
    zoomHotkey = readHotkey(L"zoomHotkey", zoomHotkey);
    highlightSpotlight = readHotkey(L"highlightSpotlight", highlightSpotlight);

    inkStyle = inkStyleFromName(readString(file, L"inkStyle", L"marker"));
    inkColor = inkColorFromName(readString(file, L"inkColor", L"red"));
    strokeWidth = std::clamp(readDouble(file, L"strokeWidth", strokeWidth), 1.0, 30.0);
    clickEffect = clickEffectFromName(readString(file, L"clickEffect", L"ripple"));
    clickColor = clickColorFromName(readString(file, L"clickColor", L"blue"));
    return true;
}

bool Settings::save() const {
    const std::wstring file = path();
    writeString(file, L"enabled", enabled ? L"1" : L"0");
    writeString(file, L"defaultZoom", std::to_wstring(defaultZoom));
    writeString(file, L"minZoom", std::to_wstring(minZoom));
    writeString(file, L"maxZoom", std::to_wstring(maxZoom));
    writeString(file, L"smoothImage", smoothImage ? L"1" : L"0");
    writeString(file, L"excludeObs", excludeObs ? L"1" : L"0");
    writeString(file, L"drawWhileZoomed", drawWhileZoomed ? L"1" : L"0");
    writeString(file, L"middleClickZoom", middleClickZoom ? L"1" : L"0");
    writeString(file, L"cursorSize", std::to_wstring(cursorSize));
    writeString(file, L"cursorStyle", cursorStyleName(cursorStyle));
    writeString(file, L"smoothCursor", smoothCursor ? L"1" : L"0");
    writeString(file, L"hideIdleCursor", hideIdleCursor ? L"1" : L"0");
    writeString(file, L"idleDelay", std::to_wstring(idleDelay));
    const wchar_t* followName = L"calm";
    if (follow == FollowStyle::Standard) followName = L"standard";
    if (follow == FollowStyle::Snappy) followName = L"snappy";
    writeString(file, L"follow", followName);
    writeString(file, L"followMarginCalm", std::to_wstring(followMarginCalm));
    writeString(file, L"followMarginStandard", std::to_wstring(followMarginStandard));
    writeString(file, L"followMarginSnappy", std::to_wstring(followMarginSnappy));
    const auto writeHotkey = [&](const wchar_t* key, const Hotkey& hotkey) {
        writeString(file, key,
                    std::to_wstring(hotkey.vk) + L":" + std::to_wstring(hotkey.mods));
    };
    writeHotkey(L"zoomHotkey", zoomHotkey);
    writeHotkey(L"highlightSpotlight", highlightSpotlight);
    writeString(file, L"inkStyle", inkStyleName(inkStyle));
    writeString(file, L"inkColor", inkColorName(inkColor));
    writeString(file, L"strokeWidth", std::to_wstring(strokeWidth));
    writeString(file, L"clickEffect", clickEffectName(clickEffect));
    writeString(file, L"clickColor", clickColorName(clickColor));
    return true;
}

InkLook Settings::inkLook() const {
    InkLook look;
    look.style = inkStyle;
    look.width = strokeWidth;
    switch (inkColor) {
        case InkPalette::Red: look.color = {1.0f, 0.23f, 0.19f, 1.0f}; break;
        case InkPalette::Yellow: look.color = {1.0f, 0.82f, 0.10f, 1.0f}; break;
        case InkPalette::Green: look.color = {0.20f, 0.88f, 0.50f, 1.0f}; break;
        case InkPalette::Blue: look.color = {0.25f, 0.60f, 1.00f, 1.0f}; break;
        case InkPalette::Pink: look.color = {1.0f, 0.33f, 0.72f, 1.0f}; break;
        case InkPalette::White: look.color = {1.0f, 1.0f, 1.0f, 1.0f}; break;
    }
    return look;
}

Rgba Settings::clickRgba() const {
    switch (clickColor) {
        case ClickPalette::Blue: return {0.20f, 0.55f, 1.00f, 1.0f};
        case ClickPalette::Yellow: return {1.00f, 0.80f, 0.10f, 1.0f};
        case ClickPalette::Red: return {1.00f, 0.30f, 0.30f, 1.0f};
        case ClickPalette::Green: return {0.20f, 0.85f, 0.50f, 1.0f};
        case ClickPalette::White: return {1.00f, 1.00f, 1.00f, 1.0f};
    }
    return {0.20f, 0.55f, 1.00f, 1.0f};
}

FollowPreset Settings::followPreset() const {
    FollowPreset preset = FollowPreset::calm;
    switch (follow) {
        case FollowStyle::Calm: preset = FollowPreset::calm; break;
        case FollowStyle::Standard: preset = FollowPreset::standard; break;
        case FollowStyle::Snappy: preset = FollowPreset::snappy; break;
    }
    // Keep each preset's pan speed; only the edge trigger distance is user-configurable.
    // Defaults are 6% / 4% / 2% from the edge. After the camera follows, it settles a little
    // further inward than the trigger so it makes one decisive move.
    Real margin = followMarginCalm;
    Real gap = 0.08;
    switch (follow) {
        case FollowStyle::Calm:
            margin = followMarginCalm;
            gap = 0.08;
            break;
        case FollowStyle::Standard:
            margin = followMarginStandard;
            gap = 0.07;
            break;
        case FollowStyle::Snappy:
            margin = followMarginSnappy;
            gap = 0.06;
            break;
    }
    preset.margin = margin;
    preset.settleMargin = std::min(margin + gap, 0.45);
    return preset;
}

}  // namespace lz
