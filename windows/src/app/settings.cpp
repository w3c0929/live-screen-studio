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
    blankSystemCursor = readBool(file, L"blankSystemCursor", blankSystemCursor);

    const std::wstring followName = readString(file, L"follow", L"calm");
    if (followName == L"standard") {
        follow = FollowStyle::Standard;
    } else if (followName == L"snappy") {
        follow = FollowStyle::Snappy;
    } else {
        follow = FollowStyle::Calm;
    }

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
    writeString(file, L"blankSystemCursor", blankSystemCursor ? L"1" : L"0");
    const wchar_t* followName = L"calm";
    if (follow == FollowStyle::Standard) followName = L"standard";
    if (follow == FollowStyle::Snappy) followName = L"snappy";
    writeString(file, L"follow", followName);
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
    // Windows feels better when the camera waits until the magnified cursor is close to the
    // screen edge. The macOS presets use a much wider safe zone (25% of the screen), which
    // made the canvas start moving while the pointer still looked far from the edge.
    // Keep each preset's pan speed, but move the trigger closer to the edge.
    switch (follow) {
        case FollowStyle::Calm:
            preset.margin = 0.06;
            preset.settleMargin = 0.14;
            break;
        case FollowStyle::Standard:
            preset.margin = 0.04;
            preset.settleMargin = 0.11;
            break;
        case FollowStyle::Snappy:
            preset.margin = 0.02;
            preset.settleMargin = 0.08;
            break;
    }
    return preset;
}

}  // namespace lz
