#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "quantiq/alpaca.hpp"
#include "quantiq/dashboard.hpp"
#include "quantiq/dry_run.hpp"
#include "quantiq/engine.hpp"
#include "quantiq/errors.hpp"
#include "quantiq/live.hpp"
#include "quantiq/mock_venue.hpp"
#include "quantiq/report.hpp"
#include "quantiq/strategies.hpp"

using namespace quantiq;

namespace {

std::vector<Bar> bars_from(const std::vector<double>& closes) {
    std::vector<Bar> bars;
    for (std::size_t i = 0; i < closes.size(); ++i) {
        const auto p = Price::from_double(closes[i]);
        bars.push_back(Bar{"AAPL", Timestamp{} + std::chrono::hours(24 * i), p, p, p, p, 1000});
    }
    return bars;
}

/// Holds a fixed target from a chosen bar onward, so an engine test is about
/// the engine rather than about whether an indicator crossed.
class ScriptedStrategy : public Strategy {
public:
    ScriptedStrategy(int enter_on, int exit_on) : enter_(enter_on), exit_(exit_on) {}

    Target on_bar(const Bar&) override {
        ++seen_;
        if (exit_ > 0 && seen_ >= exit_) return Target{0.0, "scripted exit"};
        if (seen_ >= enter_) return Target{1.0, "scripted entry"};
        return Target{0.0, "waiting"};
    }
    std::string name() const override { return "Scripted"; }

private:
    int enter_;
    int exit_;
    int seen_ = 0;
};

struct TempJournal {
    std::string path = "test-journal.jsonl";
    TempJournal() { std::filesystem::remove(path); }
    ~TempJournal() { std::filesystem::remove(path); }
};

Account account_with(double cash) {
    return Account{Money::from_double(cash), Money::from_double(cash)};
}

}  // namespace

TEST_CASE("the engine trades the gap between the target and what is held") {
    TempJournal tmp;
    MockVenue venue(bars_from({100, 100, 110, 120, 130}));
    ScriptedStrategy strategy(2, 4);
    Journal journal(tmp.path);
    Risk risk;

    Engine engine(venue, strategy, journal, risk, Sizer{0.10});
    const auto stats = engine.run();

    REQUIRE(stats.bars == 5);
    REQUIRE(stats.fills == 2);   // one entry, one exit, and no drift trades between
    REQUIRE(stats.rejected == 0);
    REQUIRE(engine.portfolio().realized().to_double() > 0.0);
}

TEST_CASE("a rising price does not churn the position to chase its weight") {
    TempJournal tmp;
    // Price climbs 20%, so a fixed weight implies steadily fewer shares. Inside
    // the band that is drift, not a decision, and must not trade.
    MockVenue venue(bars_from({100, 100, 104, 108, 112, 116, 120}));
    ScriptedStrategy strategy(2, 0);
    Journal journal(tmp.path);
    Risk risk;

    Engine engine(venue, strategy, journal, risk, Sizer{0.10});
    REQUIRE(engine.run().fills == 1);
}

TEST_CASE("a target that does not change places no further orders") {
    TempJournal tmp;
    // Constant price, so the share count the target implies never moves either.
    MockVenue venue(bars_from({100, 100, 100, 100, 100, 100, 100, 100}));
    ScriptedStrategy strategy(2, 0);   // enters and never exits
    Journal journal(tmp.path);
    Risk risk;

    Engine engine(venue, strategy, journal, risk, Sizer{0.10});
    const auto stats = engine.run();

    REQUIRE(stats.bars == 8);
    REQUIRE(stats.fills == 1);   // not one per bar
}

