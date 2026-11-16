#pragma once

#include <algorithm>
#include <cmath>

namespace lz {

/// Zoom levels offered by the settings combo and stepped through by the mouse wheel.
inline constexpr double kZoomSteps[] = {1.25, 1.5, 1.75, 2.0, 2.5, 3.0, 4.0};
inline constexpr int kZoomStepCount = static_cast<int>(sizeof(kZoomSteps) / sizeof(kZoomSteps[0]));

inline int nearestZoomStep(double zoom) {
    int best = 0;
    double distance = std::abs(kZoomSteps[0] - zoom);
    for (int i = 1; i < kZoomStepCount; ++i) {
        const double current = std::abs(kZoomSteps[i] - zoom);
        if (current < distance) {
            distance = current;
            best = i;
        }
    }
    return best;
}

}  // namespace lz
