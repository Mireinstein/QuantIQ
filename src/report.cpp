#include "quantiq/report.hpp"

#include <deque>
#include <fstream>
#include <iomanip>
#include <map>

#include <nlohmann/json.hpp>

#include "quantiq/errors.hpp"

namespace quantiq {

namespace {

struct Lot {
    Quantity quantity;
    double price;
    Timestamp opened;
};

/// Per strategy and symbol, buys queue up as open lots and each sell consumes
/// them oldest first. A round trip is only counted once a sell closes a lot,
/// which is why a position still open contributes nothing to the totals.
struct Book {
    std::map<std::pair<std::string, std::string>, std::deque<Lot>> lots;
};

}  // namespace

namespace {

/// One pass over the journal, producing both the per-trade detail and the
/// per-strategy totals. Written once because the two used to drift apart when
/// the matching rule was changed in only one of them.
void walk(const std::string& journal_path, std::vector<Trade>* out_trades,
          std::map<std::string, StrategyResult>* out_results) {
    std::ifstream in(journal_path);
    if (!in) throw DataError("cannot read journal: " + journal_path);

    Book book;
    std::map<std::string, StrategyResult> results;
    std::map<std::string, double> running;
    std::map<std::string, double> peak;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;

        nlohmann::json j = nlohmann::json::parse(line, nullptr, false);
        if (j.is_discarded() || j.value("event", "") != "fill") continue;

        const auto strategy = j.value("strategy", "unknown");
        const auto symbol = j.value("symbol", "");
        const auto side = j.value("side", "");
        const auto quantity = j.value("quantity", Quantity{0});
        const auto price = j.value("price", 0.0);
        const auto reason = j.value("reason", "");
        Timestamp when{};
        try {
            when = parse_date(j.value("ts", ""));
        } catch (const DataError&) {
            // A journal line without a usable date still carries its money.
        }

        auto& result = results[strategy];
        result.strategy = strategy;

        auto& queue = book.lots[{strategy, symbol}];

        if (side == "buy") {
            queue.push_back(Lot{quantity, price, when});
            continue;
        }

        Quantity remaining = quantity;
        double profit = 0.0;
        double entry_price = 0.0;
        Timestamp opened{};
        while (remaining > 0 && !queue.empty()) {
            Lot& lot = queue.front();
            const Quantity matched = std::min(remaining, lot.quantity);
            profit += (price - lot.price) * static_cast<double>(matched);
            if (entry_price == 0.0) {
                entry_price = lot.price;
                opened = lot.opened;
            }
            lot.quantity -= matched;
            remaining -= matched;
            if (lot.quantity == 0) queue.pop_front();
        }

        if (remaining == quantity) continue;  // nothing was actually closed

        if (out_trades != nullptr) {
            out_trades->push_back(Trade{strategy, symbol, opened, when, quantity - remaining,
                                        Price::from_double(entry_price),
                                        Price::from_double(price), Money::from_double(profit),
                                        reason});
        }

        ++result.trades;
        if (profit > 0) ++result.wins;
        result.net += Money::from_double(profit);

        double& equity = running[strategy];
        equity += profit;
        result.equity.push_back(equity);

        double& high = peak[strategy];
        high = std::max(high, equity);
        const double drawdown = equity - high;
        if (Money::from_double(drawdown) < result.max_drawdown) {
            result.max_drawdown = Money::from_double(drawdown);
        }
    }

    if (out_results != nullptr) *out_results = std::move(results);
}

}  // namespace

std::vector<StrategyResult> summarize(const std::string& journal_path) {
    std::map<std::string, StrategyResult> results;
    walk(journal_path, nullptr, &results);

    std::vector<StrategyResult> out;
    for (auto& [_, r] : results) out.push_back(std::move(r));
    return out;
}

std::vector<Trade> trades_of(const std::string& journal_path) {
    std::vector<Trade> trades;
    walk(journal_path, &trades, nullptr);
    return trades;
}

void print_report(std::ostream& os, const std::vector<StrategyResult>& results) {
    if (results.empty()) {
        os << "\n  no completed trades in the journal yet\n\n";
        return;
    }

    os << '\n'
       << "  " << std::left << std::setw(16) << "strategy" << std::right << std::setw(8) << "trades"
       << std::setw(8) << "win%" << std::setw(12) << "net" << std::setw(12) << "max dd" << '\n'
       << "  " << std::string(56, '-') << '\n';

    int trades = 0;
    int wins = 0;
    Money net;
    Money worst;

    for (const auto& r : results) {
        os << "  " << std::left << std::setw(16) << r.strategy << std::right << std::setw(8)
           << r.trades << std::setw(8) << std::fixed << std::setprecision(1) << r.win_rate()
           << std::setw(12) << r.net.str() << std::setw(12) << r.max_drawdown.str() << '\n';
        trades += r.trades;
        wins += r.wins;
        net += r.net;
        if (r.max_drawdown < worst) worst = r.max_drawdown;
    }

    const double rate = trades == 0 ? 0.0 : 100.0 * static_cast<double>(wins) / trades;
    os << "  " << std::string(56, '-') << '\n'
       << "  " << std::left << std::setw(16) << "total" << std::right << std::setw(8) << trades
       << std::setw(8) << std::fixed << std::setprecision(1) << rate << std::setw(12) << net.str()
       << std::setw(12) << worst.str() << "\n\n";
}

}  // namespace quantiq
