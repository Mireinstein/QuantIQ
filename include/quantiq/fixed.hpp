#pragma once

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>

namespace quantiq {

/// Fixed-point decimal with four places, tagged so that a Price cannot be
/// silently assigned to a Money. Prices and cash are held as integers because
/// binary floating point cannot represent 0.01 exactly, and an accumulating
/// rounding error in a position's cost basis is a real bug rather than a
/// theoretical one.
template <typename Tag>
class Fixed4 {
public:
    static constexpr std::int64_t kScale = 10000;

    constexpr Fixed4() = default;

    static constexpr Fixed4 from_ticks(std::int64_t ticks) noexcept {
        Fixed4 v;
        v.ticks_ = ticks;
        return v;
    }

    static Fixed4 from_double(double d) noexcept {
        return from_ticks(static_cast<std::int64_t>(std::llround(d * kScale)));
    }

    constexpr std::int64_t ticks() const noexcept { return ticks_; }
    constexpr double to_double() const noexcept {
        return static_cast<double>(ticks_) / kScale;
    }

    constexpr Fixed4 operator+(Fixed4 o) const noexcept { return from_ticks(ticks_ + o.ticks_); }
    constexpr Fixed4 operator-(Fixed4 o) const noexcept { return from_ticks(ticks_ - o.ticks_); }
    constexpr Fixed4 operator-() const noexcept { return from_ticks(-ticks_); }
    constexpr Fixed4& operator+=(Fixed4 o) noexcept { ticks_ += o.ticks_; return *this; }
    constexpr Fixed4& operator-=(Fixed4 o) noexcept { ticks_ -= o.ticks_; return *this; }

    constexpr auto operator<=>(const Fixed4&) const noexcept = default;
    constexpr bool operator==(const Fixed4&) const noexcept = default;

    std::string str() const {
        std::ostringstream os;
        os << std::fixed << std::setprecision(2) << to_double();
        return os.str();
    }

private:
    std::int64_t ticks_ = 0;
};

template <typename Tag>
std::ostream& operator<<(std::ostream& os, Fixed4<Tag> v) {
    return os << v.str();
}

using Price = Fixed4<struct PriceTag>;
using Money = Fixed4<struct MoneyTag>;
using Quantity = std::int64_t;

/// Crossing the tag boundary is deliberate and explicit: a price times a share
/// count is cash, and that is the only way to get from one to the other.
inline Money notional(Price p, Quantity q) {
    return Money::from_ticks(p.ticks() * q);
}

}  // namespace quantiq