TEST_CASE("adopting broker positions stops a restart from buying twice") {
    TempJournal tmp;
    MockVenue venue(bars_from({100, 100, 100, 100}));
    ScriptedStrategy strategy(1, 0);
    Journal journal(tmp.path);
    Risk risk;

    Engine engine(venue, strategy, journal, risk, Sizer{0.10});
    // 10% of the mock's $100k at $100 is exactly what the strategy would buy.
    engine.adopt({Position{"AAPL", 100, Price::from_double(100.0)}});
    const auto stats = engine.run();

    REQUIRE(stats.fills == 0);
    REQUIRE(engine.portfolio().find("AAPL")->quantity == 100);
}

TEST_CASE("the report reconstructs the round trip from the journal alone") {
    TempJournal tmp;
    MockVenue venue(bars_from({100, 100, 110, 120, 130}));
    ScriptedStrategy strategy(2, 4);
    Journal journal(tmp.path);
    Risk risk;

    Engine engine(venue, strategy, journal, risk, Sizer{0.10});
    engine.run();

    journal.flush();
    const auto results = summarize(tmp.path);
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].strategy == "Scripted");
    REQUIRE(results[0].trades == 1);
    REQUIRE(results[0].wins == 1);
}

TEST_CASE("risk refuses to sell a position that is not held") {
    Portfolio empty;
    Risk risk;
    std::string why;

    const Order sell{"AAPL", Side::Sell, 10, "S", ""};
    REQUIRE_FALSE(risk.allow(sell, empty, Price::from_double(10.0), account_with(1000), why));
    REQUIRE(why == "no position to sell");
}

TEST_CASE("risk refuses an order the account cannot pay for") {
    Portfolio empty;
    Risk risk;
    std::string why;

    // 100 shares at $50 is $5,000 against $1,000 of cash.
    const Order buy{"AAPL", Side::Buy, 100, "S", ""};
    REQUIRE_FALSE(risk.allow(buy, empty, Price::from_double(50.0), account_with(1000), why));
    REQUIRE(why.find("cash is") != std::string::npos);

    REQUIRE(risk.allow(buy, empty, Price::from_double(50.0), account_with(6000), why));
}

TEST_CASE("the mock venue will not spend money the account does not have") {
    // Without this the replay equity curve describes an account nobody could
    // have held.
    MockVenue venue(bars_from({100}), Money::from_double(500.0));
    venue.next_bar();

    REQUIRE_THROWS_AS(venue.submit(Order{"AAPL", Side::Buy, 100, "S", ""}), InsufficientFunds);
}

TEST_CASE("risk caps how many names can be open at once") {
    Portfolio p;
    p.apply(Fill{"AAPL", Side::Buy, 1, Price::from_double(1.0), Timestamp{}, "1"});
    p.apply(Fill{"MSFT", Side::Buy, 1, Price::from_double(1.0), Timestamp{}, "2"});

    Risk risk(RiskLimits{.max_positions = 2});
    std::string why;
    const auto price = Price::from_double(1.0);
    const auto account = account_with(100000);

    // Adding to a name already held is fine; a third name is not.
    REQUIRE(risk.allow(Order{"AAPL", Side::Buy, 1, "S", ""}, p, price, account, why));
    REQUIRE_FALSE(risk.allow(Order{"NVDA", Side::Buy, 1, "S", ""}, p, price, account, why));
    REQUIRE(why == "at max positions");
}

TEST_CASE("a halt stops everything, including orders that were otherwise fine") {
    Portfolio p;
    Risk risk;
    std::string why;
    const auto price = Price::from_double(1.0);
    const auto account = account_with(100000);

    REQUIRE(risk.allow(Order{"AAPL", Side::Buy, 1, "S", ""}, p, price, account, why));
    risk.halt("manual");
    REQUIRE(risk.halted());
    REQUIRE_FALSE(risk.allow(Order{"AAPL", Side::Buy, 1, "S", ""}, p, price, account, why));
}

TEST_CASE("a drawdown past the limit trips the halt on its own") {
    Risk risk(RiskLimits{.max_drawdown = 0.10});

    risk.observe_equity(Money::from_double(1000.0));
    REQUIRE_FALSE(risk.halted());

    risk.observe_equity(Money::from_double(950.0));
    REQUIRE_FALSE(risk.halted());

    risk.observe_equity(Money::from_double(800.0));
    REQUIRE(risk.halted());
}

