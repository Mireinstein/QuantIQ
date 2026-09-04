#include "quantiq/types.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>

#include "quantiq/errors.hpp"

namespace quantiq {

std::string to_date(Timestamp ts) {
    const std::time_t t = std::chrono::system_clock::to_time_t(ts);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

Timestamp parse_date(const std::string& yyyy_mm_dd) {
    std::tm tm{};
    std::istringstream is(yyyy_mm_dd);
    is >> std::get_time(&tm, "%Y-%m-%d");
    if (is.fail()) throw DataError("unparseable date: " + yyyy_mm_dd);
    return std::chrono::system_clock::from_time_t(timegm(&tm));
}

std::ostream& operator<<(std::ostream& os, const Bar& b) {
    return os << b.symbol << ' ' << to_date(b.ts) << " o=" << b.open << " h=" << b.high
              << " l=" << b.low << " c=" << b.close << " v=" << b.volume;
}

std::ostream& operator<<(std::ostream& os, const Fill& f) {
    return os << to_string(f.side) << ' ' << f.quantity << ' ' << f.symbol << " @ " << f.price;
}

}  // namespace quantiq
