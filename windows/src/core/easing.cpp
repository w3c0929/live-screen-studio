#include "easing.h"

#include <algorithm>
#include <cmath>

namespace lz {

double Easing::easeInOutCubic(double t) {
    t = std::min(std::max(t, 0.0), 1.0);
    return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
}

}  // namespace lz
