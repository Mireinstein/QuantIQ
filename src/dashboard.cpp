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

std::string pct(double fraction, int places = 1) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(places) << fraction * 100.0 << '%';
    return os.str();
}

std::string ratio(double value) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(2) << value;
    return os.str();
}

std::string polyline(const std::vector<double>& series, double low, double span,
                     const char* css_class) {
    if (series.size() < 2) return "";

    std::ostringstream points;
    for (std::size_t i = 0; i < series.size(); ++i) {
        const double x = static_cast<double>(i) / (series.size() - 1) * kChartWidth;
        const double y = kChartHeight - (series[i] - low) / span * kChartHeight;
        points << (i ? " " : "") << std::fixed << std::setprecision(1) << x << ',' << y;
    }
    return "<polyline class=\"" + std::string(css_class) + "\" points=\"" + points.str() + "\"/>";
}

/// The strategy's equity against buying the instrument on day one and holding
/// it, both rebased to the same starting capital and drawn over the same
/// calendar days.
///
/// Without the benchmark the strategy's own curve is unreadable: a line that
/// climbs looks like success even when doing nothing at all would have climbed
/// faster.
std::string curve(const std::vector<MarkPoint>& marks) {
    if (marks.size() < 2) return "";

    const double start = marks.front().equity;
    const double first_close = marks.front().close;
    if (start <= 0.0 || first_close <= 0.0) return "";

    std::vector<double> strategy;
    std::vector<double> benchmark;
    strategy.reserve(marks.size());
    benchmark.reserve(marks.size());
    for (const auto& m : marks) {
        strategy.push_back(m.equity);
        benchmark.push_back(start * m.close / first_close);
    }

    // Shared scale, or the comparison is a lie told with two axes.
    double low = std::min(*std::min_element(strategy.begin(), strategy.end()),
                          *std::min_element(benchmark.begin(), benchmark.end()));
    double high = std::max(*std::max_element(strategy.begin(), strategy.end()),
                           *std::max_element(benchmark.begin(), benchmark.end()));
    const double pad = (high - low) * 0.05;
    low -= pad;
    high += pad;
    const double span = high - low == 0.0 ? 1.0 : high - low;

    std::ostringstream svg;
    svg << "<svg viewBox=\"0 0 " << kChartWidth << ' ' << kChartHeight
        << "\" preserveAspectRatio=\"none\">"
        << polyline(benchmark, low, span, "bench") << polyline(strategy, low, span, "strat")
        << "</svg>";
    return svg.str();
}

