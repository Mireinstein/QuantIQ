#include "quantiq/report.hpp"

#include <cmath>
#include <deque>
#include <numeric>
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
        if (j.is_discarded()) continue;

        const auto event = j.value("event", "");
        if (event == "mark") {
            auto& result = results[j.value("strategy", "unknown")];
            result.strategy = j.value("strategy", "unknown");
            Timestamp when{};
            try {
                when = parse_date(j.value("ts", ""));
            } catch (const DataError&) {
            }
            result.marks.push_back(MarkPoint{when, j.value("equity", 0.0), j.value("close", 0.0),
                                             j.value("quantity", Quantity{0}) != 0});
            continue;
        }
        if (event != "fill") continue;

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

Metrics compute_metrics(const std::vector<MarkPoint>& marks, const std::vector<Trade>& trades) {
    Metrics m;
    if (marks.size() < 2) return m;

    const double start = marks.front().equity;
    const double end = marks.back().equity;
    const double first_close = marks.front().close;
    const double last_close = marks.back().close;
    if (start <= 0.0 || first_close <= 0.0) return m;

    m.valid = true;
    m.days = static_cast<int>(marks.size());
    m.total_return = end / start - 1.0;
    m.benchmark_return = last_close / first_close - 1.0;

    // Trading days rather than calendar days, since that is what the series is
    // indexed by; 252 is the convention for a US equity year.
    const double years = static_cast<double>(marks.size()) / 252.0;
    if (years > 0.0) {
        m.cagr = std::pow(1.0 + m.total_return, 1.0 / years) - 1.0;
        m.benchmark_cagr = std::pow(1.0 + m.benchmark_return, 1.0 / years) - 1.0;
    }

    std::vector<double> returns;
    returns.reserve(marks.size());
    double peak = start;
    int invested_days = 0;

    for (std::size_t i = 1; i < marks.size(); ++i) {
        const double previous = marks[i - 1].equity;
        if (previous > 0.0) returns.push_back(marks[i].equity / previous - 1.0);

        peak = std::max(peak, marks[i].equity);
        if (peak > 0.0) {
            m.max_drawdown_pct = std::min(m.max_drawdown_pct, marks[i].equity / peak - 1.0);
        }
        if (marks[i].invested) ++invested_days;
    }
    m.exposure = static_cast<double>(invested_days) / static_cast<double>(marks.size());

    if (!returns.empty()) {
        const double mean =
            std::accumulate(returns.begin(), returns.end(), 0.0) / static_cast<double>(returns.size());

        double variance = 0.0;
        double downside = 0.0;
        int losses = 0;
        for (double r : returns) {
            variance += (r - mean) * (r - mean);
            if (r < 0.0) {
                downside += r * r;
                ++losses;
            }
        }
        variance /= static_cast<double>(returns.size());

        // Annualised by sqrt(252): daily volatility scales with the square root
        // of time, so a yearly figure is the daily one times sqrt(trading days).
        const double sd = std::sqrt(variance);
        if (sd > 0.0) m.sharpe = mean / sd * std::sqrt(252.0);

        // Sortino ignores upside volatility, because being surprised by a
        // profit is not the risk anyone is trying to measure.
        if (losses > 0) {
            const double downside_sd = std::sqrt(downside / static_cast<double>(losses));
            if (downside_sd > 0.0) m.sortino = mean / downside_sd * std::sqrt(252.0);
        }
    }

    if (m.max_drawdown_pct < 0.0) m.calmar = m.cagr / -m.max_drawdown_pct;

    double gross_win = 0.0;
    double gross_loss = 0.0;
    int wins = 0;
    int losses = 0;
    for (const auto& t : trades) {
        const double p = t.profit.to_double();
        if (p >= 0.0) {
            gross_win += p;
            ++wins;
        } else {
            gross_loss += -p;
            ++losses;
        }
    }
    if (gross_loss > 0.0) m.profit_factor = gross_win / gross_loss;
    if (wins > 0) m.avg_win = gross_win / wins;
    if (losses > 0) m.avg_loss = gross_loss / losses;
    if (!trades.empty()) {
        m.expectancy = (gross_win - gross_loss) / static_cast<double>(trades.size());
    }

    return m;
}

std::vector<StrategyResult> summarize(const std::string& journal_path) {
    std::map<std::string, StrategyResult> results;
    std::vector<Trade> trades;
    walk(journal_path, &trades, &results);

    std::vector<StrategyResult> out;
    for (auto& [name, r] : results) {
        std::vector<Trade> mine;
        for (const auto& t : trades) {
            if (t.strategy == name) mine.push_back(t);
        }
        r.metrics = compute_metrics(r.marks, mine);
        out.push_back(std::move(r));
    }
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
