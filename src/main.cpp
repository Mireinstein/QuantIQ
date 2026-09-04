#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <csignal>

#include "quantiq/alpaca.hpp"
#include "quantiq/engine.hpp"
#include "quantiq/live.hpp"
#include "quantiq/errors.hpp"
#include "quantiq/mock_venue.hpp"
#include "quantiq/report.hpp"
#include "quantiq/strategies.hpp"

using namespace quantiq;

namespace {

int usage() {
    register_builtin_strategies();
    std::cerr << "usage:\n"
              << "  trader --replay FILE.csv [--strategy NAME|all] [--set key=value]...\n"
              << "  trader --report [--journal FILE]\n"
              << "  trader --live [--config FILE]\n"
              << "  trader --check\n"
              << "  trader --test-order SYMBOL QTY\n"
              << "  trader --list\n\n"
              << "strategies: ";
    for (const auto& name : StrategyRegistry::instance().names()) std::cerr << name << ' ';
    std::cerr << "\n\nexamples:\n"
              << "  fetch-bars AAPL 5y\n"
              << "  trader --replay bars/AAPL-5y.csv --strategy SmaCrossover --set fast=9 --set slow=21\n"
              << "  trader --report\n";
    return 2;
}

std::pair<std::string, double> parse_setting(const std::string& kv) {
    const auto eq = kv.find('=');
    if (eq == std::string::npos) throw ConfigError("--set expects key=value, got " + kv);
    return {kv.substr(0, eq), std::stod(kv.substr(eq + 1))};
}

int replay(const std::string& csv, const std::string& strategy_name, const StrategyParams& params,
           const std::string& journal_path) {
    const auto bars = MockVenue::bars_from_csv(csv);

    // "all" runs every registered strategy over the same bars into one journal,
    // which is the only way the report can put them side by side.
    std::vector<std::string> to_run;
    if (strategy_name == "all") {
        to_run = StrategyRegistry::instance().names();
    } else {
        to_run.push_back(strategy_name);
    }
    const bool verbose = to_run.size() == 1;

    std::cout << "\n  replaying " << bars.size() << " bars · " << to_date(bars.front().ts) << " → "
              << to_date(bars.back().ts) << "\n";

    Journal journal(journal_path);

    for (const auto& name : to_run) {
        auto strategy = StrategyRegistry::instance().create(name, params);
        MockVenue venue(bars);
        Risk risk;
        const Sizer sizer(params.get("fraction", 0.10));

        std::cout << "\n  " << strategy->name() << '\n';
        journal.session("replay_start", csv + " " + strategy->name());

        Engine engine(venue, *strategy, journal, risk, sizer);
        const auto stats = engine.run(verbose);

        std::cout << "    " << stats.signals << " signals · " << stats.fills << " fills · "
                  << stats.rejected << " rejected";
        if (risk.halted()) std::cout << " · HALTED: " << risk.halt_reason();
        std::cout << '\n';
    }

    print_report(std::cout, summarize(journal_path));
    return 0;
}

/// Ctrl-C sets the flag and lets the trader shut down through its normal path:
/// the feed thread is stopped and joined, and the journal is flushed. Killing
/// the process outright would leave the position and the journal disagreeing.
void on_interrupt(int) { LiveTrader::stop_flag().store(true); }

int live(const std::string& config_path) {
    std::signal(SIGINT, on_interrupt);
    std::signal(SIGTERM, on_interrupt);

    std::filesystem::create_directories("journal");
    LiveTrader trader(LiveConfig::from_file(config_path));
    trader.run();
    return 0;
}

/// Read-only: proves the credentials work and the account is tradable before
/// anything is placed.
int check() {
    AlpacaVenue venue;
    const auto account = venue.account();
    const auto clock = venue.clock();

    std::cout << "\n  venue     " << venue.name() << '\n'
              << "  equity    " << account.equity << "  cash " << account.cash << '\n'
              << "  market    " << (clock.is_open ? "OPEN" : "closed")
              << (clock.is_open ? ", closes " : ", opens ")
              << to_date(clock.is_open ? clock.next_close : clock.next_open) << '\n';

    const auto positions = venue.positions();
    std::cout << "  positions " << (positions.empty() ? "none" : "") << '\n';
    for (const auto& p : positions) {
        std::cout << "            " << p.symbol << ' ' << p.quantity << " @ " << p.avg_price << '\n';
    }
    std::cout << '\n';
    return 0;
}

/// Places one real paper order. Deliberately its own command rather than a flag
/// on the trading loop: sending an order should never be a side effect of
/// testing something else.
int test_order(const Symbol& symbol, Quantity quantity) {
    AlpacaVenue venue;
    const Order order{symbol, Side::Buy, quantity, "manual", "test order"};

    std::cout << "\n  submitting buy " << quantity << ' ' << symbol << " to " << venue.name()
              << "\n";
    const Fill fill = venue.submit(order);

    if (fill.quantity == 0) {
        std::cout << "  accepted but not filled yet (market closed) · order " << fill.order_id
                  << "\n\n";
        return 0;
    }
    std::cout << "  filled " << fill.quantity << " @ " << fill.price << " · order "
              << fill.order_id << "\n\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();

    register_builtin_strategies();

    std::string mode;
    std::string csv;
    std::string strategy_name = "SmaCrossover";
    std::string journal_path = "journal/trades.jsonl";
    StrategyParams params;

    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--replay" && i + 1 < argc) {
                mode = "replay";
                csv = argv[++i];
            } else if (arg == "--report") {
                mode = "report";
            } else if (arg == "--list") {
                mode = "list";
            } else if (arg == "--check") {
                mode = "check";
            } else if (arg == "--live") {
                mode = "live";
            } else if (arg == "--config" && i + 1 < argc) {
                csv = argv[++i];
            } else if (arg == "--test-order" && i + 2 < argc) {
                mode = "test-order";
                csv = argv[++i];
                params.values["qty"] = std::stod(argv[++i]);
            } else if (arg == "--strategy" && i + 1 < argc) {
                strategy_name = argv[++i];
            } else if (arg == "--journal" && i + 1 < argc) {
                journal_path = argv[++i];
            } else if (arg == "--set" && i + 1 < argc) {
                const auto [key, value] = parse_setting(argv[++i]);
                params.values[key] = value;
            } else {
                return usage();
            }
        }

        if (mode == "list") {
            for (const auto& name : StrategyRegistry::instance().names()) std::cout << name << '\n';
            return 0;
        }
        if (mode == "check") return check();
        if (mode == "live") return live(csv.empty() ? "config.json" : csv);
        if (mode == "test-order") {
            return test_order(csv, static_cast<Quantity>(params.get("qty", 1)));
        }
        if (mode == "report") {
            print_report(std::cout, summarize(journal_path));
            return 0;
        }
        if (mode != "replay") return usage();

        // A replay starts from nothing, otherwise the report would mix these
        // trades in with whatever ran before it.
        std::filesystem::create_directories("journal");
        std::filesystem::remove(journal_path);
        return replay(csv, strategy_name, params, journal_path);

    } catch (const Error& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
