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

struct Settings {
    bool enabled = true;
    double defaultZoom = 2.0;
    double minZoom = 1.25;
    double maxZoom = 4.0;
    FollowStyle follow = FollowStyle::Calm;
    bool smoothImage = false;
    bool excludeObs = true;
    bool drawWhileZoomed = true;
    bool blankSystemCursor = false;

    double cursorSize = 1.2;
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
