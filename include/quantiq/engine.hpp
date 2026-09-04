#pragma once

#include <string>
#include <unordered_map>

#include "quantiq/journal.hpp"
#include "quantiq/portfolio.hpp"
#include "quantiq/risk.hpp"
#include "quantiq/sizer.hpp"
#include "quantiq/strategy.hpp"
#include "quantiq/venue.hpp"

namespace quantiq {

struct EngineStats {
    std::size_t bars = 0;
    std::size_t signals = 0;
    std::size_t fills = 0;
    std::size_t rejected = 0;
};

/// The loop: pull a bar, ask the strategy what it wants to hold, work out how
/// many shares that is, and trade the difference between that and what is
/// actually held.
///
/// Trading the difference rather than acting on a buy/sell instruction is what
/// makes this safe to restart and safe to run repeatedly: if the position
/// already matches the target, the difference is zero and nothing is sent.
///
/// Nothing here knows whether the bars came from a CSV or from Alpaca. That is
/// the reason the Venue interface exists.
class Engine {
public:
    /// `band` is how far the held position may drift from the target before it
    /// is worth trading. Without it, a rising price alone shrinks the share
    /// count a fixed weight implies, and the bot sells a handful of shares
    /// every bar to chase it -- paying spread to correct noise.
    Engine(Venue& venue, Strategy& strategy, Journal& journal, Risk& risk, Sizer sizer = Sizer{},
           double band = 0.20);

    EngineStats run(bool verbose = false);

    /// One bar through the whole pipeline. Exposed so the live loop can drive
    /// it from a queue rather than from `run`.
    void on_bar(const Bar& bar, bool verbose = false);

    /// Adopts positions that already exist at the broker, so a restart does not
    /// mistake a held position for a flat one and buy it twice.
    void adopt(const std::vector<Position>& positions);

    const Portfolio& portfolio() const noexcept { return portfolio_; }
    const EngineStats& stats() const noexcept { return stats_; }

private:
    Venue& venue_;
    Strategy& strategy_;
    Journal& journal_;
    Risk& risk_;
    Sizer sizer_;
    double band_;
    Portfolio portfolio_;
    std::unordered_map<Symbol, Price> marks_;
    EngineStats stats_;
};

}  // namespace quantiq
