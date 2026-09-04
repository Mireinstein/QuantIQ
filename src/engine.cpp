#include "quantiq/engine.hpp"

#include <iostream>

namespace quantiq {

Engine::Engine(Venue& venue, Strategy& strategy, Journal& journal, Risk& risk)
    : venue_(venue), strategy_(strategy), journal_(journal), risk_(risk) {}

EngineStats Engine::run(bool verbose) {
    while (auto bar = venue_.next_bar()) {
        ++stats_.bars;
        marks_[bar->symbol] = bar->close;

        risk_.observe_equity(venue_.account().cash + portfolio_.market_value(marks_));
        if (risk_.halted()) break;

        if (auto signal = strategy_.on_bar(*bar)) {
            ++stats_.signals;
            handle(*bar, *signal, verbose);
        }
    }
    return stats_;
}

void Engine::handle(const Bar& bar, const Signal& signal, bool verbose) {
    const Order order{bar.symbol, signal.side, signal.quantity, strategy_.name(), signal.reason};

    std::string why_not;
    if (!risk_.allow(order, portfolio_, why_not)) {
        ++stats_.rejected;
        journal_.rejected(order, why_not);
        if (verbose) {
            std::cout << "  " << to_date(bar.ts) << "  " << order.symbol << "  rejected: "
                      << why_not << '\n';
        }
        return;
    }

    const Fill fill = venue_.submit(order);
    portfolio_.apply(fill);
    journal_.fill(fill, order.strategy, order.reason);
    ++stats_.fills;

    if (verbose) {
        std::cout << "  " << to_date(fill.ts) << "   " << (fill.side == Side::Buy ? "buy " : "sell")
                  << "  " << fill.quantity << " @ " << fill.price << '\n';
    }
}

}  // namespace quantiq
