#pragma once

#include "quantiq/types.hpp"

namespace quantiq {

/// Turns a target weight into a share count.
///
/// Sizing by share count rather than by value is a trap: 50 shares of a $900
/// stock is a 45% position on a $100k account and 50 shares of a $12 stock is
/// under 1%, so a report comparing strategies would mostly be comparing which
/// of them happened to trade expensive names. Sizing off equity makes every
/// position the same size, and makes the comparison mean something.
class Sizer {
public:
    /// `fraction` is the share of equity a full (weight 1.0) position takes.
    explicit Sizer(double fraction = 0.10);

    Quantity shares(double weight, Price price, Money equity) const;

    double fraction() const noexcept { return fraction_; }

private:
    double fraction_;
};

}  // namespace quantiq
