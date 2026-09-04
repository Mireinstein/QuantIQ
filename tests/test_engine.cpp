#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "quantiq/alpaca.hpp"
#include "quantiq/engine.hpp"
#include "quantiq/errors.hpp"
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

/// Fires one buy then one sell, so an engine test can be about the engine
/// rather than about whether an indicator crossed.
class ScriptedStrategy : public Strategy {
public:
    std::optional<Signal> on_bar(const Bar&) override {
        ++seen_;
        if (seen_ == 2) return Signal{Side::Buy, 10, "scripted entry"};
        if (seen_ == 4) return Signal{Side::Sell, 10, "scripted exit"};
        return std::nullopt;
    }
    std::string name() const override { return "Scripted"; }

private:
    int seen_ = 0;
};

struct TempJournal {
    std::string path = "test-journal.jsonl";
    TempJournal() { std::filesystem::remove(path); }
    ~TempJournal() { std::filesystem::remove(path); }
};

}  // namespace

TEST_CASE("the engine walks every bar and books the fills it is given") {
    TempJournal tmp;
    MockVenue venue(bars_from({100, 100, 110, 120, 130}));
    ScriptedStrategy strategy;
    Journal journal(tmp.path);
    Risk risk;

    Engine engine(venue, strategy, journal, risk);
    const auto stats = engine.run();

    REQUIRE(stats.bars == 5);
    REQUIRE(stats.signals == 2);
    REQUIRE(stats.fills == 2);
    REQUIRE(stats.rejected == 0);
    REQUIRE(engine.portfolio().realized().to_double() == 200.0);
}

TEST_CASE("the report reconstructs the round trip from the journal alone") {
    TempJournal tmp;
    MockVenue venue(bars_from({100, 100, 110, 120, 130}));
    ScriptedStrategy strategy;
    Journal journal(tmp.path);
    Risk risk;

    Engine engine(venue, strategy, journal, risk);
    engine.run();

    const auto results = summarize(tmp.path);
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].strategy == "Scripted");
    REQUIRE(results[0].trades == 1);
    REQUIRE(results[0].wins == 1);
    REQUIRE(results[0].net.to_double() == 200.0);
}

TEST_CASE("risk refuses to sell a position that is not held") {
    Portfolio empty;
    Risk risk;
    std::string why;

    const Order sell{"AAPL", Side::Sell, 10, "S", ""};
    REQUIRE_FALSE(risk.allow(sell, empty, why));
    REQUIRE(why == "no position to sell");
}

TEST_CASE("risk caps how many names can be open at once") {
    Portfolio p;
    p.apply(Fill{"AAPL", Side::Buy, 1, Price::from_double(1.0), Timestamp{}, "1"});
    p.apply(Fill{"MSFT", Side::Buy, 1, Price::from_double(1.0), Timestamp{}, "2"});

    Risk risk(RiskLimits{.max_positions = 2});
    std::string why;

    // Adding to a name already held is fine; a third name is not.
    REQUIRE(risk.allow(Order{"AAPL", Side::Buy, 1, "S", ""}, p, why));
    REQUIRE_FALSE(risk.allow(Order{"NVDA", Side::Buy, 1, "S", ""}, p, why));
    REQUIRE(why == "at max positions");
}

TEST_CASE("a halt stops everything, including orders that were otherwise fine") {
    Portfolio p;
    Risk risk;
    std::string why;

    REQUIRE(risk.allow(Order{"AAPL", Side::Buy, 1, "S", ""}, p, why));
    risk.halt("manual");
    REQUIRE(risk.halted());
    REQUIRE_FALSE(risk.allow(Order{"AAPL", Side::Buy, 1, "S", ""}, p, why));
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
