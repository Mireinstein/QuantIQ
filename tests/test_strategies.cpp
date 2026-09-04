#include <catch2/catch_test_macros.hpp>

#include "quantiq/errors.hpp"
#include "quantiq/sizer.hpp"
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

/// The weight after each bar, so a test can assert on the shape of the whole
/// path rather than on one call.
std::vector<double> weights(Strategy& s, const std::vector<double>& closes) {
    std::vector<double> out;
    for (double c : closes) out.push_back(s.on_bar(bar_at(c)).weight);
    return out;
}

/// Where the target changed, and to what -- the equivalent of the old signal
/// list, derived rather than emitted.
std::vector<double> changes(Strategy& s, const std::vector<double>& closes) {
    std::vector<double> out;
    double previous = 0.0;
    for (double w : weights(s, closes)) {
        if (w != previous) out.push_back(w);
        previous = w;
    }
    return out;
}

}  // namespace

TEST_CASE("a strategy wants nothing until it has enough history") {
    SmaCrossover s(StrategyParams{{{"fast", 3}, {"slow", 5}}});
    for (int i = 0; i < 4; ++i) REQUIRE(s.on_bar(bar_at(100.0)).weight == 0.0);
}

TEST_CASE("the warm-up bar does not count as a crossing") {
    SmaCrossover s(StrategyParams{{{"fast", 2}, {"slow", 4}}});
    // Rising throughout, so the fast mean is already above the slow one when
    // the window first fills. That is a starting state, not a cross.
    REQUIRE(changes(s, {10, 11, 12, 13}).empty());
}

TEST_CASE("SmaCrossover goes long on the upward cross and flat on the way back") {
    SmaCrossover s(StrategyParams{{{"fast", 2}, {"slow", 4}}});
    const auto path = changes(s, {20, 19, 18, 17, 16, 15, 25, 30, 35, 20, 10, 5});

    REQUIRE(path.size() >= 2);
    REQUIRE(path[0] == 1.0);
    REQUIRE(path[1] == 0.0);
}

TEST_CASE("SmaCrossover never asks to be short") {
    SmaCrossover s(StrategyParams{{{"fast", 2}, {"slow", 4}}});
    for (double w : weights(s, {50, 45, 40, 35, 30, 25, 20, 15})) REQUIRE(w >= 0.0);
}

TEST_CASE("a repeated signal produces the same target, so the engine trades nothing") {
    SmaCrossover s(StrategyParams{{{"fast", 2}, {"slow", 4}}});
    const auto path = weights(s, {20, 19, 18, 17, 16, 15, 25, 30, 35, 40, 45});

    // Once long, the weight stays 1.0 rather than re-emitting a buy each bar.
    REQUIRE(path.back() == 1.0);
    REQUIRE(*(path.end() - 2) == 1.0);
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
    const auto moves = changes(s, path);

    REQUIRE(moves.size() == 2);
    REQUIRE(moves[0] == 1.0);
    REQUIRE(moves[1] == 0.0);
}

TEST_CASE("MeanReversion ignores a flat series where deviation is undefined") {
    MeanReversion s(StrategyParams{{{"window", 5}}});
    REQUIRE(changes(s, std::vector<double>(20, 100.0)).empty());
}

TEST_CASE("Momentum goes long once the move clears the threshold") {
    Momentum s(StrategyParams{{{"lookback", 3}, {"threshold", 0.05}}});
    const auto moves = changes(s, {100, 100, 100, 100, 120});

    REQUIRE(moves.size() == 1);
    REQUIRE(moves[0] == 1.0);
}

TEST_CASE("Breakout compares against the window before the current bar") {
    Breakout s(StrategyParams{{{"lookback", 3}}});
    s.on_bar(ohlc(10, 10, 10, 10));
    s.on_bar(ohlc(10, 11, 9, 10));
    s.on_bar(ohlc(10, 12, 9, 10));

    // Sets a new high and closes above the prior three-bar high in one bar; the
    // high it just printed must not be what it is measured against.
    REQUIRE(s.on_bar(ohlc(12, 20, 12, 19)).weight == 1.0);
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

TEST_CASE("sizing is by value, so cheap and expensive stocks get equal-sized bets") {
    const Sizer sizer(0.10);
    const auto equity = Money::from_double(100000.0);

    // 10% of $100k is $10k either way: 11 shares at $880, or 833 at $12.
    REQUIRE(sizer.shares(1.0, Price::from_double(880.0), equity) == 11);
    REQUIRE(sizer.shares(1.0, Price::from_double(12.0), equity) == 833);
}

TEST_CASE("a zero target is a flat position, whatever the price") {
    const Sizer sizer(0.10);
    REQUIRE(sizer.shares(0.0, Price::from_double(100.0), Money::from_double(100000.0)) == 0);
}

TEST_CASE("sizing rounds down, so a position never exceeds its budget") {
    const Sizer sizer(0.10);
    // $10k budget at $3000 a share is 3.33 shares, and 4 would overshoot.
    REQUIRE(sizer.shares(1.0, Price::from_double(3000.0), Money::from_double(100000.0)) == 3);
}

TEST_CASE("an account with no equity buys nothing rather than dividing by zero") {
    const Sizer sizer(0.10);
    REQUIRE(sizer.shares(1.0, Price::from_double(100.0), Money{}) == 0);
    REQUIRE(sizer.shares(1.0, Price{}, Money::from_double(100000.0)) == 0);
}

TEST_CASE("a nonsensical position fraction is rejected at construction") {
    REQUIRE_THROWS_AS(Sizer(0.0), ConfigError);
    REQUIRE_THROWS_AS(Sizer(1.5), ConfigError);
}
