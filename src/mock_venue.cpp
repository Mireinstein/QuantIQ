#include "quantiq/mock_venue.hpp"

#include <fstream>
#include <sstream>

#include "quantiq/errors.hpp"

namespace quantiq {

MockVenue::MockVenue(std::vector<Bar> bars, Money starting_cash)
    : bars_(std::move(bars)), cash_(starting_cash) {}

std::vector<Bar> MockVenue::bars_from_csv(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw DataError("cannot open bar file: " + path);

    std::vector<Bar> bars;
    std::string line;
    std::getline(in, line);  // header

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        std::string symbol, date, o, h, l, c, v;
        std::getline(fields, symbol, ',');
        std::getline(fields, date, ',');
        std::getline(fields, o, ',');
        std::getline(fields, h, ',');
        std::getline(fields, l, ',');
        std::getline(fields, c, ',');
        std::getline(fields, v, ',');
        if (c.empty()) throw DataError("short row in " + path + ": " + line);

        bars.push_back(Bar{symbol, parse_date(date), Price::from_double(std::stod(o)),
                           Price::from_double(std::stod(h)), Price::from_double(std::stod(l)),
                           Price::from_double(std::stod(c)),
                           v.empty() ? 0 : std::stoll(v)});
    }

    if (bars.empty()) throw DataError("no bars in " + path);
    return bars;
}

std::optional<Bar> MockVenue::next_bar() {
    if (cursor_ >= bars_.size()) return std::nullopt;
    current_ = bars_[cursor_++];
    return current_;
}

Fill MockVenue::submit(const Order& order) {
    // Fills at the close of the bar the strategy just saw. Real fills happen at
    // the next available price, so replay results are optimistic by roughly one
    // bar's move -- worth remembering before believing a replay number.
    const Money value = notional(current_.close, order.quantity);

    // Replay must not be able to spend money the account does not have, or the
    // equity curve it produces describes an account nobody could have held.
    if (order.side == Side::Buy && value > cash_) {
        throw InsufficientFunds("cannot buy " + std::to_string(order.quantity) + " " +
                                order.symbol + " for " + value.str() + " with " + cash_.str());
    }

    const Fill fill{order.symbol, order.side, order.quantity, current_.close, current_.ts,
                    "mock-" + std::to_string(next_id_++)};
    cash_ += order.side == Side::Buy ? -value : value;
    holdings_[order.symbol] += order.side == Side::Buy ? order.quantity : -order.quantity;
    return fill;
}

Account MockVenue::account() const {
    Money holdings_value;
    for (const auto& [symbol, quantity] : holdings_) {
        if (symbol == current_.symbol) holdings_value += notional(current_.close, quantity);
    }
    return Account{cash_ + holdings_value, cash_};
}

}  // namespace quantiq
