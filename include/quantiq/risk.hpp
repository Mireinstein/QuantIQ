#pragma once

#include <atomic>
#include <string>

#include "quantiq/portfolio.hpp"
#include "quantiq/types.hpp"

namespace quantiq {

struct RiskLimits {
    std::size_t max_positions = 5;
    double max_daily_loss = 2000.0;
    double max_drawdown = 0.15;
};

/// Sits between a signal and the venue. Every order passes through `allow`, and
/// once `halted` is set nothing else goes out for the rest of the session --
/// the flag is atomic because in live trading it is set from the strategy
/// thread and read from the order thread.
class Risk {
public:
    explicit Risk(RiskLimits limits = {}) : limits_(limits) {}

    bool allow(const Order& order, const Portfolio& portfolio, Price price,
               const Account& account, std::string& why_not);

    void observe_equity(Money equity);

    void halt(const std::string& reason);
    bool halted() const noexcept { return halted_.load(); }
    const std::string& halt_reason() const noexcept { return halt_reason_; }

    const RiskLimits& limits() const noexcept { return limits_; }

private:
    RiskLimits limits_;
    std::atomic<bool> halted_{false};
    std::string halt_reason_;
    Money peak_equity_;
    bool seen_equity_ = false;
};

}  // namespace quantiq
