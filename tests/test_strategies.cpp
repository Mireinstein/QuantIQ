#include <catch2/catch_test_macros.hpp>

#include "quantiq/errors.hpp"
#include "quantiq/strategies.hpp"

using namespace quantiq;

namespace {

Bar bar_at(double close) {
    const auto p = Price::from_double(close);
    return Bar{"AAPL", Timestamp{}, p, p, p, p, 1000};
}

Bar ohlc(double open, double high, double low, double close) {
    return Bar{"AAPL",
               Timestamp{},
               Price::from_double(open),
               Price::from_double(high),
               Price::from_double(low),
               Price::from_double(close),
               1000};
}

std::vector<Signal> feed(Strategy& s, const std::vector<double>& closes) {
    std::vector<Signal> signals;
    for (double c : closes) {
        if (auto sig = s.on_bar(bar_at(c))) signals.push_back(*sig);
    }
    return signals;
}

}  // namespace

TEST_CASE("a strategy stays silent until it has enough history") {
    SmaCrossover s(StrategyParams{{{"fast", 3}, {"slow", 5}}});
    for (int i = 0; i < 4; ++i) REQUIRE_FALSE(s.on_bar(bar_at(100.0)).has_value());
}

TEST_CASE("the warm-up bar does not count as a crossing") {
    SmaCrossover s(StrategyParams{{{"fast", 2}, {"slow", 4}}});
    // Rising throughout, so the fast mean is already above the slow one when
    // the window first fills. That is a starting state, not a cross.
    REQUIRE(feed(s, {10, 11, 12, 13}).empty());
}

TEST_CASE("SmaCrossover buys on the upward cross and sells on the way back") {
    SmaCrossover s(StrategyParams{{{"fast", 2}, {"slow", 4}, {"size", 10}}});
    const auto signals = feed(s, {20, 19, 18, 17, 16, 15, 25, 30, 35, 20, 10, 5});

    REQUIRE(signals.size() >= 2);
    REQUIRE(signals[0].side == Side::Buy);
    REQUIRE(signals[0].quantity == 10);
    REQUIRE(signals[1].side == Side::Sell);
}

TEST_CASE("SmaCrossover never sells before it has bought") {
    SmaCrossover s(StrategyParams{{{"fast", 2}, {"slow", 4}}});
    const auto signals = feed(s, {50, 45, 40, 35, 30, 25, 20, 15});
    for (const auto& sig : signals) REQUIRE(sig.side != Side::Sell);
}

TEST_CASE("a fast window at or above the slow one is rejected at construction") {
    REQUIRE_THROWS_AS(SmaCrossover(StrategyParams{{{"fast", 20}, {"slow", 10}}}), ConfigError);
    REQUIRE_THROWS_AS(SmaCrossover(StrategyParams{{{"fast", 10}, {"slow", 10}}}), ConfigError);
}

TEST_CASE("MeanReversion buys a drop and exits on the recovery") {
    MeanReversion s(StrategyParams{{{"window", 5}, {"z_entry", -1.5}, {"z_exit", 0.0}}});

    std::vector<double> path(10, 100.0);
    path.push_back(90.0);   // the dislocation
    path.push_back(101.0);  // back through the mean
    const auto signals = feed(s, path);

    REQUIRE(signals.size() == 2);
    REQUIRE(signals[0].side == Side::Buy);
    REQUIRE(signals[1].side == Side::Sell);
}

TEST_CASE("MeanReversion ignores a flat series where deviation is undefined") {
    MeanReversion s(StrategyParams{{{"window", 5}}});
    REQUIRE(feed(s, std::vector<double>(20, 100.0)).empty());
}

TEST_CASE("Momentum buys once the move clears the threshold") {
    Momentum s(StrategyParams{{{"lookback", 3}, {"threshold", 0.05}}});
    const auto signals = feed(s, {100, 100, 100, 100, 120});

    REQUIRE(signals.size() == 1);
    REQUIRE(signals[0].side == Side::Buy);
}

TEST_CASE("Breakout compares against the window before the current bar") {
    Breakout s(StrategyParams{{{"lookback", 3}}});
    s.on_bar(ohlc(10, 10, 10, 10));
    s.on_bar(ohlc(10, 11, 9, 10));
    s.on_bar(ohlc(10, 12, 9, 10));

    // Sets a new high and closes above the prior three-bar high in one bar; the
    // high it just printed must not be what it is measured against.
    const auto signal = s.on_bar(ohlc(12, 20, 12, 19));
    REQUIRE(signal.has_value());
    REQUIRE(signal->side == Side::Buy);
}

TEST_CASE("every registered strategy can be built by name") {
    register_builtin_strategies();
    auto& registry = StrategyRegistry::instance();

    REQUIRE(registry.names().size() == 4);
    for (const auto& name : registry.names()) {
        auto s = registry.create(name, StrategyParams{});
        REQUIRE(s != nullptr);
        REQUIRE(s->name() == name);
    }
}

TEST_CASE("an unknown strategy name is a config error, not a crash") {
    register_builtin_strategies();
    REQUIRE_THROWS_AS(StrategyRegistry::instance().create("Nope", StrategyParams{}), ConfigError);
}
