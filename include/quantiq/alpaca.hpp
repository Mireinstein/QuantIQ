#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "quantiq/http.hpp"
#include "quantiq/portfolio.hpp"
#include "quantiq/venue.hpp"

namespace quantiq {

struct MarketClock {
    bool is_open = false;
    Timestamp now;
    Timestamp next_open;
    Timestamp next_close;
};

/// Live paper trading against Alpaca.
///
/// The base URL is checked at construction: a typo that pointed this at the
/// live endpoint would trade real money, and that is not a mistake worth
/// leaving discoverable at runtime.
class AlpacaVenue : public Venue {
public:
    AlpacaVenue();

    std::optional<Bar> next_bar() override;
    Fill submit(const Order& order) override;
    Account account() const override;
    std::string name() const override { return "alpaca-paper"; }

    MarketClock clock() const;
    std::vector<Position> positions() const;

    /// Daily bars from Alpaca's own data API, so live and any replay built from
    /// it agree on what a bar is rather than mixing vendors.
    std::vector<Bar> history(const Symbol& symbol, int days) const;

    /// Fed by the market data feed; next_bar drains it.
    void push_bar(const Bar& bar);

private:
    Fill await_fill(const std::string& order_id, const Order& order) const;

    HttpClient client_;
    std::string base_;
    std::string data_base_;

    mutable std::mutex mutex_;
    std::deque<Bar> pending_;
};

}  // namespace quantiq
