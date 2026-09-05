#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "quantiq/alpaca.hpp"
#include "quantiq/engine.hpp"
#include "quantiq/dry_run.hpp"
#include "quantiq/feed.hpp"

namespace quantiq {

struct StrategyConfig {
    std::string name;
    std::vector<Symbol> symbols;
    StrategyParams params;
};

struct LiveConfig {
    double position_fraction = 0.10;
    double rebalance_band = 0.20;
    std::chrono::seconds poll_interval{60};
    RiskLimits risk;
    std::vector<StrategyConfig> strategies;
    std::string journal_path = "journal/live.jsonl";

    /// Reads the JSON config and rejects a universe where two strategies share
    /// a symbol -- they would each try to set that position and undo each
    /// other, and the report could no longer say which one earned what.
    static LiveConfig from_file(const std::string& path);
};

/// Runs strategies against the live paper account.
///
/// Three things happen concurrently: the feed thread polls for bars, this
/// thread turns them into orders, and a signal handler can stop both. The
/// clock loop wraps all of it, so the process can be left running and it
/// wakes itself when the market opens.
class LiveTrader {
public:
    explicit LiveTrader(LiveConfig config, bool dry_run = false);

    /// Loops over trading sessions until stopped. Sleeps through nights,
    /// weekends and holidays by asking Alpaca when the market next opens
    /// rather than by hardcoding a calendar.
    void run();

    /// One pass and exit: reconcile, act on the most recent completed bar for
    /// each symbol, and stop.
    ///
    /// This is what a scheduled job runs. Daily bars mean one decision per
    /// symbol per day, so keeping a container alive around the clock would bill
    /// twenty-four hours for a few seconds of work.
    ///
    /// Acts only on a *completed* bar. Today's bar keeps changing until the
    /// close, and trading a partial bar means the decision would have been
    /// different an hour later.
    void run_once();

    /// Call before reading the journal while the trader is still alive.
    void flush() { journal_.flush(); }

    /// Number of bars pulled to warm a strategy up before it is asked for a
    /// decision. A fresh process has no memory of yesterday, so without this a
    /// scheduled job would restart every indicator from nothing each morning
    /// and never accumulate the history any of them need.
    static constexpr int kWarmupBars = 250;

    /// Set from the signal handler; also stops an in-progress session.
    static std::atomic<bool>& stop_flag();

private:
    void trade_session(const MarketClock& clock);
    void reconcile();
    std::vector<Symbol> all_symbols() const;

    LiveConfig config_;
    AlpacaVenue venue_;
    std::unique_ptr<DryRunVenue> dry_;
    Venue* trading_;   ///< venue_, or the dry-run wrapper around it
    Journal journal_;
    Risk risk_;
    /// One engine per symbol: a strategy instance carries the state of one
    /// position, so two symbols need two instances.
    std::map<Symbol, std::unique_ptr<Strategy>> strategies_;
    std::map<Symbol, std::unique_ptr<Engine>> engines_;
};

}  // namespace quantiq
