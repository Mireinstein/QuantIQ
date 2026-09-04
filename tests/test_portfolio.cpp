#include <catch2/catch_test_macros.hpp>

#include "quantiq/portfolio.hpp"

using namespace quantiq;

namespace {
Fill make_fill(Side side, Quantity qty, double price) {
    return Fill{"AAPL", side, qty, Price::from_double(price), Timestamp{}, "t"};
}
}  // namespace

TEST_CASE("a buy opens a position at its fill price") {
    Portfolio p;
    p.apply(make_fill(Side::Buy, 50, 100.0));

    REQUIRE(p.holds("AAPL"));
    REQUIRE(p.find("AAPL")->quantity == 50);
    REQUIRE(p.find("AAPL")->avg_price == Price::from_double(100.0));
}

TEST_CASE("a second buy averages the cost basis") {
    Portfolio p;
    p.apply(make_fill(Side::Buy, 50, 100.0));
    p.apply(make_fill(Side::Buy, 50, 110.0));

    REQUIRE(p.find("AAPL")->quantity == 100);
    REQUIRE(p.find("AAPL")->avg_price == Price::from_double(105.0));
}

TEST_CASE("selling books profit against the average price and closes the position") {
    Portfolio p;
    p.apply(make_fill(Side::Buy, 50, 100.0));
    p.apply(make_fill(Side::Sell, 50, 110.0));

    REQUIRE_FALSE(p.holds("AAPL"));
    REQUIRE(p.realized().to_double() == 500.0);
    REQUIRE(p.open_positions() == 0);
}

TEST_CASE("a partial sell leaves the rest open at the same basis") {
    Portfolio p;
    p.apply(make_fill(Side::Buy, 100, 100.0));
    p.apply(make_fill(Side::Sell, 40, 105.0));

    REQUIRE(p.find("AAPL")->quantity == 60);
    REQUIRE(p.realized().to_double() == 200.0);
}

TEST_CASE("unrealised profit needs a mark and is zero without one") {
    Portfolio p;
    p.apply(make_fill(Side::Buy, 10, 100.0));

    REQUIRE(p.unrealized({}).to_double() == 0.0);
    REQUIRE(p.unrealized({{"AAPL", Price::from_double(120.0)}}).to_double() == 200.0);
}
