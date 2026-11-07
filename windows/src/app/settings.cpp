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
    return true;
}

FollowPreset Settings::followPreset() const {
    switch (follow) {
        case FollowStyle::Calm: return FollowPreset::calm;
        case FollowStyle::Standard: return FollowPreset::standard;
        case FollowStyle::Snappy: return FollowPreset::snappy;
    }
    return FollowPreset::calm;
}

}  // namespace lz
