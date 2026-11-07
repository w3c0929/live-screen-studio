#pragma once

#include <optional>

#include "geometry.h"

namespace lz {

/// Pointer looks drawn by LiveZoom. Only the arrow is replaced (and, for `dot`, the pointing hand);
/// text, resize and other cursors keep their system image so their meaning stays clear.
enum class CursorStyle {
    System,
    Classic,
    Light,
    Ocean,
    Sunset,
    Mint,
    Dot,
    Spotlight,
};

struct ClickAppearance {
    Real radius = 0;
    Real lineWidth = 0;
    Real opacity = 0;
    bool filled = false;

    bool operator==(const ClickAppearance&) const = default;
};

enum class ClickEffect {
    None,
    Ripple,
    Halo,
};

struct ClickEffectMath {
    static double duration(ClickEffect effect);
    /// How a click mark looks `age` seconds after the click, in points at 1x cursor size; nullopt once faded.
    static std::optional<ClickAppearance> appearance(ClickEffect effect, double age);
};

}  // namespace lz