TEST_CASE("a CSV round-trips into bars") {
    const std::string path = "test-bars.csv";
    {
        std::ofstream out(path);
        out << "symbol,date,open,high,low,close,volume\n"
            << "AAPL,2025-01-02,100.0,101.5,99.5,101.0,1000000\n"
            << "AAPL,2025-01-03,101.0,103.0,100.5,102.5,1200000\n";
    }

    const auto bars = MockVenue::bars_from_csv(path);
    std::filesystem::remove(path);

    REQUIRE(bars.size() == 2);
    REQUIRE(bars[0].symbol == "AAPL");
    REQUIRE(bars[0].close == Price::from_double(101.0));
    REQUIRE(to_date(bars[1].ts) == "2025-01-03");
    REQUIRE(bars[1].volume == 1200000);
}

TEST_CASE("a missing bar file is a data error with the path in it") {
    REQUIRE_THROWS_AS(MockVenue::bars_from_csv("does-not-exist.csv"), DataError);
}

TEST_CASE("the venue refuses a base URL that is not the paper endpoint") {
    // A typo here would place real orders with real money, so it is checked at
    // construction rather than left to be noticed on the first fill.
    setenv("ALPACA_BASE_URL", "https://api.alpaca.markets", 1);
    setenv("ALPACA_DATA_URL", "https://data.alpaca.markets", 1);
    setenv("ALPACA_API_KEY_ID", "x", 1);
    setenv("ALPACA_API_SECRET_KEY", "y", 1);

    REQUIRE_THROWS_AS(AlpacaVenue(), ConfigError);

    unsetenv("ALPACA_BASE_URL");
    unsetenv("ALPACA_DATA_URL");
    unsetenv("ALPACA_API_KEY_ID");
    unsetenv("ALPACA_API_SECRET_KEY");
}

TEST_CASE("a missing credential names the variable rather than failing as a 401") {
    unsetenv("QUANTIQ_NOT_SET");
    REQUIRE_THROWS_AS(require_env("QUANTIQ_NOT_SET"), ConfigError);
}

TEST_CASE("the queue hands items across threads in order") {
    BoundedQueue<int> queue(4);
    std::thread producer([&] {
        for (int i = 0; i < 20; ++i) queue.push(i);
        queue.close();
    });

    std::vector<int> received;
    while (auto value = queue.pop()) received.push_back(*value);
    producer.join();

    REQUIRE(received.size() == 20);
    REQUIRE(received.front() == 0);
    REQUIRE(received.back() == 19);
}

TEST_CASE("closing the queue releases a consumer that is blocked on it") {
    // Without this a shutdown deadlocks: the consumer waits for data that will
    // never arrive and the thread cannot be joined.
    BoundedQueue<int> queue(4);
    std::thread consumer([&] { REQUIRE_FALSE(queue.pop().has_value()); });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    queue.close();
    consumer.join();
}

