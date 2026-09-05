#include "quantiq/dashboard.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "quantiq/errors.hpp"
#include "quantiq/report.hpp"

namespace quantiq {

namespace {

constexpr int kChartWidth = 640;
constexpr int kChartHeight = 180;

/// Journal text is written by this program, but a symbol or a strategy name
/// arrives from a config file, so it is escaped rather than trusted.
std::string escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string money(double value) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(2) << value;
    return os.str();
}

/// A cumulative-profit curve as a polyline. Flat when there is nothing to plot,
/// so a strategy that never traded still renders a panel rather than a gap.
std::string curve(const std::vector<double>& equity) {
    if (equity.size() < 2) return "";

    const double low = std::min(0.0, *std::min_element(equity.begin(), equity.end()));
    const double high = std::max(0.0, *std::max_element(equity.begin(), equity.end()));
    const double span = high - low == 0.0 ? 1.0 : high - low;

    std::ostringstream points;
    for (std::size_t i = 0; i < equity.size(); ++i) {
        const double x = static_cast<double>(i) / (equity.size() - 1) * kChartWidth;
        const double y = kChartHeight - (equity[i] - low) / span * kChartHeight;
        points << (i ? " " : "") << std::fixed << std::setprecision(1) << x << ',' << y;
    }

    // The zero line matters more than the axis: it is the difference between
    // a curve that made money and one that only stopped losing.
    const double zero = kChartHeight - (0.0 - low) / span * kChartHeight;

    std::ostringstream svg;
    svg << "<svg viewBox=\"0 0 " << kChartWidth << ' ' << kChartHeight << "\" preserveAspectRatio=\"none\">"
        << "<line x1=\"0\" y1=\"" << zero << "\" x2=\"" << kChartWidth << "\" y2=\"" << zero
        << "\" class=\"zero\"/>"
        << "<polyline points=\"" << points.str() << "\"/></svg>";
    return svg.str();
}

}  // namespace

void write_dashboard(const std::string& journal_path, const std::string& out_path) {
    const auto results = summarize(journal_path);
    const auto trades = trades_of(journal_path);

    const auto parent = std::filesystem::path(out_path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);

    std::ofstream out(out_path);
    if (!out) throw DataError("cannot write dashboard: " + out_path);

    out << R"(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>QuantIQ</title><style>
:root { --bg:#fbfbfa; --fg:#1a1c1f; --dim:#6b7280; --line:#e3e5e8; --up:#1f7a4d; --down:#b04a38; }
@media (prefers-color-scheme: dark) {
  :root { --bg:#16181c; --fg:#e8eaed; --dim:#9099a6; --line:#2b2f36; --up:#4ec98a; --down:#e0765f; }
}
body { margin:0; padding:32px 24px; background:var(--bg); color:var(--fg);
       font:14px/1.5 -apple-system,"Helvetica Neue",sans-serif; }
main { max-width:900px; margin:0 auto; }
h1 { font-size:19px; margin:0 0 2px; letter-spacing:-.3px; }
p.sub { color:var(--dim); margin:0 0 28px; }
h2 { font-size:14px; margin:32px 0 10px; font-weight:600; }
table { border-collapse:collapse; width:100%; font-size:13px; }
th,td { padding:7px 10px; text-align:right; border-bottom:1px solid var(--line); }
th:first-child,td:first-child { text-align:left; }
th { color:var(--dim); font-weight:500; }
tfoot td { font-weight:600; border-bottom:none; border-top:2px solid var(--line); }
.up { color:var(--up); } .down { color:var(--down); }
.panel { border:1px solid var(--line); border-radius:8px; padding:14px 16px; margin-bottom:12px; }
.panel header { display:flex; justify-content:space-between; align-items:baseline; margin-bottom:8px; }
.panel h3 { font-size:13px; margin:0; font-weight:600; }
.panel .n { color:var(--dim); font-size:12px; }
svg { width:100%; height:110px; display:block; }
polyline { fill:none; stroke:currentColor; stroke-width:1.5; vector-effect:non-scaling-stroke; }
.zero { stroke:var(--line); stroke-width:1; vector-effect:non-scaling-stroke; }
.empty { color:var(--dim); padding:24px 0; text-align:center; }
.wrap { overflow-x:auto; }
</style></head><body><main>
<h1>QuantIQ</h1>
)";

    out << "<p class=\"sub\">" << trades.size() << " closed trades across " << results.size()
        << " strategies &middot; from " << escape(journal_path) << "</p>\n";

    if (results.empty()) {
        out << "<p class=\"empty\">Nothing closed yet. Run a replay or a live session.</p>\n"
            << "</main></body></html>\n";
        return;
    }

    out << "<h2>Strategies</h2><div class=\"wrap\"><table><thead><tr>"
        << "<th>Strategy</th><th>Trades</th><th>Win %</th><th>Net</th><th>Max drawdown</th>"
        << "</tr></thead><tbody>\n";

    int total_trades = 0;
    int total_wins = 0;
    double total_net = 0.0;
    double worst = 0.0;

    for (const auto& r : results) {
        out << "<tr><td>" << escape(r.strategy) << "</td><td>" << r.trades << "</td><td>"
            << std::fixed << std::setprecision(1) << r.win_rate() << "</td><td class=\""
            << (r.net.ticks() >= 0 ? "up" : "down") << "\">" << money(r.net.to_double())
            << "</td><td class=\"down\">" << money(r.max_drawdown.to_double()) << "</td></tr>\n";

        total_trades += r.trades;
        total_wins += r.wins;
        total_net += r.net.to_double();
        worst = std::min(worst, r.max_drawdown.to_double());
    }

    const double rate = total_trades == 0 ? 0.0 : 100.0 * total_wins / total_trades;
    out << "</tbody><tfoot><tr><td>Total</td><td>" << total_trades << "</td><td>" << std::fixed
        << std::setprecision(1) << rate << "</td><td class=\"" << (total_net >= 0 ? "up" : "down")
        << "\">" << money(total_net) << "</td><td class=\"down\">" << money(worst)
        << "</td></tr></tfoot></table></div>\n";

    out << "<h2>Cumulative profit</h2>\n";
    for (const auto& r : results) {
        out << "<div class=\"panel " << (r.net.ticks() >= 0 ? "up" : "down") << "\"><header><h3>"
            << escape(r.strategy) << "</h3><span class=\"n\">" << money(r.net.to_double())
            << " over " << r.trades << " trades</span></header>" << curve(r.equity) << "</div>\n";
    }

    out << "<h2>Trades</h2><div class=\"wrap\"><table><thead><tr>"
        << "<th>Closed</th><th>Strategy</th><th>Symbol</th><th>Qty</th><th>Entry</th>"
        << "<th>Exit</th><th>Profit</th></tr></thead><tbody>\n";

    // Newest first, because the reason you open this page is usually to see
    // what just happened.
    for (auto it = trades.rbegin(); it != trades.rend(); ++it) {
        out << "<tr><td>" << to_date(it->closed) << "</td><td>" << escape(it->strategy)
            << "</td><td>" << escape(it->symbol) << "</td><td>" << it->quantity << "</td><td>"
            << it->entry << "</td><td>" << it->exit << "</td><td class=\""
            << (it->profit.ticks() >= 0 ? "up" : "down") << "\">" << money(it->profit.to_double())
            << "</td></tr>\n";
    }

    out << "</tbody></table></div>\n</main></body></html>\n";
}

}  // namespace quantiq
