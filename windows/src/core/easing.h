#pragma once

namespace lz {

struct Easing {
    /// Slow start, slow finish; velocity is zero at both ends.
    static double easeInOutCubic(double t);
};

}  // namespace lz
