#pragma once

#include <ostream>
#include <string>
#include <vector>

#include "quantiq/types.hpp"

namespace quantiq {

struct StrategyResult {
    std::string strategy;
    int trades = 0;
    int wins = 0;
    Money net;
    Money max_drawdown;

    double win_rate() const {
        return trades == 0 ? 0.0 : 100.0 * static_cast<double>(wins) / trades;
    }
};

/// Replays the journal and pairs each exit against the entry that preceded it,
/// which is what turns a list of fills into a list of round-trip trades. Only
/// completed round trips count -- an open position has no profit yet.
std::vector<StrategyResult> summarize(const std::string& journal_path);

void print_report(std::ostream& os, const std::vector<StrategyResult>& results);

}  // namespace quantiq
