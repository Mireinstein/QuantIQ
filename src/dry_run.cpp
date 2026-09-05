#include "quantiq/dry_run.hpp"

#include <iostream>

namespace quantiq {

Fill DryRunVenue::submit(const Order& order) {
    ++withheld_;
    std::cout << "  would " << to_string(order.side) << ' ' << order.quantity << ' ' << order.symbol
              << "  (" << order.reason << ")\n";

    // A zero-quantity fill is what the engine already treats as "accepted but
    // nothing happened", so nothing downstream needs to know this venue is not
    // real.
    return Fill{order.symbol, order.side, 0, Price{}, std::chrono::system_clock::now(), "dry-run"};
}

}  // namespace quantiq
