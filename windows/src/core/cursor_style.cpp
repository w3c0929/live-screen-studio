#include "cursor_style.h"

#include <cmath>

namespace lz {

double ClickEffectMath::duration(ClickEffect effect) {
    switch (effect) {
        case ClickEffect::None: return 0;
        case ClickEffect::Ripple: return 0.5;
        case ClickEffect::Halo: return 0.45;
    }
    return 0;
}

std::optional<ClickAppearance> ClickEffectMath::appearance(ClickEffect effect, double age) {
    const double d = duration(effect);
    if (effect == ClickEffect::None || age < 0 || age >= d) return std::nullopt;
    const Real t = static_cast<Real>(age / d);
    const Real eased = 1 - std::pow(1 - t, 3);
    const Real fade = 1 - t;
    switch (effect) {
        case ClickEffect::None:
            return std::nullopt;
        case ClickEffect::Ripple:
            return ClickAppearance{5 + 23 * eased, 3 - 1.8 * eased, 0.9 * fade, false};
        case ClickEffect::Halo:
            return ClickAppearance{9 + 13 * eased, 0, 0.5 * fade, true};
    }
    return std::nullopt;
}

}  // namespace lz