/// How far below its own high-water mark the strategy was, every day. A curve
/// that ends up says nothing about how much pain it took to get there.
std::string underwater(const std::vector<MarkPoint>& marks) {
    if (marks.size() < 2) return "";

    std::vector<double> series;
    double peak = marks.front().equity;
    for (const auto& m : marks) {
        peak = std::max(peak, m.equity);
        series.push_back(peak > 0.0 ? (m.equity / peak - 1.0) * 100.0 : 0.0);
    }

    const double low = std::min(-1.0, *std::min_element(series.begin(), series.end()));
    const double span = -low;

    std::ostringstream svg;
    svg << "<svg class=\"uw\" viewBox=\"0 0 " << kChartWidth << " 60\" preserveAspectRatio=\"none\">";
    std::ostringstream points;
    points << "0,0 ";
    for (std::size_t i = 0; i < series.size(); ++i) {
        const double x = static_cast<double>(i) / (series.size() - 1) * kChartWidth;
        const double y = -series[i] / span * 60.0;
        points << std::fixed << std::setprecision(1) << x << ',' << y << ' ';
    }
    points << kChartWidth << ",0";
    svg << "<polygon points=\"" << points.str() << "\"/></svg>";
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
svg { width:100%; height:120px; display:block; }
svg.uw { height:44px; }
polyline { fill:none; stroke-width:1.5; vector-effect:non-scaling-stroke; }
polyline.strat { stroke:currentColor; }
polyline.bench { stroke:var(--dim); stroke-dasharray:3 3; }
polygon { fill:var(--down); opacity:.22; }
.legend { color:var(--dim); font-size:11px; margin-top:6px; display:flex; gap:14px; }
.legend i { font-style:normal; }
.legend i::before { content:"\\2014 "; }
.legend .b::before { content:"\\2013\\2013 "; }
.stats { display:grid; grid-template-columns:repeat(auto-fit,minmax(88px,1fr)); gap:2px 14px;
         margin-top:10px; font-size:12px; }
.stats div { display:flex; justify-content:space-between; padding:3px 0;
             border-bottom:1px solid var(--line); }
.stats span { color:var(--dim); }
.note { color:var(--dim); font-size:12px; margin:-4px 0 18px; }
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
        << "<th>Strategy</th><th>Trades</th><th>Win %</th><th>Return</th><th>Held</th>"
           "<th>Sharpe</th><th>Max DD</th><th>Net</th>"
        << "</tr></thead><tbody>\n";

    int total_trades = 0;
    int total_wins = 0;
    double total_net = 0.0;
    double worst = 0.0;

    for (const auto& r : results) {
        const auto& m = r.metrics;
        out << "<tr><td>" << escape(r.strategy) << "</td><td>" << r.trades << "</td><td>"
            << std::fixed << std::setprecision(1) << r.win_rate() << "</td><td class=\""
            << (m.total_return >= m.benchmark_return ? "up" : "down") << "\">"
            << (m.valid ? pct(m.total_return) : "-") << "</td><td>"
            << (m.valid ? pct(m.benchmark_return) : "-") << "</td><td>"
            << (m.valid ? ratio(m.sharpe) : "-") << "</td><td class=\"down\">"
            << (m.valid ? pct(m.max_drawdown_pct) : "-") << "</td><td class=\""
            << (r.net.ticks() >= 0 ? "up" : "down") << "\">" << money(r.net.to_double())
            << "</td></tr>\n";

        total_trades += r.trades;
        total_wins += r.wins;
        total_net += r.net.to_double();
        worst = std::min(worst, r.max_drawdown.to_double());
    }

    const double rate = total_trades == 0 ? 0.0 : 100.0 * total_wins / total_trades;
    out << "</tbody><tfoot><tr><td>Total</td><td>" << total_trades << "</td><td>" << std::fixed
        << std::setprecision(1) << rate << "</td><td></td><td></td><td></td><td></td>"
        << "<td class=\"" << (total_net >= 0 ? "up" : "down") << "\">" << money(total_net)
        << "</td></tr></tfoot></table></div>\n";
    (void)worst;

    out << "<h2>Equity against buy and hold</h2>\n"
        << "<p class=\"note\">Solid is the strategy, dashed is buying the instrument on day one "
           "and holding it, both starting from the same capital.</p>\n";

    for (const auto& r : results) {
        const auto& m = r.metrics;
        const bool beat = m.total_return > m.benchmark_return;

        out << "<div class=\"panel " << (r.net.ticks() >= 0 ? "up" : "down") << "\"><header><h3>"
            << escape(r.strategy) << "</h3><span class=\"n\">"
            << (m.valid ? pct(m.total_return) + " vs " + pct(m.benchmark_return) + " held"
                        : std::string("no marks"))
            << "</span></header>" << curve(r.marks)
            << "<div class=\"legend\"><i>" << escape(r.strategy) << "</i><i class=\"b\">buy and hold</i>"
            << "<i style=\"border:0\">" << (beat ? "" : "lost to holding") << "</i></div>";

        if (m.valid) {
            out << "<div class=\"stats\">"
                << "<div><span>CAGR</span>" << pct(m.cagr) << "</div>"
                << "<div><span>Benchmark</span>" << pct(m.benchmark_cagr) << "</div>"
                << "<div><span>Sharpe</span>" << ratio(m.sharpe) << "</div>"
                << "<div><span>Sortino</span>" << ratio(m.sortino) << "</div>"
                << "<div><span>Calmar</span>" << ratio(m.calmar) << "</div>"
                << "<div><span>Max DD</span>" << pct(m.max_drawdown_pct) << "</div>"
                << "<div><span>Exposure</span>" << pct(m.exposure, 0) << "</div>"
                << "<div><span>Profit factor</span>" << ratio(m.profit_factor) << "</div>"
                << "<div><span>Avg win</span>" << money(m.avg_win) << "</div>"
                << "<div><span>Avg loss</span>" << money(m.avg_loss) << "</div>"
                << "<div><span>Expectancy</span>" << money(m.expectancy) << "</div>"
                << "<div><span>Days</span>" << m.days << "</div>"
                << "</div>" << underwater(r.marks)
                << "<div class=\"legend\"><i style=\"border:0\">drawdown from high-water mark</i></div>";
        }
        out << "</div>\n";
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

