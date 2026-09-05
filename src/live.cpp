#include "quantiq/live.hpp"

#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <set>

#include <nlohmann/json.hpp>

#include "quantiq/errors.hpp"
#include "quantiq/strategies.hpp"

namespace quantiq {

namespace {

std::mutex g_sleep_mutex;
std::condition_variable g_sleep_cv;

/// Sleeps until `until`, or until stopped. A plain sleep_for would leave the
/// process unkillable for hours over a weekend.
void sleep_until_or_stop(Timestamp until, std::atomic<bool>& stop) {
    std::unique_lock<std::mutex> lock(g_sleep_mutex);
    g_sleep_cv.wait_until(lock, until, [&stop] { return stop.load(); });
}

std::string clock_line(const MarketClock& clock) {
    return clock.is_open ? "open until " + to_date(clock.next_close)
                         : "closed until " + to_date(clock.next_open);
}

}  // namespace

std::atomic<bool>& LiveTrader::stop_flag() {
    static std::atomic<bool> flag{false};
    return flag;
}

LiveConfig LiveConfig::from_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw ConfigError("cannot read config: " + path);

    const auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded()) throw ConfigError("config is not valid JSON: " + path);

    LiveConfig config;
    config.position_fraction = j.value("position_fraction", 0.10);
    config.rebalance_band = j.value("rebalance_band", 0.20);
    config.poll_interval = std::chrono::seconds(j.value("poll_seconds", 60));
    config.journal_path = j.value("journal", config.journal_path);

    if (j.contains("risk")) {
        const auto& r = j.at("risk");
        config.risk.max_positions = r.value("max_positions", config.risk.max_positions);
        config.risk.max_drawdown = r.value("max_drawdown", config.risk.max_drawdown);
        config.risk.max_daily_loss = r.value("max_daily_loss", config.risk.max_daily_loss);
    }

    std::set<Symbol> claimed;
    for (const auto& s : j.at("strategies")) {
        StrategyConfig strategy;
        strategy.name = s.at("name").get<std::string>();
        strategy.symbols = s.at("symbols").get<std::vector<Symbol>>();

        // Bound to a local first: `value()` returns by value, and iterating
        // the temporary it produces leaves the iterator dangling.
        const nlohmann::json params = s.value("params", nlohmann::json::object());
        for (const auto& [key, number] : params.items()) {
            strategy.params.values[key] = number.get<double>();
        }
        for (const auto& symbol : strategy.symbols) {
            if (!claimed.insert(symbol).second) {
                throw ConfigError(symbol + " is claimed by more than one strategy; " +
                                  "two strategies cannot both set one position");
            }
        }
        config.strategies.push_back(std::move(strategy));
    }

    if (config.strategies.empty()) throw ConfigError("config lists no strategies");
    return config;
}

LiveTrader::LiveTrader(LiveConfig config, bool dry_run)
    : config_(std::move(config)), journal_(config_.journal_path), risk_(config_.risk) {
    register_builtin_strategies();

    if (dry_run) dry_ = std::make_unique<DryRunVenue>(venue_);
    trading_ = dry_ ? static_cast<Venue*>(dry_.get()) : static_cast<Venue*>(&venue_);

    const Sizer sizer(config_.position_fraction);
    for (const auto& spec : config_.strategies) {
        for (const auto& symbol : spec.symbols) {
            strategies_[symbol] = StrategyRegistry::instance().create(spec.name, spec.params);
            engines_[symbol] = std::make_unique<Engine>(*trading_, *strategies_[symbol], journal_,
                                                        risk_, sizer, config_.rebalance_band);
        }
    }
}

std::vector<Symbol> LiveTrader::all_symbols() const {
    std::vector<Symbol> symbols;
    for (const auto& [symbol, _] : engines_) symbols.push_back(symbol);
    return symbols;
}

void LiveTrader::reconcile() {
    // The broker is the source of truth. Anything held at the start of a
    // session is adopted, so a restart does not mistake a position for a flat
    // book and buy it a second time.
    const auto positions = venue_.positions();
    for (const auto& position : positions) {
        auto engine = engines_.find(position.symbol);
        if (engine == engines_.end()) {
            std::cout << "  holding " << position.symbol << " x" << position.quantity
                      << " that no strategy owns -- left alone\n";
            continue;
        }
        engine->second->adopt({position});
        std::cout << "  adopted " << position.symbol << " x" << position.quantity << '\n';
    }
    if (positions.empty()) std::cout << "  flat\n";
}

