#pragma once

#include "geometry.h"

namespace lz {

/// How the zoomed viewport chases the cursor.
struct FollowPreset {
    /// Safe-zone margin on each edge, as a fraction of the viewport size.
    Real margin = 0.25;
    /// Where the cursor ends up (distance from the edge it crossed) after the camera follows.
    Real settleMargin = 0.33;
    /// Time the camera takes to roughly catch up with its target.
    double smoothTime = 0.6;
    /// Maximum pan speed as seen on screen, in points per second.
    Real maxVisualSpeed = 900;

    static const FollowPreset calm;
    static const FollowPreset standard;
    static const FollowPreset snappy;
};

}  // namespace lz
