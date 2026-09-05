#include "quantiq/engine.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace quantiq {

Engine::Engine(Venue& venue, Strategy& strategy, Journal& journal, Risk& risk, Sizer sizer,
               double band)
    : venue_(venue), strategy_(strategy), journal_(journal), risk_(risk), sizer_(sizer),
      band_(band) {}

void Engine::adopt(const std::vector<Position>& positions) {
    for (const auto& p : positions) {
        if (p.quantity <= 0) continue;
        portfolio_.apply(Fill{p.symbol, Side::Buy, p.quantity, p.avg_price,
                              std::chrono::system_clock::now(), "adopted"});
        journal_.session("adopted", p.symbol + " " + std::to_string(p.quantity));
    }
}

EngineStats Engine::run(bool verbose) {
    while (auto bar = venue_.next_bar()) {
        on_bar(*bar, verbose);
        if (risk_.halted()) break;
    }
    return stats_;
}

void Engine::on_bar(const Bar& bar, bool verbose) {
    ++stats_.bars;
    marks_[bar.symbol] = bar.close;

    const Account account = venue_.account();
    const Money equity = account.cash + portfolio_.market_value(marks_);
    const Position* position = portfolio_.find(bar.symbol);

    journal_.mark(bar.ts, strategy_.name(), bar.symbol, equity, bar.close,
                  position == nullptr ? 0 : position->quantity);

    risk_.observe_equity(equity);
    if (risk_.halted()) return;

    const Target target = strategy_.on_bar(bar);

    const Quantity desired = sizer_.shares(target.weight, bar.close, account.equity);
    const Quantity current = position == nullptr ? 0 : position->quantity;
    const Quantity delta = desired - current;
    if (delta == 0) return;

    // Opening and closing always go through. Everything in between is a drift
    // correction, and only worth paying for once the drift is material.
    const bool opening_or_closing = current == 0 || desired == 0;
    const Quantity larger = std::max(desired, current);
    if (!opening_or_closing &&
        static_cast<double>(std::llabs(delta)) < band_ * static_cast<double>(larger)) {
        return;
    }

    ++stats_.signals;
    const Order order{bar.symbol,
                      delta > 0 ? Side::Buy : Side::Sell,
                      std::llabs(delta),
                      strategy_.name(),
                      target.reason};

    std::string why_not;
    if (!risk_.allow(order, portfolio_, bar.close, account, why_not)) {
        ++stats_.rejected;
        journal_.rejected(order, why_not);
        if (verbose) {
            std::cout << "  " << to_date(bar.ts) << "  " << order.symbol << "  rejected: "
                      << why_not << '\n';
        }
        return;
    }

    const Fill fill = venue_.submit(order);
    if (fill.quantity == 0) {
        journal_.rejected(order, "accepted but unfilled");
        return;
    }

    portfolio_.apply(fill);
    journal_.fill(fill, order.strategy, order.reason);
    ++stats_.fills;

    if (verbose) {
        std::cout << "  " << to_date(fill.ts) << "   "
                  << (fill.side == Side::Buy ? "buy " : "sell") << "  " << fill.quantity << " @ "
                  << fill.price << '\n';
    }
}

}  // namespace quantiq
