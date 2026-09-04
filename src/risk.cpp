#include "quantiq/risk.hpp"

namespace quantiq {

bool Risk::allow(const Order& order, const Portfolio& portfolio, std::string& why_not) {
    if (halted_.load()) {
        why_not = "halted: " + halt_reason_;
        return false;
    }

    if (order.side == Side::Sell && !portfolio.holds(order.symbol)) {
        why_not = "no position to sell";
        return false;
    }

    // A new name is what consumes a slot; adding to something already held is
    // not taking on another position's worth of concentration risk.
    const bool opens_new = order.side == Side::Buy && !portfolio.holds(order.symbol);
    if (opens_new && portfolio.open_positions() >= limits_.max_positions) {
        why_not = "at max positions";
        return false;
    }

    return true;
}

void Risk::observe_equity(Money equity) {
    if (!seen_equity_ || equity > peak_equity_) {
        peak_equity_ = equity;
        seen_equity_ = true;
        return;
    }

    const double peak = peak_equity_.to_double();
    if (peak <= 0.0) return;

    const double drawdown = (peak - equity.to_double()) / peak;
    if (drawdown > limits_.max_drawdown) {
        halt("drawdown " + std::to_string(drawdown * 100.0) + "% exceeded limit");
    }
}

void Risk::halt(const std::string& reason) {
    halt_reason_ = reason;
    halted_.store(true);
}

}  // namespace quantiq
