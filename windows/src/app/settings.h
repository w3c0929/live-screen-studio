#pragma once

#include <string>

#include "cursor_style.h"
#include "follow_preset.h"
#include "style.h"

namespace lz {

enum class FollowStyle {
    Calm,
    Standard,
    Snappy,
};

enum class InkPalette {
    Red,
    Yellow,
    Green,
    Blue,
    Pink,
    White,
};

enum class ClickPalette {
    Blue,
    Yellow,
    Red,
    Green,
    White,
};

/// A recorded shortcut. Modifier bits: 1 Ctrl, 2 Alt, 4 Shift, 8 Win.
struct Hotkey {
    unsigned vk = 0;
    unsigned mods = 0;

    [[nodiscard]] bool valid() const { return vk != 0 && mods != 0; }
    bool operator==(const Hotkey&) const = default;
};

[[nodiscard]] std::wstring HotkeyText(const Hotkey& hotkey);

struct Settings {
    bool enabled = true;
    double defaultZoom = 2.0;
    double minZoom = 1.25;
    double maxZoom = 4.0;
    FollowStyle follow = FollowStyle::Calm;
    double followMarginCalm = 0.06;
    double followMarginStandard = 0.04;
    double followMarginSnappy = 0.02;
    bool smoothImage = false;
    bool excludeObs = true;
    bool drawWhileZoomed = true;

    Hotkey highlightSpotlight{0x34, 5};  // Ctrl+Shift+4

    double cursorSize = 1.2;
    CursorStyle cursorStyle = CursorStyle::Classic;
    bool smoothCursor = false;
    bool hideIdleCursor = false;
    double idleDelay = 3.0;

    InkStyle inkStyle = InkStyle::Marker;
    InkPalette inkColor = InkPalette::Red;
    double strokeWidth = 5.0;
    ClickEffect clickEffect = ClickEffect::Ripple;
    ClickPalette clickColor = ClickPalette::Blue;

    bool load();
    bool save() const;
    [[nodiscard]] FollowPreset followPreset() const;
    [[nodiscard]] InkLook inkLook() const;
    [[nodiscard]] Rgba clickRgba() const;
    [[nodiscard]] std::wstring path() const;
};

}  // namespace lz
