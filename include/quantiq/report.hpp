#pragma once

#include <ostream>
#include <string>
#include <vector>

#include "quantiq/types.hpp"

namespace quantiq {

/// One completed round trip: an exit matched against the entry it closed.
struct Trade {
    std::string strategy;
    Symbol symbol;
    Timestamp opened;
    Timestamp closed;
    Quantity quantity;
    Price entry;
    Price exit;
    Money profit;
    std::string reason;
};

struct StrategyResult {
    std::string strategy;
    int trades = 0;
    int wins = 0;
    Money net;
    Money max_drawdown;

    /// Cumulative profit after each closed trade -- what the equity curve plots.
    std::vector<double> equity;

    double win_rate() const {
        return trades == 0 ? 0.0 : 100.0 * static_cast<double>(wins) / trades;
    }
};

/// Replays the journal and pairs each exit against the entry that preceded it,
/// which is what turns a list of fills into a list of round trips. Only
/// completed round trips count -- an open position has no profit yet.
std::vector<StrategyResult> summarize(const std::string& journal_path);

/// The same pass, keeping each round trip rather than only the totals.
std::vector<Trade> trades_of(const std::string& journal_path);

void print_report(std::ostream& os, const std::vector<StrategyResult>& results);

}  // namespace quantiq
