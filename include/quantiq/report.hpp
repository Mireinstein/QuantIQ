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

/// One day of the strategy's life: what it was worth, what the instrument
/// closed at, and whether capital was deployed.
struct MarkPoint {
    Timestamp ts;
    double equity = 0.0;
    double close = 0.0;
    bool invested = false;
};

/// The numbers that make a P&L figure interpretable.
///
/// A net profit on its own says nothing: it depends on how much capital was
/// committed, for how long, against what the market did anyway, and at what
/// risk. Each of these answers one of those.
struct Metrics {
    double total_return = 0.0;      ///< Over the whole period, as a fraction.
    double benchmark_return = 0.0;  ///< Buying the instrument and holding it.
    double cagr = 0.0;
    double benchmark_cagr = 0.0;
    double sharpe = 0.0;            ///< Return per unit of volatility, annualised.
    double sortino = 0.0;           ///< As Sharpe, but only downside counts.
    double calmar = 0.0;            ///< CAGR against the worst drawdown.
    double max_drawdown_pct = 0.0;
    double exposure = 0.0;          ///< Fraction of days holding a position.
    double profit_factor = 0.0;     ///< Gross wins over gross losses.
    double avg_win = 0.0;
    double avg_loss = 0.0;
    double expectancy = 0.0;        ///< Expected profit per trade.
    int days = 0;
    bool valid = false;
};

struct StrategyResult {
    std::string strategy;
    int trades = 0;
    int wins = 0;
    Money net;
    Money max_drawdown;

    /// Cumulative profit after each closed trade.
    std::vector<double> equity;

    /// Daily marks, which is what the curves are actually drawn from -- a
    /// series indexed by trade cannot be lined up against the market or against
    /// another strategy that traded a different number of times.
    std::vector<MarkPoint> marks;
    Metrics metrics;

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

/// Fills in `metrics` from `marks`. Separate from the journal pass so the same
/// arithmetic can be applied to any equity series.
Metrics compute_metrics(const std::vector<MarkPoint>& marks, const std::vector<Trade>& trades);

void print_report(std::ostream& os, const std::vector<StrategyResult>& results);

}  // namespace quantiq
