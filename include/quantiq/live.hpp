#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "quantiq/alpaca.hpp"
#include "quantiq/engine.hpp"
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
    explicit LiveTrader(LiveConfig config);

    /// Loops over trading sessions until stopped. Sleeps through nights,
    /// weekends and holidays by asking Alpaca when the market next opens
    /// rather than by hardcoding a calendar.
    void run();

    /// Set from the signal handler; also stops an in-progress session.
    static std::atomic<bool>& stop_flag();

private:
    void trade_session(const MarketClock& clock);
    void reconcile();
    std::vector<Symbol> all_symbols() const;

    LiveConfig config_;
    AlpacaVenue venue_;
    Journal journal_;
    Risk risk_;
    /// One engine per symbol: a strategy instance carries the state of one
    /// position, so two symbols need two instances.
    std::map<Symbol, std::unique_ptr<Strategy>> strategies_;
    std::map<Symbol, std::unique_ptr<Engine>> engines_;
};

}  // namespace quantiq