TEST_CASE("a bounded queue makes the producer wait rather than growing forever") {
    BoundedQueue<int> queue(2);
    queue.push(1);
    queue.push(2);

    std::atomic<bool> third_landed{false};
    std::thread producer([&] {
        queue.push(3);
        third_landed.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE_FALSE(third_landed.load());   // blocked, as it should be

    REQUIRE(queue.pop().value() == 1);
    producer.join();
    REQUIRE(third_landed.load());
}

TEST_CASE("a feed message is handled by type rather than by nullable fields") {
    std::vector<FeedMessage> messages{Bar{"AAPL", Timestamp{}, {}, {}, {}, Price::from_double(1.0), 0},
                                      FeedError{"timeout"}, SessionClose{Timestamp{}}};

    int bars = 0, errors = 0, closes = 0;
    for (const auto& message : messages) {
        std::visit(
            [&](auto&& m) {
                using T = std::decay_t<decltype(m)>;
                if constexpr (std::is_same_v<T, Bar>) ++bars;
                else if constexpr (std::is_same_v<T, FeedError>) ++errors;
                else if constexpr (std::is_same_v<T, SessionClose>) ++closes;
            },
            message);
    }

    REQUIRE(bars == 1);
    REQUIRE(errors == 1);
    REQUIRE(closes == 1);
}

TEST_CASE("two strategies cannot both claim the same symbol") {
    const std::string path = "test-config.json";
    {
        std::ofstream out(path);
        out << R"({"strategies":[
              {"name":"SmaCrossover","symbols":["AAPL","MSFT"]},
              {"name":"Momentum","symbols":["MSFT"]}]})";
    }

    REQUIRE_THROWS_AS(LiveConfig::from_file(path), ConfigError);
    std::filesystem::remove(path);
}

TEST_CASE("a valid config builds one entry per symbol") {
    const std::string path = "test-config.json";
    {
        std::ofstream out(path);
        out << R"({"position_fraction":0.05,"strategies":[
              {"name":"SmaCrossover","symbols":["AAPL","MSFT"],"params":{"fast":5,"slow":20}},
              {"name":"Momentum","symbols":["NVDA"]}]})";
    }

    const auto config = LiveConfig::from_file(path);
    std::filesystem::remove(path);

    REQUIRE(config.position_fraction == 0.05);
    REQUIRE(config.strategies.size() == 2);
    REQUIRE(config.strategies[0].symbols.size() == 2);
    REQUIRE(config.strategies[0].params.get("fast", 0) == 5);
}

TEST_CASE("a config listing no strategies is rejected rather than run empty") {
    const std::string path = "test-config.json";
    {
        std::ofstream out(path);
        out << R"({"strategies":[]})";
    }

    REQUIRE_THROWS_AS(LiveConfig::from_file(path), ConfigError);
    std::filesystem::remove(path);
}

