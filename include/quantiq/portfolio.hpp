#pragma once

#include <optional>
#include <unordered_map>
#include <vector>

#include "quantiq/types.hpp"

namespace quantiq {

struct Position {
    Symbol symbol;
    Quantity quantity = 0;
    Price avg_price;
};

/// Tracks what is held and what has been made. Realised profit is booked at the
/// moment a position is reduced; everything still open is unrealised and needs
/// a current price to value, which is why marks are passed in rather than
/// cached here.
class Portfolio {
public:
    void apply(const Fill& fill);

    const Position* find(const Symbol& symbol) const;
    bool holds(const Symbol& symbol) const;
    std::size_t open_positions() const;

    Money realized() const noexcept { return realized_; }
    Money unrealized(const std::unordered_map<Symbol, Price>& marks) const;

    /// What the open positions are worth right now. Equity is this plus cash,
    /// and equity is what a drawdown percentage has to be measured against --
    /// profit alone starts at zero, and every percentage of zero is infinite.
    Money market_value(const std::unordered_map<Symbol, Price>& marks) const;

    const std::unordered_map<Symbol, Position>& positions() const noexcept {
        return positions_;
    }

private:
    std::unordered_map<Symbol, Position> positions_;
    Money realized_;
};

}  // namespace quantiq
