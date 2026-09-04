#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "quantiq/engine.hpp"
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

        std::cout << "\n  " << strategy->name() << '\n';
        journal.session("replay_start", csv + " " + strategy->name());

        Engine engine(venue, *strategy, journal, risk);
        const auto stats = engine.run(verbose);

        std::cout << "    " << stats.signals << " signals · " << stats.fills << " fills · "
                  << stats.rejected << " rejected";
        if (risk.halted()) std::cout << " · HALTED: " << risk.halt_reason();
        std::cout << '\n';
    }

    print_report(std::cout, summarize(journal_path));
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
