#pragma once

#include <string>

#include "follow_preset.h"

namespace lz {

enum class FollowStyle {
    Calm,
    Standard,
    Snappy,
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

    bool load();
    bool save() const;
    [[nodiscard]] FollowPreset followPreset() const;
    [[nodiscard]] std::wstring path() const;
};

}  // namespace lz
