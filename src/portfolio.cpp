#include "quantiq/portfolio.hpp"

namespace quantiq {

void Portfolio::apply(const Fill& fill) {
    Position& pos = positions_[fill.symbol];
    pos.symbol = fill.symbol;

    if (fill.side == Side::Buy) {
        // Weighted average cost, computed in ticks so the division is the only
        // place precision is lost and it happens once per fill rather than
        // compounding across them.
        const std::int64_t existing = pos.avg_price.ticks() * pos.quantity;
        const std::int64_t added = fill.price.ticks() * fill.quantity;
        const Quantity total = pos.quantity + fill.quantity;
        pos.avg_price = Price::from_ticks(total == 0 ? 0 : (existing + added) / total);
        pos.quantity = total;
        return;
    }

    const Quantity sold = std::min(fill.quantity, pos.quantity);
    realized_ += Money::from_ticks((fill.price.ticks() - pos.avg_price.ticks()) * sold);
    pos.quantity -= sold;
    if (pos.quantity == 0) positions_.erase(fill.symbol);
}

const Position* Portfolio::find(const Symbol& symbol) const {
    auto it = positions_.find(symbol);
    return it == positions_.end() ? nullptr : &it->second;
}

bool Portfolio::holds(const Symbol& symbol) const {
    const Position* p = find(symbol);
    return p != nullptr && p->quantity > 0;
}

std::size_t Portfolio::open_positions() const { return positions_.size(); }

Money Portfolio::market_value(const std::unordered_map<Symbol, Price>& marks) const {
    Money total;
    for (const auto& [symbol, pos] : positions_) {
        auto mark = marks.find(symbol);
        if (mark == marks.end()) continue;
        total += notional(mark->second, pos.quantity);
    }
    return total;
}

Money Portfolio::unrealized(const std::unordered_map<Symbol, Price>& marks) const {
    Money total;
    for (const auto& [symbol, pos] : positions_) {
        auto mark = marks.find(symbol);
        if (mark == marks.end()) continue;
        total += Money::from_ticks((mark->second.ticks() - pos.avg_price.ticks()) * pos.quantity);
    }
    return total;
}

}  // namespace quantiq
