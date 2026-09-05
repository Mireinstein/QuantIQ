#include "quantiq/alpaca.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>

#include <nlohmann/json.hpp>

#include "quantiq/errors.hpp"

namespace quantiq {

namespace {

constexpr auto kPaperHost = "paper-api.alpaca.markets";

Timestamp parse_rfc3339(const std::string& s) {
    std::tm tm{};
    std::istringstream is(s.substr(0, 19));
    is >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (is.fail()) throw DataError("unparseable timestamp: " + s);
    return std::chrono::system_clock::from_time_t(timegm(&tm));
}

Price price_of(const nlohmann::json& j, const char* key) {
    if (!j.contains(key) || j.at(key).is_null()) return Price{};
    const auto& v = j.at(key);
    return Price::from_double(v.is_string() ? std::stod(v.get<std::string>()) : v.get<double>());
}

}  // namespace

AlpacaVenue::AlpacaVenue() {
    load_env_file(".env");
    base_ = require_env("ALPACA_BASE_URL");
    data_base_ = require_env("ALPACA_DATA_URL");

    if (base_.find(kPaperHost) == std::string::npos) {
        throw ConfigError("ALPACA_BASE_URL must point at " + std::string(kPaperHost) +
                          ", refusing to trade against " + base_);
    }

    client_ = HttpClient({{"APCA-API-KEY-ID", require_env("ALPACA_API_KEY_ID")},
                          {"APCA-API-SECRET-KEY", require_env("ALPACA_API_SECRET_KEY")}});
}

Account AlpacaVenue::account() const {
    const auto j = nlohmann::json::parse(client_.get(base_ + "/v2/account"));
    if (j.value("trading_blocked", false)) throw ApiError("account is blocked from trading");

    return Account{Money::from_double(std::stod(j.at("equity").get<std::string>())),
                   Money::from_double(std::stod(j.at("cash").get<std::string>()))};
}

MarketClock AlpacaVenue::clock() const {
    const auto j = nlohmann::json::parse(client_.get(base_ + "/v2/clock"));
    return MarketClock{j.at("is_open").get<bool>(), parse_rfc3339(j.at("timestamp")),
                       parse_rfc3339(j.at("next_open")), parse_rfc3339(j.at("next_close"))};
}

std::vector<Position> AlpacaVenue::positions() const {
    const auto j = nlohmann::json::parse(client_.get(base_ + "/v2/positions"));

    std::vector<Position> out;
    for (const auto& p : j) {
        out.push_back(Position{p.at("symbol").get<std::string>(),
                               std::stoll(p.at("qty").get<std::string>()),
                               Price::from_double(std::stod(p.at("avg_entry_price").get<std::string>()))});
    }
    return out;
}

std::set<Symbol> AlpacaVenue::symbols_with_open_orders() const {
    const auto j = nlohmann::json::parse(client_.get(base_ + "/v2/orders?status=open"));

    std::set<Symbol> symbols;
    for (const auto& o : j) symbols.insert(o.at("symbol").get<std::string>());
    return symbols;
}

std::vector<Bar> AlpacaVenue::history(const Symbol& symbol, int days) const {
    // A start date is required: without one the endpoint answers with a null
    // bar list rather than an error, which reads as "no data for this symbol".
    // Weekends and holidays mean calendar days run well ahead of trading days,
    // so the window is padded before being trimmed to what was asked for.
    const auto start = std::chrono::system_clock::now() - std::chrono::hours(24 * (days * 2 + 10));

    const auto url = data_base_ + "/v2/stocks/" + symbol +
                     "/bars?timeframe=1Day&adjustment=split&feed=iex&start=" + to_date(start);
    const auto j = nlohmann::json::parse(client_.get(url));

    std::vector<Bar> bars;
    if (!j.contains("bars") || j.at("bars").is_null()) return bars;

    for (const auto& b : j.at("bars")) {
        bars.push_back(Bar{symbol, parse_rfc3339(b.at("t")), price_of(b, "o"), price_of(b, "h"),
                           price_of(b, "l"), price_of(b, "c"), b.value("v", std::int64_t{0})});
    }

    if (bars.size() > static_cast<std::size_t>(days)) {
        bars.erase(bars.begin(), bars.end() - days);
    }
    return bars;
}

void AlpacaVenue::push_bar(const Bar& bar) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.push_back(bar);
}

std::optional<Bar> AlpacaVenue::next_bar() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_.empty()) return std::nullopt;

    const Bar bar = pending_.front();
    pending_.pop_front();
    return bar;
}

Fill AlpacaVenue::submit(const Order& order) {
    const nlohmann::json body{{"symbol", order.symbol},
                              {"qty", order.quantity},
                              {"side", to_string(order.side)},
                              {"type", "market"},
                              {"time_in_force", "day"}};

    const auto j = nlohmann::json::parse(client_.post(base_ + "/v2/orders", body.dump()));
    return await_fill(j.at("id").get<std::string>(), order);
}

Fill AlpacaVenue::await_fill(const std::string& order_id, const Order& order) const {
    // Market orders normally fill in well under a second, but an order placed
    // outside regular hours stays queued until the open. Rather than block a
    // trading thread until then, give up after a few seconds and report a fill
    // of zero -- the caller books nothing, and the journal records the attempt.
    for (int attempt = 0; attempt < 20; ++attempt) {
        const auto j = nlohmann::json::parse(client_.get(base_ + "/v2/orders/" + order_id));
        const auto status = j.value("status", "");

        if (status == "filled") {
            return Fill{order.symbol, order.side,
                        std::stoll(j.at("filled_qty").get<std::string>()),
                        price_of(j, "filled_avg_price"), parse_rfc3339(j.at("filled_at")), order_id};
        }
        if (status == "rejected" || status == "canceled" || status == "expired") {
            throw OrderRejected("order " + order_id + " ended as " + status);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    return Fill{order.symbol, order.side, 0, Price{}, std::chrono::system_clock::now(), order_id};
}

}  // namespace quantiq