TEST_CASE("the dashboard renders the journal into one self-contained file") {
    TempJournal tmp;
    MockVenue venue(bars_from({100, 100, 110, 120, 130}));
    ScriptedStrategy strategy(2, 4);
    Journal journal(tmp.path);
    Risk risk;

    Engine engine(venue, strategy, journal, risk, Sizer{0.10});
    engine.run();

    journal.flush();
    const std::string html_path = "test-dashboard.html";
    write_dashboard(tmp.path, html_path);

    std::ifstream in(html_path);
    const std::string html((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    std::filesystem::remove(html_path);

    REQUIRE(html.find("<!doctype html>") == 0);
    REQUIRE(html.find("Scripted") != std::string::npos);
    // Nothing fetched at runtime: the page has to be readable offline.
    REQUIRE(html.find("<script") == std::string::npos);
    REQUIRE(html.find("http://") == std::string::npos);
}

TEST_CASE("a journal with no closed trades still produces a page") {
    TempJournal tmp;
    Journal journal(tmp.path);
    journal.session("replay_start", "nothing happened");

    journal.flush();
    const std::string html_path = "test-dashboard.html";
    write_dashboard(tmp.path, html_path);

    std::ifstream in(html_path);
    const std::string html((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    std::filesystem::remove(html_path);

    REQUIRE(html.find("Nothing closed yet") != std::string::npos);
}

TEST_CASE("a strategy name from config is escaped rather than trusted into the page") {
    TempJournal tmp;
    {
        std::ofstream out(tmp.path);
        out << R"({"event":"fill","ts":"2025-01-02","symbol":"<img>","strategy":"a&b",)"
            << R"("side":"buy","quantity":1,"price":10.0,"reason":""})" << '\n'
            << R"({"event":"fill","ts":"2025-01-03","symbol":"<img>","strategy":"a&b",)"
            << R"("side":"sell","quantity":1,"price":11.0,"reason":""})" << '\n';
    }

    const std::string html_path = "test-dashboard.html";
    write_dashboard(tmp.path, html_path);

    std::ifstream in(html_path);
    const std::string html((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    std::filesystem::remove(html_path);

    REQUIRE(html.find("&lt;img&gt;") != std::string::npos);
    REQUIRE(html.find("a&amp;b") != std::string::npos);
}

TEST_CASE("trades carry the detail the totals throw away") {
    TempJournal tmp;
    MockVenue venue(bars_from({100, 100, 110, 120, 130}));
    ScriptedStrategy strategy(2, 4);
    Journal journal(tmp.path);
    Risk risk;

    Engine engine(venue, strategy, journal, risk, Sizer{0.10});
    engine.run();

    journal.flush();
    const auto trades = trades_of(tmp.path);
    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].strategy == "Scripted");
    REQUIRE(trades[0].symbol == "AAPL");
    REQUIRE(trades[0].exit > trades[0].entry);
    REQUIRE(trades[0].profit.ticks() > 0);
}

namespace {

/// A synthetic equity series, so metric arithmetic can be checked against
/// numbers worked out by hand rather than against whatever a replay produced.
std::vector<MarkPoint> marks_from(const std::vector<double>& equity,
                                  const std::vector<double>& close, bool invested = true) {
    std::vector<MarkPoint> marks;
    for (std::size_t i = 0; i < equity.size(); ++i) {
        marks.push_back(MarkPoint{Timestamp{} + std::chrono::hours(24 * i), equity[i],
                                  i < close.size() ? close[i] : 0.0, invested});
    }
    return marks;
}

}  // namespace

TEST_CASE("total return and benchmark are measured over the same window") {
    // Equity up 10%, the instrument up 50%: the strategy lost badly to holding.
    const auto marks = marks_from({100.0, 105.0, 110.0}, {10.0, 12.0, 15.0});
    const auto m = compute_metrics(marks, {});

    REQUIRE(m.valid);
    REQUIRE(m.total_return == Catch::Approx(0.10));
    REQUIRE(m.benchmark_return == Catch::Approx(0.50));
    REQUIRE(m.total_return < m.benchmark_return);
}

TEST_CASE("drawdown is measured from the high-water mark, not from the start") {
    // Rises to 120, falls to 90: the drawdown is 25% off the peak, not 10% off
    // the opening balance.
    const auto marks = marks_from({100.0, 120.0, 90.0, 100.0}, {1.0, 1.0, 1.0, 1.0});
    const auto m = compute_metrics(marks, {});

    REQUIRE(m.max_drawdown_pct == Catch::Approx(-0.25));
}

TEST_CASE("exposure counts the days capital was actually deployed") {
    std::vector<MarkPoint> marks = marks_from({100.0, 100.0, 100.0, 100.0}, {1, 1, 1, 1});
    marks[0].invested = false;
    marks[1].invested = false;

    REQUIRE(compute_metrics(marks, {}).exposure == Catch::Approx(0.5));
}

TEST_CASE("a flat equity curve has no volatility and so no Sharpe") {
    const auto m = compute_metrics(marks_from({100.0, 100.0, 100.0}, {1, 1, 1}), {});
    REQUIRE(m.sharpe == 0.0);
    REQUIRE(m.max_drawdown_pct == 0.0);
}

TEST_CASE("profit factor and expectancy come from the trades, not the curve") {
    std::vector<Trade> trades;
    const auto trade = [](double profit) {
        Trade t;
        t.profit = Money::from_double(profit);
        return t;
    };
    trades.push_back(trade(300.0));
    trades.push_back(trade(100.0));
    trades.push_back(trade(-200.0));

    const auto m = compute_metrics(marks_from({100.0, 110.0}, {1.0, 1.0}), trades);

    REQUIRE(m.profit_factor == Catch::Approx(2.0));      // 400 won, 200 lost
    REQUIRE(m.avg_win == Catch::Approx(200.0));
    REQUIRE(m.avg_loss == Catch::Approx(200.0));
    REQUIRE(m.expectancy == Catch::Approx(200.0 / 3.0));
}

TEST_CASE("a series too short to have a return yields no metrics rather than nonsense") {
    REQUIRE_FALSE(compute_metrics({}, {}).valid);
    REQUIRE_FALSE(compute_metrics(marks_from({100.0}, {1.0}), {}).valid);
}

TEST_CASE("the dashboard names the benchmark, not just the strategy") {
    TempJournal tmp;
    MockVenue venue(bars_from({100, 100, 110, 120, 130}));
    ScriptedStrategy strategy(2, 4);
    Journal journal(tmp.path);
    Risk risk;

    Engine engine(venue, strategy, journal, risk, Sizer{0.10});
    engine.run();

    journal.flush();
    const std::string html_path = "test-dashboard.html";
    write_dashboard(tmp.path, html_path);

    std::ifstream in(html_path);
    const std::string html((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    std::filesystem::remove(html_path);

    REQUIRE(html.find("buy and hold") != std::string::npos);
    REQUIRE(html.find("Sharpe") != std::string::npos);
    REQUIRE(html.find("Exposure") != std::string::npos);
}

TEST_CASE("a strategy warmed on history reaches the same state as one fed live") {
    // A scheduled job starts a fresh process each run, so a strategy that is
    // not replayed over past bars would restart from nothing every morning and
    // never accumulate the history its indicators need.
    const std::vector<double> closes{20, 19, 18, 17, 16, 15, 25, 30, 35, 40};

    SmaCrossover fed_live(StrategyParams{{{"fast", 2}, {"slow", 4}}});
    double last_live = 0.0;
    for (double c : closes) {
        const auto p = Price::from_double(c);
        last_live = fed_live.on_bar(Bar{"AAPL", Timestamp{}, p, p, p, p, 0}).weight;
    }

    SmaCrossover warmed(StrategyParams{{{"fast", 2}, {"slow", 4}}});
    double last_warm = 0.0;
    for (std::size_t i = 0; i < closes.size(); ++i) {
        const auto p = Price::from_double(closes[i]);
        last_warm = warmed.on_bar(Bar{"AAPL", Timestamp{}, p, p, p, p, 0}).weight;
    }

    REQUIRE(last_warm == last_live);
    REQUIRE(last_warm == 1.0);
}

TEST_CASE("a dry run makes the same decisions but sends nothing") {
    // Without this the only way to try the bot out is to let it trade, which
    // means a new user's first run puts orders on their account.
    TempJournal tmp;
    MockVenue real(bars_from({100, 100, 110, 120, 130}));
    DryRunVenue dry(real);
    ScriptedStrategy strategy(2, 4);
    Journal journal(tmp.path);
    Risk risk;

    Engine engine(dry, strategy, journal, risk, Sizer{0.10});
    const auto stats = engine.run();

    REQUIRE(stats.bars == 5);
    REQUIRE(dry.withheld() > 0);   // it wanted to trade
    REQUIRE(stats.fills == 0);     // and nothing was booked
    REQUIRE(engine.portfolio().open_positions() == 0);

    // Daily marks are still written, so a dry run shows the equity curve it
    // would have had. What it must not show is trades that never happened.
    journal.flush();
    const auto results = summarize(tmp.path);
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].trades == 0);
    REQUIRE(results[0].marks.size() == 5);
}

TEST_CASE("a dry run still reads the real account and bars") {
    MockVenue real(bars_from({100, 110}), Money::from_double(12345.0));
    DryRunVenue dry(real);

    REQUIRE(dry.account().cash == Money::from_double(12345.0));
    REQUIRE(dry.next_bar().has_value());
    REQUIRE(dry.name().find("dry run") != std::string::npos);
}