void LiveTrader::trade_session(const MarketClock& clock) {
    BoundedQueue<FeedMessage> queue(256);
    PollingFeed feed(venue_, all_symbols(), queue, config_.poll_interval);

    journal_.session("session_open", to_date(clock.now));
    std::cout << "  session open, watching " << engines_.size() << " symbols\n";

    int consecutive_errors = 0;

    while (!stop_flag().load() && !risk_.halted()) {
        // Warm up the history each strategy needs, then take bars as they land.
        auto message = queue.pop();
        if (!message) break;

        std::visit(
            [&](auto&& m) {
                using T = std::decay_t<decltype(m)>;

                if constexpr (std::is_same_v<T, Bar>) {
                    consecutive_errors = 0;
                    auto engine = engines_.find(m.symbol);
                    if (engine == engines_.end()) return;
                    std::cout << "  " << to_date(m.ts) << ' ' << m.symbol << " close " << m.close
                              << '\n';
                    engine->second->on_bar(m, true);
                } else if constexpr (std::is_same_v<T, FeedError>) {
                    // One failed poll is weather. A run of them means the feed
                    // is down, and trading on stale prices is worse than not
                    // trading at all.
                    if (++consecutive_errors >= 10) {
                        risk_.halt("feed failing: " + m.what);
                    }
                    journal_.session("feed_error", m.what);
                } else if constexpr (std::is_same_v<T, SessionClose>) {
                    stop_flag().store(true);
                }
            },
            *message);

        if (std::chrono::system_clock::now() >= clock.next_close) break;
    }

    feed.stop();
    journal_.session("session_close", risk_.halted() ? risk_.halt_reason() : "bell");
}

void LiveTrader::run_once() {
    std::cout << "\n  account   " << venue_.account().equity << "  on " << trading_->name()
              << '\n';
    reconcile();

    const auto clock = venue_.clock();
    std::cout << "  market    " << clock_line(clock) << '\n';

    const auto working = venue_.symbols_with_open_orders();

    for (auto& [symbol, engine] : engines_) {
        try {
            if (working.count(symbol) != 0) {
                std::cout << "  " << symbol << "  order already working, left alone\n";
                continue;
            }

            auto bars = venue_.history(symbol, kWarmupBars);
            if (bars.empty()) {
                std::cout << "  " << symbol << "  no bars\n";
                continue;
            }

            // Today's bar is still forming while the market is open, and a
            // decision taken on a partial bar would have been a different
            // decision an hour later.
            if (clock.is_open && bars.size() > 1) bars.pop_back();

            // Everything but the last bar rebuilds the indicator state this
            // process was started without. Only the last one is allowed to
            // trade.
            for (std::size_t i = 0; i + 1 < bars.size(); ++i) {
                strategies_[symbol]->on_bar(bars[i]);
            }

            const Bar& bar = bars.back();
            std::cout << "  " << to_date(bar.ts) << ' ' << symbol << " close " << bar.close
                      << "  (warmed on " << bars.size() - 1 << " bars)\n";
            engine->on_bar(bar, true);
        } catch (const Error& e) {
            std::cout << "  " << symbol << "  " << e.what() << '\n';
            journal_.session("error", symbol + ": " + e.what());
        }
    }

    journal_.session("run_once", to_date(clock.now));
    if (risk_.halted()) std::cout << "\n  HALTED: " << risk_.halt_reason() << '\n';
    std::cout << '\n';
}

void LiveTrader::run() {
    std::cout << "\n  account   " << venue_.account().equity << '\n';
    reconcile();

    while (!stop_flag().load()) {
        const auto clock = venue_.clock();
        std::cout << "  market    " << clock_line(clock) << '\n';

        if (!clock.is_open) {
            sleep_until_or_stop(clock.next_open, stop_flag());
            continue;
        }

        trade_session(clock);

        if (risk_.halted()) {
            std::cout << "\n  HALTED: " << risk_.halt_reason() << "\n\n";
            return;
        }
    }
    std::cout << "\n  stopped\n\n";
}

}  // namespace quantiq
