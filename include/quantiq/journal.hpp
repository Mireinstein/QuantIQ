#pragma once

#include <fstream>
#include <string>

#include "quantiq/types.hpp"

namespace quantiq {

/// Append-only record of everything the bot decided, one JSON object per line.
/// The report is computed from this file rather than from in-memory state, so a
/// crash loses at most the current bar and the numbers survive a restart.
///
/// The stream is opened in the constructor and closed by the destructor; if
/// opening fails the object never exists, so there is no half-built journal to
/// check for.
class Journal {
public:
    explicit Journal(const std::string& path);

    void fill(const Fill& f, const std::string& strategy, const std::string& reason);

    /// One line per bar per strategy: what the account was worth, what the
    /// instrument closed at, and how much was held.
    ///
    /// Recorded even when nothing traded, because a curve drawn only at trades
    /// cannot show when a drawdown happened, cannot be compared against buying
    /// and holding, and cannot say how much of the time capital was actually
    /// deployed.
    void mark(Timestamp ts, const std::string& strategy, const Symbol& symbol, Money equity,
              Price close, Quantity quantity);
    void rejected(const Order& o, const std::string& why);
    void session(const std::string& event, const std::string& detail);

    const std::string& path() const noexcept { return path_; }

private:
    void write_line(const std::string& json);

    std::string path_;
    std::ofstream out_;
};

}  // namespace quantiq
