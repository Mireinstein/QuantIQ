#include <catch2/catch_test_macros.hpp>

#include "quantiq/fixed.hpp"

using namespace quantiq;

TEST_CASE("prices round-trip through doubles without drift") {
    const auto p = Price::from_double(189.33);
    REQUIRE(p.ticks() == 1893300);
    REQUIRE(p.to_double() == 189.33);
    REQUIRE(p.str() == "189.33");
}

TEST_CASE("repeated addition does not accumulate float error") {
    Money total;
    for (int i = 0; i < 1000; ++i) total += Money::from_double(0.01);
    REQUIRE(total.to_double() == 10.0);
}

TEST_CASE("comparison is exact") {
    REQUIRE(Price::from_double(1.10) > Price::from_double(1.09));
    REQUIRE(Price::from_double(0.30) == Price::from_double(0.10) + Price::from_double(0.20));
}

TEST_CASE("notional multiplies a price by a share count") {
    REQUIRE(notional(Price::from_double(10.50), 200).to_double() == 2100.0);
}
