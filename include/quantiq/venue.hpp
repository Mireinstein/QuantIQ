#pragma once

#include <optional>
#include <string>

#include "quantiq/types.hpp"

namespace quantiq {

/// Where bars come from and where orders go. Replay and live trading differ
/// only in which implementation is plugged in here -- the engine, the
/// strategies, the portfolio and the risk layer are identical either way, so
/// anything that passes against a MockVenue is running the same code path it
/// will run against Alpaca.
class Venue {
public:
    virtual ~Venue() = default;

    /// The next bar, or nullopt when the data is exhausted. A live venue blocks
    /// until one arrives; a replay venue returns immediately.
    virtual std::optional<Bar> next_bar() = 0;

    virtual Fill submit(const Order& order) = 0;
    virtual Account account() const = 0;
    virtual std::string name() const = 0;
};

}  // namespace quantiq
