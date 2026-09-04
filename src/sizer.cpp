#include "quantiq/sizer.hpp"

#include <algorithm>
#include <cmath>

#include "quantiq/errors.hpp"

namespace quantiq {

Sizer::Sizer(double fraction) : fraction_(fraction) {
    if (fraction <= 0.0 || fraction > 1.0) {
        throw ConfigError("position fraction must be between 0 and 1");
    }
}

Quantity Sizer::shares(double weight, Price price, Money equity) const {
    if (price.ticks() <= 0 || equity.ticks() <= 0) return 0;

    const double budget = std::clamp(weight, 0.0, 1.0) * fraction_ * equity.to_double();

    // Rounded down: buying a fraction of a share is not on the table, and
    // rounding up would push the position past the budget it was given.
    return static_cast<Quantity>(std::floor(budget / price.to_double()));
}

}  // namespace quantiq
