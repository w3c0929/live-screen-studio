#pragma once

#include <vector>

#include "geometry.h"

namespace lz {

/// Turns a stroke path into a sine wave running along it.
struct WavyPath {
    /// Offsets the polyline perpendicular to its direction by `amplitude * sin(2*pi*s / wavelength)`,
    /// where `s` is the distance travelled along the path. The phase starts at zero so the wave
    /// begins exactly on the drawn point.
    static std::vector<Point> offset(const std::vector<Point>& points, Real amplitude,
                                     Real wavelength);
};

}  // namespace lz
