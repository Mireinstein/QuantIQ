#pragma once

#include <memory>

#include "quantiq/venue.hpp"

namespace quantiq {

/// Wraps a venue and reports what would have been sent instead of sending it.
///
/// Bars, account and positions still come from the real venue, so the decisions
/// are the ones the bot would genuinely make -- only the order is withheld.
/// This exists because the obvious way to try the bot out is to run it, and
/// without a way to do that safely the first thing a new user does is put real
/// orders on their account by accident.
class DryRunVenue : public Venue {
public:
    explicit DryRunVenue(Venue& real) : real_(real) {}

    std::optional<Bar> next_bar() override { return real_.next_bar(); }
    Account account() const override { return real_.account(); }
    std::string name() const override { return real_.name() + " (dry run)"; }

    Fill submit(const Order& order) override;

    std::size_t withheld() const noexcept { return withheld_; }

private:
    Venue& real_;
    std::size_t withheld_ = 0;
};

}  // namespace quantiq
