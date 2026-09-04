#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "quantiq/venue.hpp"

namespace quantiq {

/// Replays a fixed sequence of bars and fills every order at the close of the
/// bar being processed. Used by the test suite with a handful of hand-written
/// bars, and by `--replay` with years of real history read from CSV; nothing
/// distinguishes the two cases except how the vector was populated.
class MockVenue : public Venue {
public:
    explicit MockVenue(std::vector<Bar> bars, Money starting_cash = Money::from_double(100000.0));

    static std::vector<Bar> bars_from_csv(const std::string& path);

    std::optional<Bar> next_bar() override;
    Fill submit(const Order& order) override;
    Account account() const override;
    std::string name() const override { return "mock"; }

    std::size_t bar_count() const noexcept { return bars_.size(); }
    const std::vector<Bar>& bars() const noexcept { return bars_; }

private:
    std::vector<Bar> bars_;
    std::size_t cursor_ = 0;
    Bar current_{};
    Money cash_;
    std::unordered_map<Symbol, Quantity> holdings_;
    int next_id_ = 1;
};

}  // namespace quantiq
