#pragma once

#include <chrono>
#include <optional>
#include <ostream>
#include <string>

#include "quantiq/fixed.hpp"

namespace quantiq {

using Symbol = std::string;
using Timestamp = std::chrono::system_clock::time_point;

std::string to_date(Timestamp ts);
Timestamp parse_date(const std::string& yyyy_mm_dd);

enum class Side { Buy, Sell };

inline const char* to_string(Side s) { return s == Side::Buy ? "buy" : "sell"; }
inline std::ostream& operator<<(std::ostream& os, Side s) { return os << to_string(s); }

struct Bar {
    Symbol symbol;
    Timestamp ts;
    Price open;
    Price high;
    Price low;
    Price close;
    std::int64_t volume = 0;
};

std::ostream& operator<<(std::ostream& os, const Bar& b);

/// What a strategy emits. `reason` is carried through to the journal so the
/// report can say why a trade happened, not just that it did.
struct Signal {
    Side side;
    Quantity quantity;
    std::string reason;
};

struct Order {
    Symbol symbol;
    Side side;
    Quantity quantity;
    std::string strategy;
    std::string reason;
};

struct Fill {
    Symbol symbol;
    Side side;
    Quantity quantity;
    Price price;
    Timestamp ts;
    std::string order_id;
};

std::ostream& operator<<(std::ostream& os, const Fill& f);

struct Account {
    Money equity;
    Money cash;
};

}  // namespace quantiq
