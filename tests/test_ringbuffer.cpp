#include <catch2/catch_test_macros.hpp>

#include "quantiq/ringbuffer.hpp"

using namespace quantiq;

TEST_CASE("index zero is the newest value") {
    RingBuffer<int, 4> buf;
    buf.push(1);
    buf.push(2);
    buf.push(3);

    REQUIRE(buf.size() == 3);
    REQUIRE(buf[0] == 3);
    REQUIRE(buf[1] == 2);
    REQUIRE(buf[2] == 1);
}

TEST_CASE("pushing past capacity drops the oldest value") {
    RingBuffer<int, 3> buf;
    for (int i = 1; i <= 5; ++i) buf.push(i);

    REQUIRE(buf.full());
    REQUIRE(buf.size() == 3);
    REQUIRE(buf[0] == 5);
    REQUIRE(buf[2] == 3);
}

TEST_CASE("reading past the end throws rather than returning stale data") {
    RingBuffer<int, 4> buf;
    buf.push(1);
    REQUIRE_THROWS_AS(buf[1], std::out_of_range);
}

TEST_CASE("iteration runs newest to oldest") {
    RingBuffer<int, 4> buf;
    buf.push(1);
    buf.push(2);
    buf.push(3);

    std::vector<int> seen;
    for (int v : buf) seen.push_back(v);
    REQUIRE(seen == std::vector<int>{3, 2, 1});
}
