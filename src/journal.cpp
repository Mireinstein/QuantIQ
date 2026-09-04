#include "quantiq/journal.hpp"

#include <nlohmann/json.hpp>

#include "quantiq/errors.hpp"

namespace quantiq {

Journal::Journal(const std::string& path) : path_(path), out_(path, std::ios::app) {
    if (!out_) throw DataError("cannot open journal for writing: " + path);
}

void Journal::write_line(const std::string& json) {
    out_ << json << '\n';
    out_.flush();  // a crash should not cost the trades already made
}

void Journal::fill(const Fill& f, const std::string& strategy, const std::string& reason) {
    nlohmann::json j{{"event", "fill"},
                     {"ts", to_date(f.ts)},
                     {"symbol", f.symbol},
                     {"strategy", strategy},
                     {"side", to_string(f.side)},
                     {"quantity", f.quantity},
                     {"price", f.price.to_double()},
                     {"order_id", f.order_id},
                     {"reason", reason}};
    write_line(j.dump());
}

void Journal::rejected(const Order& o, const std::string& why) {
    nlohmann::json j{{"event", "rejected"},
                     {"symbol", o.symbol},
                     {"strategy", o.strategy},
                     {"side", to_string(o.side)},
                     {"quantity", o.quantity},
                     {"reason", why}};
    write_line(j.dump());
}

void Journal::session(const std::string& event, const std::string& detail) {
    nlohmann::json j{{"event", event}, {"detail", detail}};
    write_line(j.dump());
}

}  // namespace quantiq
